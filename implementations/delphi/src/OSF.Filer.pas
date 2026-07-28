// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

unit OSF.Filer;

interface

uses
  System.SysUtils,
  System.Classes,
  System.DateUtils,
  System.Generics.Collections,
  System.JSON,
  System.ZLib,
  Xml.XMLIntf,
  Xml.XMLDoc,
  // OmniXML - pure-Pascal DOM. Pulling it in registers the vendor so we
  // can route ParseXMLMeta away from MSXML; that keeps OSF4 reading
  // working on hosts where MSXML / IE is missing or broken.
  Xml.omnixmldom,
  Xml.XMLDom,
  OSF.Types,
  OSF.CRC32C,
  OSF.Channel,
  OSF.Log;

type
  // Outcome of a low-level block read: a decoded block, a skip (bytes
  // consumed, read on), or a stop (truncation / unrecoverable).
  TOSFBlockOutcome = (boBlock, boSkip, boStop);

  // File-level metadata written in the OSF meta block.
  // When writing: set fields before calling WriteHeader.
  // When reading: populated by OpenForRead after the meta block is parsed.
  TOSFFileMetadata = record
    CreatedUtc: TDateTime; // UTC; writer defaults to UtcNow if zero
    Creator: string;
    Tag: string;
    Reason: string;
    Comment: string;
    NamespaceSep: string; // default '.'
    Longitude: Double;
    Latitude: Double;
    Altitude: Double;
  end;

  // A single data block read from the stream.
  // Callers use the associated channel's DataType to interpret RawPayload.
  //
  // Layout of RawPayload by BlockType:
  // bcStartData             - N contiguous encoded data values (timestamp in StartTimestampNs)
  // bcContinuedData         - N contiguous encoded data values
  // bcAbsTimeStampData      - N × [int64 timestamp, encoded value] interleaved
  // bcContinuedRelStampData - N × [uint32 delta_ns, encoded value] interleaved (OSF4 only)
  // info block ($FFFF)      - raw UTF-8 XML or JSON (IsInfoBlock = True)
  TOSFDataBlock = record
    ChannelIndex: Word;
    BlockType: TBlockContent;
    MultiValue: Boolean; // bit 7 of the control byte
    SampleCount: UInt32; // 1 when not MultiValue
    StartTimestampNs: Int64; // bcStartData only; 0 for all other types
    SampleRate: Double; // bcStartData only; 0.0 for all other types
    RawPayload: TBytes;
    IsInfoBlock: Boolean; // True when ChannelIndex = $FFFF
  end;

  TOSFFileMode = (fmClosed, fmRead, fmWrite);

  TOSFFile = class
  private
    FStream: TStream;
    FOwnsStream: Boolean;
    // When the source file is OSFZ (gzip-wrapped), FStream is a
    // TZDecompressionStream and FUnderlyingStream is the raw TFileStream
    // underneath it. Both must be freed by Close (decompressor first).
    // FUnderlyingStream is nil for plain OSF files and for the raw-stream
    // OpenForRead overload (where the caller owns the source).
    FUnderlyingStream: TStream;
    FMode: TOSFFileMode;
    FVersion: TOSFVersion;
    FMetaFormat: TOSFMetaFormat;
    FChannels: TObjectList<TOSFChannelDef>;
    FMetadata: TOSFFileMetadata;
    FInfoItems: TList<TOSFMetaItem>;
    FHeaderWritten: Boolean;

    // Optional channel name filter. When non-empty, ReadNextBlock silently
    // skips data blocks whose channel name (looked up in the parsed
    // metablock) is not in the list. Info blocks (channel index $FFFF) are
    // always delivered regardless. Names are compared case-insensitively.
    FChannelFilter: TArray<string>;
    FChannelIncluded: TDictionary<Word, Boolean>;

    // True once a truncation has been detected during ReadNextBlock.
    // Read by TOSFDataManager and TOSFMetaCacheBuilder after the scan
    // completes; reset to False on every OpenForRead.
    FTruncationSeen: Boolean;
    // Source filename, set by the file-based OpenForRead/CreateForWrite
    // overloads. Used purely for log message formatting; empty when the
    // user opened the filer on a raw stream.
    FSourceName: string;

    // Integrity profile (OSF5 integrity token). On read: parsed from the
    // magic-header token in ReadMagicAndMeta. On write: set via
    // IntegrityProfile before WriteHeader (None or Crc32c only).
    FIntegrity: TOSFIntegrityProfile;
    // CRC32C of the raw metablock bytes carried by the crc32c token
    // (valid when FIntegrity >= ipCrc32c).
    FMetablockCRC: UInt32;
    // ed25519 keyid from the header token (level signed); empty otherwise.
    FEd25519KeyId: string;
    // Read-side integrity counters, reset in ReadMagicAndMeta.
    FBlocksCRCFailed: UInt32;
    FBlocksUnknownTypeSkipped: UInt32;
    FBlocksSignatureSkipped: UInt32;
    FBlocksZeroLengthSkipped: UInt32;

    // Magic header line + meta block dispatcher.
    procedure ReadMagicAndMeta;

    // JSON meta block - build / parse split into focused helpers.
    function BuildJSONMeta: TBytes;
    procedure AppendJSONFileNode(Parent: TJSONObject);
    procedure AppendJSONChannels(Parent: TJSONObject);
    procedure AppendJSONInfoArray(Parent: TJSONObject);
    procedure ParseJSONMeta(const Data: TBytes);
    procedure ParseJSONFileMetadata(FileNode: TJSONObject);
    procedure ParseJSONChannels(OSFNode: TJSONObject);
    procedure ParseJSONInfo(OSFNode: TJSONObject);

    // XML meta block - build / parse split into focused helpers.
    function BuildXMLMeta: TBytes;
    procedure AppendXMLOpenTag(B: TStringBuilder);
    procedure AppendXMLChannels(B: TStringBuilder);
    procedure AppendXMLInfos(B: TStringBuilder);
    procedure ParseXMLMeta(const Data: TBytes);
    procedure ParseXMLRootAttributes(RootNode: IXMLNode);
    procedure ParseXMLChannels(RootNode: IXMLNode);
    procedure ParseXMLInfos(RootNode: IXMLNode);

    // Block reading - split so each helper stays under 30 lines and the
    // truncation guards are at the top of their function.
    function TryReadChannelIndex(out ChannelIndex: Word): Boolean;
    function ReadInfoBlock(var Block: TOSFDataBlock): TOSFBlockOutcome;
    function ReadSignatureBlock: TOSFBlockOutcome;
    function ReadDataBlock(ChannelIndex: Word; var Block: TOSFDataBlock): TOSFBlockOutcome;
    function DecodeBlockPayload(Channel: TOSFChannelDef; const Payload: TBytes; LenField: UInt32; var Block: TOSFDataBlock): TOSFBlockOutcome;
    // Integrity level crc: verify the trailing 4-byte frame CRC over the
    // whole frame (channel index, length field, control byte, payload).
    function VerifyFrameCRC(ChannelIndex: Word; LFS: TOSFLengthFieldSize;
      LenField: UInt32; const Payload: TBytes): Boolean;
    // Parse the OSF5 magic-header integrity token(s); raises on unknown key.
    procedure ParseHeaderTokens(const Parts: TArray<string>);
    // Reads block headers, skipping any whose channel is excluded by the
    // active ChannelFilter, until a deliverable block is found.
    // Returns False on clean EOF or on a truncation that cannot be recovered.
    function SkipExcludedBlocksUntilIncluded(out ChannelIndex: Word; out StartOffset: Int64): Boolean;

    // Shared low-level write - emits channel index, length field, and payload.
    procedure WriteDataBlock(Channel: TOSFChannelDef; const Payload: TBytes);

    // Channel lookup.
    function FindChannel(Index: Integer): TOSFChannelDef;

    // Stream primitives. ReadXxx raise EReadError on short reads.
    function ReadUInt16: Word;
    function ReadUInt32: UInt32;
    procedure WriteUInt16(Value: Word);
    procedure WriteUInt32(Value: UInt32);
    procedure WriteRawBytes(const Data: TBytes);

    function GetChannelCount: Integer;
    function GetInfoItemCount: Integer;

    // Channel filter helpers.
    procedure SetChannelFilter(const Value: TArray<string>);
    procedure RebuildChannelFilterMap;
    function IsChannelExcluded(ChannelIndex: Word): Boolean;
    procedure SkipExcludedBlock(Channel: TOSFChannelDef);
  public
    constructor Create;
    destructor Destroy; override;

    // Detects the OSF version of the stream without consuming it.
    // The stream position is restored before returning. Returns False if the
    // stream does not start with a recognised OSF magic header.
    class function PeekMagic(AStream: TStream; out AVersion: TOSFVersion): Boolean;

    // ── Reading ───────────────────────────────────────────────────────────
    procedure OpenForRead(const FileName: string); overload;
    procedure OpenForRead(AStream: TStream; AOwnsStream: Boolean = False); overload;

    // Reads the next data block from the stream.
    // Returns False at clean EOF or when the stream is truncated mid-block
    // (best-effort: all complete blocks before the truncation point are returned).
    // Never raises on a truncated or partially-written file.
    function ReadNextBlock(out Block: TOSFDataBlock): Boolean;

    // ── Writing ───────────────────────────────────────────────────────────
    procedure CreateForWrite(const FileName: string; AVersion: TOSFVersion = osvOSF5); overload;
    procedure CreateForWrite(AStream: TStream; AOwnsStream: Boolean = False; AVersion: TOSFVersion = osvOSF5); overload;

    // Registers a channel definition. Must be called before WriteHeader.
    // Raises EOSFException on duplicate channel index or duplicate non-empty name.
    // Returns the channel index (= Def.Index).
    function AddChannel(Def: TOSFChannelDef): Integer;

    // Adds a free-form metadata item. Items are written into the OSF5 "info"
    // array or the OSF4 <infos> section. Must be called before WriteHeader.
    procedure AddInfoItem(const AName, AValue: string; const ADataType: string = 'string'; const AUnit: string = '');

    // Commits the magic header and meta block to the stream.
    // Must be called after all AddChannel/AddInfoItem calls and before any
    // WriteXxx data calls. Defaults CreatedUtc to UtcNow and NamespaceSep to '.'
    // if not set. The meta format follows FVersion (XML for OSF4, JSON for OSF5).
    procedure WriteHeader;

    // Writes an equidistant block for ChannelIndex.
    //
    // Only Double values are supported by this method. Double is by far the
    // most common data type for equidistant measurement channels. For other
    // numeric types (int16, float, ...) write raw blocks using the stream directly.
    //
    // FirstTimestampNs > 0 emits bcStartData and opens a new segment for the
    // channel. The very first block of every channel must pass a non-zero
    // timestamp; later in the file, passing a non-zero timestamp again starts
    // an additional segment (used for triggered captures or drift correction).
    // FirstTimestampNs = 0 (the default) emits bcContinuedData and appends to
    // the most recent segment.
    //
    // Channel.SampleRate must be set before the first call. The same value is
    // emitted on every bcStartData block.
    //
    // For OSF4 equidistant channels, also pre-set Channel.StartTimestampNs
    // before calling WriteHeader so the XML <channel> attribute carries the
    // start timestamp in the form expected by OSF4-only readers.
    procedure WriteEquidistantBlock(ChannelIndex: Integer; const Samples: array of Double; FirstTimestampNs: Int64 = 0);

    // Writes a single timestamped sample as a bcAbsTimeStampData block.
    // Value must contain the raw encoded bytes for the channel's DataType.
    procedure WriteTimestampedSample(ChannelIndex: Integer; TimestampNs: Int64; const Value: TBytes);

    // Writes multiple timestamped samples as one bcAbsTimeStampData block.
    // For variable-length channels (string/binary) in multi-sample blocks,
    // a uint32 length prefix is added per value as required by the spec.
    // Length(Timestamps) must equal Length(Values).
    procedure WriteTimestampedBlock(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of TBytes);

    // Convenience: write a batch of timestamped doubles in one block.
    procedure WriteTimestampedDoubles(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of Double);

    // Flushes and closes the stream.
    procedure Close;

    // ── Properties ────────────────────────────────────────────────────────
    property Version: TOSFVersion read FVersion;
    property MetaFormat: TOSFMetaFormat read FMetaFormat;
    property Channels: TObjectList<TOSFChannelDef> read FChannels;
    property ChannelCount: Integer read GetChannelCount;
    property InfoItems: TList<TOSFMetaItem> read FInfoItems;
    property InfoItemCount: Integer read GetInfoItemCount;
    property Metadata: TOSFFileMetadata read FMetadata write FMetadata;

    // Looks up a channel by name; returns nil if not found.
    function ChannelByName(const Name: string): TOSFChannelDef;
    // Looks up a channel by its Index attribute; returns nil if not found.
    function ChannelByIndex(Index: Integer): TOSFChannelDef;

    // Returns True if the channel (by metablock index) is currently
    // included by the active filter, or if no filter is set. The filter
    // map is built when OpenForRead completes; before OpenForRead has been
    // called, the function returns True for every index.
    function IsChannelIncluded(ChannelIndex: Integer): Boolean;

    // True once the reader has aborted a block mid-way because the
    // stream ran out before the block was complete. Reset by each
    // OpenForRead; consumed by higher layers (TOSFDataManager,
    // TOSFMetaCacheBuilder) to report a partial-load condition.
    property TruncationSeen: Boolean read FTruncationSeen;

    // ── Integrity profile (OSF5) ──────────────────────────────────────────
    // On read: the level declared by the magic-header token. On write: set
    // to ipNone (default) or ipCrc32c before WriteHeader to emit the profile.
    property IntegrityProfile: TOSFIntegrityProfile read FIntegrity write FIntegrity;
    // ed25519 keyid from the header token (level signed); empty otherwise.
    property Ed25519KeyId: string read FEd25519KeyId;
    // Read-side counters: blocks dropped by a failed frame CRC, unknown
    // block types skipped (forward-compat), signature blocks skipped, and
    // zero-length blocks skipped (a non-conforming writer artefact — the
    // frame is only the channel index and the length field, so it is
    // consumed and skipped rather than treated as a truncation).
    property BlocksCRCFailed: UInt32 read FBlocksCRCFailed;
    property BlocksUnknownTypeSkipped: UInt32 read FBlocksUnknownTypeSkipped;
    property BlocksSignatureSkipped: UInt32 read FBlocksSignatureSkipped;
    property BlocksZeroLengthSkipped: UInt32 read FBlocksZeroLengthSkipped;
    // Verification status vocabulary (osf5_integrity.md §1.6):
    // 'none' | 'crc_valid' | 'invalid' | 'signature_unverifiable'.
    function VerificationStatus: string;

    // Optional channel name filter for reads. When empty (the default), every
    // data block is delivered to ReadNextBlock callers - existing behaviour.
    // When populated, ReadNextBlock silently skips data blocks whose channel
    // name is not in the list; skipped bytes are still consumed from the
    // stream so block alignment is preserved. Info blocks (channel index
    // $FFFF) are always delivered. Names are matched case-insensitively.
    // The metablock itself is unaffected - Channels[] always contains every
    // channel definition from the on-disk metablock, filter or not.
    // The filter map is (re)built whenever ChannelFilter is set or
    // OpenForRead completes; callers may set the filter either before or
    // after OpenForRead.
    property ChannelFilter: TArray<string> read FChannelFilter write SetChannelFilter;
  end;

resourcestring
  // Lifecycle / mode errors.
  SOSFFileAlreadyOpen = 'TOSFFile: already open';
  SOSFCreateForWriteBadVersion = 'CreateForWrite: AVersion must be osvOSF4 or osvOSF5';
  SOSFAddChannelAfterHeader = 'AddChannel must be called before WriteHeader';
  SOSFAddInfoItemAfterHeader = 'AddInfoItem must be called before WriteHeader';
  SOSFWriteHeaderNotWriteMode = 'WriteHeader requires the file to be open for writing';
  SOSFWriteHeaderAlreadyCalled = 'WriteHeader has already been called';
  SOSFWriteHeaderBadVersion = 'WriteHeader: unsupported FVersion';
  SOSFWriteBeforeHeader = 'WriteHeader must be called before writing data blocks';
  SOSFWriterIntegrityOSF5Only = 'The integrity profile is an OSF5-only feature';
  SOSFWriterSigningUnsupported = 'This writer implements integrity level crc only; signing (ed25519) is not supported';
  SOSFBlockLengthOverflow = 'block length %d bytes (incl. frame CRC) overflows the u16 length field of channel %d; use sizeoflengthvalue=4';

  // AddChannel uniqueness errors.
  SOSFDuplicateChannelIndex = 'AddChannel: duplicate channel index %d';
  SOSFDuplicateChannelName = 'AddChannel: duplicate channel name "%s"';

  // Header / meta-block parse errors.
  SOSFUnexpectedEOFInHeader = 'Unexpected end of stream reading magic header';
  SOSFInvalidMagicHeader = 'Invalid OSF magic header: %s';
  SOSFUnsupportedMagicToken = 'Unsupported OSF magic token: "%s"';
  SOSFInvalidMetaBlockSize = 'Invalid meta block size in header: "%s"';
  SOSFUnknownMetaFormat = 'Unknown meta block format: first byte is 0x%02X (expected ''<'' or ''{'')';
  SOSFParseJSONMetaFailed = 'Failed to parse OSF5 JSON meta block';
  SOSFMissingOSFKey = 'JSON meta block is missing the top-level "osf" key';
  SOSFXMLNoRootElement = 'XML meta block has no root element';

  // Data-block read errors.
  SOSFUnknownChannelInBlock = 'Data block references channel index %d which is not defined in the meta block';
  SOSFUnknownLengthFieldSize = 'Data block for channel %d declares an unrecognised length-field width';

  // Data-block write errors.
  SOSFEquiUnknownChannel = 'WriteEquidistantBlock: unknown channel index %d';
  SOSFEquiNoFirstTimestamp = 'WriteEquidistantBlock: FirstTimestampNs must be provided for the first block of each channel';
  SOSFEquiNoSampleRate = 'WriteEquidistantBlock: Channel.SampleRate must be > 0 before the first equidistant block is written';
  SOSFTimestampedUnknown = 'WriteTimestampedSample: unknown channel index %d';
  SOSFTSBlockUnknown = 'WriteTimestampedBlock: unknown channel index %d';
  SOSFTSBlockLengthMismatch = 'WriteTimestampedBlock: Timestamps and Values lengths must match';
  SOSFTSDoublesLengthMismatch = 'WriteTimestampedDoubles: Timestamps and Values lengths must match';

  // Log messages - informational, debug and warning text emitted via Logger.
  SOSFLogOpeningFile = 'Opening file for read: %s (%d bytes)';
  SOSFLogDetectedVersion = 'Detected version: %s, meta format: %s';
  SOSFLogChannelsDefined = 'Channels defined in meta block: %d';
  SOSFLogChannelEntry = '  [%d] %s  type=%s  equidistant=%s';
  SOSFLogBlockRead = 'Block: channel=%d  type=%s  samples=%d  bytes=%d';
  SOSFLogTruncatedBlock = 'Truncated block at offset %d - stopping';
  SOSFLogUnknownBlockTypeInfo = 'Unknown block type %d in info block - skipping';
  SOSFLogUnknownChannelInBlock = 'Block references unknown channel index %d - skipping';
  SOSFLogUnknownBlockType = 'Unknown block type %d at offset %d - skipping';
  SOSFLogWritingHeader = 'Writing header: version=%s  channels=%d';
  SOSFLogWriteEquidistant = 'WriteEquidistant: channel=%d  samples=%d';
  SOSFLogWriteTimestamped = 'WriteTimestamped: channel=%d  samples=%d';
  SOSFLogFileClosed = 'File closed: %s  total bytes=%d';
  SOSFLogChannelFilterSkip = 'Channel filter: skipping block for "%s" (%d bytes)';
  SOSFLogOSFZDetected = 'OSFZ container detected, decompressing on the fly: %s';
  // Integrity profile (OSF5 level crc).
  SOSFUnknownHeaderToken = 'unknown header token ''%s''';
  SOSFMalformedHeaderLine = 'malformed magic header line: fields must be separated by a single space with no trailing space';
  SOSFTokenNotAllowedOSF4 = 'header token ''%s'' is not allowed on an OSF4 identifier';
  SOSFInvalidCrc32cToken = 'crc32c header token must be 8 uppercase hex digits: "%s"';
  SOSFInvalidEd25519Token = 'ed25519 header keyid must be 16 lowercase hex digits: "%s"';
  SOSFEd25519WithoutCrc = 'ed25519 header token requires a preceding crc32c token';
  SOSFMetablockCrcMismatch = 'metablock CRC mismatch: token 0x%.8x, computed 0x%.8x';
  SOSFLogFrameCRCMismatch = 'Frame CRC mismatch on channel %d at offset %d - skipping block';
  SOSFLogFrameCRCTooShort = 'Block on channel %d too short to carry a frame CRC - skipping block';
  SOSFLogUnknownBlockTypeSkip = 'Unknown block type %d on channel %d at offset %d - skipping';
  SOSFLogZeroLengthBlockSkip = 'Zero-length data block on channel %d at offset %d - skipping';
  SOSFLogSignatureBlockSkipped = 'Integrity signature block (channel 0xFFFE) skipped - not verified';

implementation

const
  OSF_FORMAT_OSF5 = 'osf5';

  // ── Local helpers ────────────────────────────────────────────────────────────

function XMLEscape(const S: string): string;
begin
  Result := S;
  Result := StringReplace(Result, '&', '&amp;', [rfReplaceAll]);
  Result := StringReplace(Result, '"', '&quot;', [rfReplaceAll]);
  Result := StringReplace(Result, '<', '&lt;', [rfReplaceAll]);
  Result := StringReplace(Result, '>', '&gt;', [rfReplaceAll]);
end;

function XMLAttrStrLocal(Node: IXMLNode; const AttrName, Default: string): string;
begin
  if Node.HasAttribute(AttrName) then
    Result := Node.Attributes[AttrName]
  else
    Result := Default;
end;

function XMLAttrDoubleLocal(Node: IXMLNode; const AttrName: string; Default: Double): Double;
var
  FS: TFormatSettings;
  S: string;
begin
  if not Node.HasAttribute(AttrName) then
    Exit(Default);
  S := Node.Attributes[AttrName];
  S := StringReplace(S, ',', '.', [rfReplaceAll]);
  FS := TFormatSettings.Invariant;
  Result := StrToFloatDef(S, Default, FS);
end;

function JStr(Obj: TJSONObject; const Key, Default: string): string;
var
  Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  if Assigned(Val) then
    Result := Val.Value
  else
    Result := Default;
end;

function JDbl(Obj: TJSONObject; const Key: string; Default: Double): Double;
var
  Val: TJSONValue;
  FS: TFormatSettings;
  S: string;
begin
  Val := Obj.GetValue(Key);
  if not Assigned(Val) then
    Exit(Default);
  if Val is TJSONNumber then
    Exit((Val as TJSONNumber).AsDouble);
  S := Val.Value;
  S := StringReplace(S, ',', '.', [rfReplaceAll]);
  FS := TFormatSettings.Invariant;
  Result := StrToFloatDef(S, Default, FS);
end;

// Formats a TDateTime as UTC ISO 8601 with millisecond precision and 'Z' suffix.
function FormatUTCDateTime(DT: TDateTime): string;
begin
  Result := FormatDateTime('yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"', DT);
end;

// Parses an ISO 8601 datetime string of the form yyyy-mm-ddThh:nn:ss[.zzz][Z].
// Returns 0 on parse failure.
function ParseISO8601DateTime(const S: string): TDateTime;
var
  Y, M, D, H, N, Sec, Ms: Word;
begin
  Result := 0;
  if Length(S) < 19 then
    Exit;
  try
    Y := StrToInt(Copy(S, 1, 4));
    M := StrToInt(Copy(S, 6, 2));
    D := StrToInt(Copy(S, 9, 2));
    H := StrToInt(Copy(S, 12, 2));
    N := StrToInt(Copy(S, 15, 2));
    Sec := StrToInt(Copy(S, 18, 2));
    Ms := 0;
    if (Length(S) >= 23) and (S[20] = '.') then
      Ms := StrToIntDef(Copy(S, 21, 3), 0);
    Result := EncodeDateTime(Y, M, D, H, N, Sec, Ms);
  except
    Result := 0;
  end;
end;

function VersionToLogString(V: TOSFVersion): string;
begin
  case V of
    osvOSF4:
      Result := 'OSF4';
    osvOSF5:
      Result := 'OSF5';
    osvUnknown:
      Result := 'unknown';
  else
    Result := 'unknown';
  end;
end;

function MetaFormatToLogString(F: TOSFMetaFormat): string;
begin
  case F of
    mfXML:
      Result := 'XML';
    mfJSON:
      Result := 'JSON';
  else
    Result := '?';
  end;
end;

function BlockTypeToLogString(BT: TBlockContent): string;
begin
  case BT of
    bcReserved:
      Result := 'reserved';
    bcTrustedTimestamp:
      Result := 'trustedTimestamp';
    bcTimebaseRealign:
      Result := 'timebaseRealign';
    bcStatusEvent:
      Result := 'statusEvent';
    bcMessageEvent:
      Result := 'messageEvent';
    bcContinuedData:
      Result := 'continuedData';
    bcStartData:
      Result := 'startData';
    bcContinuedRelStampData:
      Result := 'continuedRelStampData';
    bcAbsTimeStampData:
      Result := 'absTimeStampData';
  else
    Result := 'unknown';
  end;
end;

function BoolToLogString(B: Boolean): string;
begin
  if B then
    Result := 'true'
  else
    Result := 'false';
end;

// Reads a single LF-terminated ASCII line from the stream. Strips a trailing CR
// if present. Stops at the first LF or after MaxLen characters as a safety bound.
function ReadAsciiLine(AStream: TStream; MaxLen: Integer = 1024): AnsiString;
var
  Ch: AnsiChar;
  BytesRead: Integer;
  Done: Boolean;
begin
  Result := '';
  Done := False;
  while (not Done) and (Length(Result) < MaxLen) do
  begin
    BytesRead := AStream.Read(Ch, 1);
    if BytesRead = 0 then
      Done := True
    else if Ch = #10 then
      Done := True
    else
      Result := Result + Ch;
  end;
  if (Length(Result) > 0) and (Result[Length(Result)] = #13) then
    SetLength(Result, Length(Result) - 1);
end;

// Builds the binary payload of an equidistant data block.
// Layout: [ctrl 1B] [int64 ts 8B + double SampleRate 8B if start] [uint32 N 4B if N>1] [double × N]
function EncodeEquidistantPayload(IsStart: Boolean; FirstTimestampNs: Int64; SampleRate: Double; const Samples: array of Double): TBytes;
var
  CtrlByte: Byte;
  PayloadSize: Integer;
  Pos: Integer;
  N: Integer;
  NVal: UInt32;
begin
  N := Length(Samples);
  if IsStart then
    CtrlByte := OSFMakeControlByte(bcStartData, N > 1)
  else
    CtrlByte := OSFMakeControlByte(bcContinuedData, N > 1);

  PayloadSize := 1;
  if IsStart then
    Inc(PayloadSize, 16); // int64 ts + double SampleRate
  if N > 1 then
    Inc(PayloadSize, 4);
  Inc(PayloadSize, N * SizeOf(Double));

  SetLength(Result, PayloadSize);
  Pos := 0;
  Result[Pos] := CtrlByte;
  Inc(Pos);

  if IsStart then
  begin
    Move(FirstTimestampNs, Result[Pos], 8);
    Inc(Pos, 8);
    Move(SampleRate, Result[Pos], 8);
    Inc(Pos, 8);
  end;

  if N > 1 then
  begin
    NVal := N;
    Move(NVal, Result[Pos], 4);
    Inc(Pos, 4);
  end;

  Move(Samples[0], Result[Pos], N * SizeOf(Double));
end;

// Builds the binary payload of an absolute-timestamped data block.
// Layout: [ctrl 1B] [uint32 N 4B if N>1] [int64 ts | bytes]*
//
// For variable-length data types (string, binary) the trailing 0x00 byte is
// version-deterministic per spec rev 2026-05-24:
//   OSF4: byte is appended (writer MUST) and is included in the block-
//         level length.
//   OSF5: byte is NOT appended; the payload ends at the last data byte.
// Multi-sample variable-length blocks have no standard wire format and are
// not produced here — the caller (WriteTimestampedBlock) splits such calls
// into N single-sample blocks before reaching this function. The Assert
// at the top guards against direct internal misuse.
function EncodeTimestampedPayload(AVersion: TOSFVersion; IsVariableLength: Boolean; const Timestamps: array of Int64; const Values: array of TBytes): TBytes;
const
  ZERO_BYTE: Byte = 0;
var
  N: Integer;
  IsMulti: Boolean;
  AppendTerminator: Boolean;
  CtrlByte: Byte;
  Ms: TMemoryStream;
  I: Integer;
  Cnt: UInt32;
  TS: Int64;
  ValueLen: Integer;
begin
  N := Length(Timestamps);
  IsMulti := N > 1;
  Assert(not (IsVariableLength and IsMulti),
    'EncodeTimestampedPayload: multi-sample variable-length blocks are not ' +
    'spec — WriteTimestampedBlock must split before reaching this function');
  AppendTerminator := IsVariableLength and (AVersion = osvOSF4);
  CtrlByte := OSFMakeControlByte(bcAbsTimeStampData, IsMulti);

  Ms := TMemoryStream.Create;
  try
    Ms.WriteBuffer(CtrlByte, 1);
    if IsMulti then
    begin
      Cnt := N;
      Ms.WriteBuffer(Cnt, SizeOf(Cnt));
    end;

    for I := 0 to N - 1 do
    begin
      TS := Timestamps[I];
      Ms.WriteBuffer(TS, SizeOf(TS));
      ValueLen := Length(Values[I]);
      if ValueLen > 0 then
        Ms.WriteBuffer(Values[I][0], ValueLen);
      if AppendTerminator then
        Ms.WriteBuffer(ZERO_BYTE, 1);
    end;

    SetLength(Result, Ms.Size);
    if Ms.Size > 0 then
    begin
      Ms.Position := 0;
      Ms.ReadBuffer(Result[0], Ms.Size);
    end;
  finally
    Ms.Free;
  end;
end;

// (Logging now goes through the global Logger — see OSF.Log unit.)

// ── TOSFFile - channel filter ────────────────────────────────────────────────

procedure TOSFFile.SetChannelFilter(const Value: TArray<string>);
begin
  FChannelFilter := Value;
  RebuildChannelFilterMap;
end;

procedure TOSFFile.RebuildChannelFilterMap;
var
  AllowedSet: TDictionary<string, Boolean>;
  I: Integer;
  Ch: TOSFChannelDef;
  LowerName: string;
begin
  FChannelIncluded.Clear;
  if Length(FChannelFilter) = 0 then
    Exit;
  // Build a case-insensitive set of allowed names once, then map every known
  // channel index to True (included) or False (excluded). Lookup at read
  // time becomes a single TryGetValue.
  AllowedSet := TDictionary<string, Boolean>.Create;
  try
    for I := 0 to High(FChannelFilter) do
      AllowedSet.AddOrSetValue(LowerCase(FChannelFilter[I]), True);
    for I := 0 to FChannels.Count - 1 do
    begin
      Ch := FChannels[I];
      LowerName := LowerCase(Ch.Name);
      FChannelIncluded.AddOrSetValue(Word(Ch.Index), AllowedSet.ContainsKey(LowerName));
    end;
  finally
    AllowedSet.Free;
  end;
end;

function TOSFFile.IsChannelExcluded(ChannelIndex: Word): Boolean;
var
  Included: Boolean;
begin
  // Info blocks (channel index $FFFF) bypass the filter - they carry global
  // metadata, not channel data.
  if ChannelIndex = OSF_INFO_CHANNEL_INDEX then
    Exit(False);
  if Length(FChannelFilter) = 0 then
    Exit(False);
  if not FChannelIncluded.TryGetValue(ChannelIndex, Included) then
    // Unknown channel - leave decision to the caller. Existing logic logs
    // and stops the scan; the filter does not pre-empt that.
    Exit(False);
  Result := not Included;
end;

procedure TOSFFile.SkipExcludedBlock(Channel: TOSFChannelDef);
var
  LenField: UInt32;
  Sink: TBytes;
begin
  // Channel must be known here - IsChannelExcluded only returns True for
  // indices present in the metablock, so we have a valid LengthFieldSize.
  case Channel.LengthFieldSize of
    lfs2:
      LenField := ReadUInt16;
    lfs4:
      LenField := ReadUInt32;
  else
    LenField := 0;
  end;
  if LenField = 0 then
    Exit;
  SetLength(Sink, LenField);
  FStream.ReadBuffer(Sink[0], LenField);
  Logger.Write(SOSFLogChannelFilterSkip, [Channel.Name, LenField], llDebug, 'TOSFFile');
end;

// ── TOSFFile - construction / lifecycle ───────────────────────────────────────

constructor TOSFFile.Create;
begin
  inherited Create;
  FChannels := TObjectList<TOSFChannelDef>.Create(True);
  FInfoItems := TList<TOSFMetaItem>.Create;
  FChannelIncluded := TDictionary<Word, Boolean>.Create;
  FMode := fmClosed;
  FVersion := osvUnknown;
  FHeaderWritten := False;
end;

destructor TOSFFile.Destroy;
begin
  Close;
  FChannelIncluded.Free;
  FInfoItems.Free;
  FChannels.Free;
  inherited;
end;

procedure TOSFFile.Close;
var
  TotalBytes: Int64;
  SourceStr: string;
begin
  if FMode = fmClosed then
    Exit;
  TotalBytes := 0;
  if Assigned(FStream) then
    TotalBytes := FStream.Position;
  if FSourceName <> '' then
    SourceStr := FSourceName
  else
    SourceStr := '<stream>';
  try
    if FOwnsStream then
      FreeAndNil(FStream)
    else
      FStream := nil;
    // Free the underlying file stream after the decompressor that reads
    // from it. Always owned by us when set (only the file overload of
    // OpenForRead populates this field for OSFZ inputs).
    if Assigned(FUnderlyingStream) then
      FreeAndNil(FUnderlyingStream);
  finally
    FMode := fmClosed;
  end;
  // Pre-format with Format() so we exercise the single-string Log overload -
  // the array-of-const overload is exercised by every other call site.
  Logger.Write(SOSFLogFileClosed, [SourceStr, TotalBytes], llInfo, 'TOSFFile');
end;

function TOSFFile.GetChannelCount: Integer;
begin
  Result := FChannels.Count;
end;

function TOSFFile.GetInfoItemCount: Integer;
begin
  Result := FInfoItems.Count;
end;

class function TOSFFile.PeekMagic(AStream: TStream; out AVersion: TOSFVersion): Boolean;
var
  SavePos: Int64;
  Line: AnsiString;
  Parts: TArray<string>;
begin
  Result := False;
  AVersion := osvUnknown;
  if not Assigned(AStream) then
    Exit;

  SavePos := AStream.Position;
  try
    Line := ReadAsciiLine(AStream);
    Parts := string(Line).Split([' ']);
    if Length(Parts) >= 1 then
    begin
      AVersion := OSFVersionFromMagic(Parts[0]);
      Result := AVersion <> osvUnknown;
    end;
  finally
    AStream.Position := SavePos;
  end;
end;

// ── Stream primitives ────────────────────────────────────────────────────────

function TOSFFile.ReadUInt16: Word;
begin
  FStream.ReadBuffer(Result, SizeOf(Result));
end;

function TOSFFile.ReadUInt32: UInt32;
begin
  FStream.ReadBuffer(Result, SizeOf(Result));
end;

procedure TOSFFile.WriteUInt16(Value: Word);
begin
  FStream.WriteBuffer(Value, SizeOf(Value));
end;

procedure TOSFFile.WriteUInt32(Value: UInt32);
begin
  FStream.WriteBuffer(Value, SizeOf(Value));
end;

procedure TOSFFile.WriteRawBytes(const Data: TBytes);
begin
  if Length(Data) > 0 then
    FStream.WriteBuffer(Data[0], Length(Data));
end;

// ── Reading: open + magic header dispatch ────────────────────────────────────

procedure TOSFFile.OpenForRead(const FileName: string);
var
  FS: TFileStream;
begin
  FS := TFileStream.Create(FileName, fmOpenRead or fmShareDenyWrite);
  try
    FSourceName := FileName;
    Logger.Write(SOSFLogOpeningFile, [FileName, FS.Size], llInfo, 'TOSFFile');
  except
    FS.Free;
    raise;
  end;
  // The stream overload handles transparent OSFZ (gzip) detection now,
  // so we just hand it the file stream and let it decide.
  OpenForRead(FS, True);
end;

procedure TOSFFile.OpenForRead(AStream: TStream; AOwnsStream: Boolean);
var
  I: Integer;
  Ch: TOSFChannelDef;
  Magic: array[0..1] of Byte;
  SavedPos: Int64;
  IsGzip: Boolean;
begin
  if FMode <> fmClosed then
    raise EOSFException.Create(SOSFFileAlreadyOpen);

  FTruncationSeen := False;

  // Detect gzip on any seekable stream so callers handing us a raw
  // TFileStream (TOSFDataManager.LoadFromFile, the merger's per-file
  // loader, …) still get transparent OSFZ support. Non-seekable streams
  // (e.g. a network pipe) are trusted as-is.
  IsGzip := False;
  if (AStream <> nil) and (AStream.Size >= 2) then
  begin
    try
      SavedPos := AStream.Position;
      AStream.ReadBuffer(Magic, 2);
      AStream.Position := SavedPos;
      IsGzip := (Magic[0] = $1F) and (Magic[1] = $8B);
    except
      // Stream that doesn't support seek - fall through as plain OSF.
      IsGzip := False;
    end;
  end;

  if IsGzip then
  begin
    if FSourceName <> '' then
      Logger.Write(SOSFLogOSFZDetected, [FSourceName], llInfo, 'TOSFFile')
    else
      Logger.Write(SOSFLogOSFZDetected, ['<stream>'], llInfo, 'TOSFFile');
    // FUnderlyingStream adopts AStream only if the caller asked us to.
    // The decompressor is always owned by us - it's our own construction.
    if AOwnsStream then
      FUnderlyingStream := AStream
    else
      FUnderlyingStream := nil;
    FStream := TZDecompressionStream.Create(AStream, 15 + 32);
    FOwnsStream := True;
  end
  else
  begin
    FStream := AStream;
    FOwnsStream := AOwnsStream;
  end;

  FMode := fmRead;
  FChannels.Clear;
  FInfoItems.Clear;
  ReadMagicAndMeta;
  // Channel names only become available after the metablock is parsed.
  // Re-resolve any pre-set ChannelFilter against the freshly populated
  // channel list so ReadNextBlock filtering kicks in immediately.
  RebuildChannelFilterMap;

  Logger.Write(SOSFLogDetectedVersion, [VersionToLogString(FVersion), MetaFormatToLogString(FMetaFormat)], llInfo, 'TOSFFile');
  Logger.Write(SOSFLogChannelsDefined, [FChannels.Count], llInfo, 'TOSFFile');
  for I := 0 to FChannels.Count - 1 do
  begin
    Ch := FChannels[I];
    Logger.Write(SOSFLogChannelEntry, [Ch.Index, Ch.Name, OSFDataTypeToString(Ch.DataType), BoolToLogString(Ch.IsEquidistant)], llDebug, 'TOSFFile');
  end;
end;

procedure TOSFFile.ParseHeaderTokens(const Parts: TArray<string>);

  function IsHex(const S: string; Len: Integer; Upper: Boolean): Boolean;
  var
    C: Char;
  begin
    Result := Length(S) = Len;
    if Result then
      for C in S do
        if not (CharInSet(C, ['0'..'9']) or
                (Upper and CharInSet(C, ['A'..'F'])) or
                ((not Upper) and CharInSet(C, ['a'..'f']))) then
          Exit(False);
  end;

var
  I, ColonPos: Integer;
  Token, Key, Value: string;
  SawCrc: Boolean;
begin
  SawCrc := False;
  // Parts[0] = identifier, Parts[1] = metablock length; the rest are tokens.
  for I := 2 to High(Parts) do
  begin
    Token := Parts[I];
    // An empty field means a double or trailing space — the spec requires
    // exactly one space between fields and no trailing space (Fix 1).
    if Token = '' then
      raise EOSFFormatError.Create(SOSFMalformedHeaderLine);
    // Header tokens are an OSF5-only feature.
    if FVersion <> osvOSF5 then
      raise EOSFFormatError.CreateFmt(SOSFTokenNotAllowedOSF4, [Token]);
    ColonPos := Pos(':', Token);
    if ColonPos < 2 then
      raise EOSFFormatError.CreateFmt(SOSFUnknownHeaderToken, [Token]);
    Key := Copy(Token, 1, ColonPos - 1);
    Value := Copy(Token, ColonPos + 1, MaxInt);
    if Key = 'crc32c' then
    begin
      if not IsHex(Value, 8, True) then
        raise EOSFFormatError.CreateFmt(SOSFInvalidCrc32cToken, [Value]);
      FMetablockCRC := UInt32(StrToInt64('$' + Value));
      FIntegrity := ipCrc32c;
      SawCrc := True;
    end
    else if Key = 'ed25519' then
    begin
      if not IsHex(Value, 16, False) then
        raise EOSFFormatError.CreateFmt(SOSFInvalidEd25519Token, [Value]);
      if not SawCrc then
        raise EOSFFormatError.Create(SOSFEd25519WithoutCrc);
      FEd25519KeyId := Value;
      FIntegrity := ipEd25519;
    end
    else
      raise EOSFFormatError.CreateFmt(SOSFUnknownHeaderToken, [Key]);
  end;
end;

procedure TOSFFile.ReadMagicAndMeta;
var
  Line: AnsiString;
  Parts: TArray<string>;
  MetaSize: Int64;
  MetaBytes: TBytes;
  FirstByte: Byte;
begin
  // Reset read-side integrity state on every open.
  FIntegrity := ipNone;
  FMetablockCRC := 0;
  FEd25519KeyId := '';
  FBlocksCRCFailed := 0;
  FBlocksUnknownTypeSkipped := 0;
  FBlocksSignatureSkipped := 0;
  FBlocksZeroLengthSkipped := 0;

  Line := ReadAsciiLine(FStream);
  if Length(Line) = 0 then
    raise EOSFFormatError.Create(SOSFUnexpectedEOFInHeader);

  Parts := string(Line).Split([' ']);
  if Length(Parts) < 2 then
    raise EOSFFormatError.CreateFmt(SOSFInvalidMagicHeader, [string(Line)]);

  FVersion := OSFVersionFromMagic(Parts[0]);
  if FVersion = osvUnknown then
    raise EOSFVersionError.CreateFmt(SOSFUnsupportedMagicToken, [Parts[0]]);

  MetaSize := StrToInt64Def(Parts[1], -1);
  if MetaSize <= 0 then
    raise EOSFFormatError.CreateFmt(SOSFInvalidMetaBlockSize, [Parts[1]]);

  // Integrity profile: parse any header tokens after the length field.
  // Unknown tokens ("must understand") reject the file.
  ParseHeaderTokens(Parts);

  SetLength(MetaBytes, MetaSize);
  FStream.ReadBuffer(MetaBytes[0], MetaSize);

  // Metablock CRC (integrity level crc): verify the raw metablock bytes
  // against the crc32c token before parsing. Mismatch rejects the file.
  if (FIntegrity >= ipCrc32c) then
    if CRC32C(MetaBytes[0], MetaSize) <> FMetablockCRC then
      raise EOSFFormatError.CreateFmt(SOSFMetablockCrcMismatch,
        [FMetablockCRC, CRC32C(MetaBytes[0], MetaSize)]);

  // Dispatch on the first byte: '<' = XML (OSF4), '{' = JSON (OSF5).
  FirstByte := MetaBytes[0];
  case Chr(FirstByte) of
    '<':
      FMetaFormat := mfXML;
    '{':
      FMetaFormat := mfJSON;
  else
    raise EOSFFormatError.CreateFmt(SOSFUnknownMetaFormat, [FirstByte]);
  end;

  case FMetaFormat of
    mfXML:
      ParseXMLMeta(MetaBytes);
    mfJSON:
      ParseJSONMeta(MetaBytes);
  end;
end;

// ── Reading: JSON meta block ─────────────────────────────────────────────────

procedure TOSFFile.ParseJSONMeta(const Data: TBytes);
var
  JSONText: string;
  Root: TJSONObject;
  OSFNode: TJSONObject;
  FileNode: TJSONObject;
begin
  JSONText := TEncoding.UTF8.GetString(Data);
  Root := TJSONObject.ParseJSONValue(JSONText) as TJSONObject;
  if not Assigned(Root) then
    raise EOSFFormatError.Create(SOSFParseJSONMetaFailed);
  try
    OSFNode := Root.GetValue('osf') as TJSONObject;
    if not Assigned(OSFNode) then
      raise EOSFFormatError.Create(SOSFMissingOSFKey);

    // Newer files put file metadata in a "file" sub-object; older files put
    // it directly under "osf". Support both.
    FileNode := OSFNode.GetValue('file') as TJSONObject;
    if not Assigned(FileNode) then
      FileNode := OSFNode;

    ParseJSONFileMetadata(FileNode);
    ParseJSONChannels(OSFNode);
    ParseJSONInfo(OSFNode);
  finally
    Root.Free;
  end;
end;

procedure TOSFFile.ParseJSONFileMetadata(FileNode: TJSONObject);
begin
  FMetadata.CreatedUtc := ParseISO8601DateTime(JStr(FileNode, 'created_utc', ''));
  FMetadata.Creator := JStr(FileNode, 'creator', '');
  FMetadata.Tag := JStr(FileNode, 'tag', '');
  FMetadata.Reason := JStr(FileNode, 'reason', '');
  FMetadata.Comment := JStr(FileNode, 'comment', '');
  FMetadata.NamespaceSep := JStr(FileNode, 'namespacesep', OSF_DEFAULT_NAMESPACE_SEP);
  FMetadata.Longitude := JDbl(FileNode, 'created_at_longitude', 0.0);
  FMetadata.Latitude := JDbl(FileNode, 'created_at_latitude', 0.0);
  FMetadata.Altitude := JDbl(FileNode, 'created_at_altitude', 0.0);
end;

procedure TOSFFile.ParseJSONChannels(OSFNode: TJSONObject);
var
  ChanArr: TJSONArray;
  I: Integer;
begin
  ChanArr := OSFNode.GetValue('channels') as TJSONArray;
  if not Assigned(ChanArr) then
    Exit;
  for I := 0 to ChanArr.Count - 1 do
    FChannels.Add(TOSFChannelDef.FromJSONObject(ChanArr.Items[I] as TJSONObject));
end;

procedure TOSFFile.ParseJSONInfo(OSFNode: TJSONObject);
var
  InfoArr: TJSONArray;
  InfoObj: TJSONObject;
  Item: TOSFMetaItem;
  I: Integer;
begin
  InfoArr := OSFNode.GetValue('info') as TJSONArray;
  if not Assigned(InfoArr) then
    Exit;
  for I := 0 to InfoArr.Count - 1 do
  begin
    InfoObj := InfoArr.Items[I] as TJSONObject;
    Item.Name := JStr(InfoObj, 'name', '');
    Item.Value := JStr(InfoObj, 'value', '');
    Item.DataType := JStr(InfoObj, 'datatype', 'string');
    Item.UnitStr := JStr(InfoObj, 'unit', '');
    FInfoItems.Add(Item);
  end;
end;

// ── Reading: XML meta block ──────────────────────────────────────────────────

procedure TOSFFile.ParseXMLMeta(const Data: TBytes);
var
  XMLDoc: IXMLDocument;
  Doc: TXMLDocument;
  XMLText: string;
  RootNode: IXMLNode;
begin
  XMLText := TEncoding.UTF8.GetString(Data);
  Doc := TXMLDocument.Create(nil);
  // Pin the DOM vendor to OmniXML (pure Pascal). The default on Windows
  // is MSXML, which is unavailable on stripped-down hosts and raises
  // EOleException 'Microsoft MSXML ist nicht installiert' before the
  // first byte of XML is parsed. Routing through OmniXML keeps OSF4
  // metablock reads working everywhere this binary deploys.
  Doc.DOMVendor := GetDOMVendor(sOmniXmlVendor);
  XMLDoc := Doc;
  XMLDoc.LoadFromXML(XMLText);
  XMLDoc.Active := True;

  RootNode := XMLDoc.DocumentElement;
  if not Assigned(RootNode) then
    raise EOSFFormatError.Create(SOSFXMLNoRootElement);

  ParseXMLRootAttributes(RootNode);
  ParseXMLChannels(RootNode);
  ParseXMLInfos(RootNode);
end;

procedure TOSFFile.ParseXMLRootAttributes(RootNode: IXMLNode);
begin
  // Works for either <optimeas> (OSF4) or <osf> (synthetic) - only attributes.
  if RootNode.HasAttribute('creator') then
    FMetadata.Creator := RootNode.Attributes['creator'];
  if RootNode.HasAttribute('tag') then
    FMetadata.Tag := RootNode.Attributes['tag'];
  if RootNode.HasAttribute('reason') then
    FMetadata.Reason := RootNode.Attributes['reason'];
  if RootNode.HasAttribute('comment') then
    FMetadata.Comment := RootNode.Attributes['comment'];
  if RootNode.HasAttribute('created_utc') then
    FMetadata.CreatedUtc := ParseISO8601DateTime(RootNode.Attributes['created_utc']);
  if RootNode.HasAttribute('namespacesep') then
    FMetadata.NamespaceSep := RootNode.Attributes['namespacesep']
  else
    FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;
  FMetadata.Longitude := XMLAttrDoubleLocal(RootNode, 'longitude', 0.0);
  FMetadata.Latitude := XMLAttrDoubleLocal(RootNode, 'latitude', 0.0);
  FMetadata.Altitude := XMLAttrDoubleLocal(RootNode, 'altitude', 0.0);
end;

procedure TOSFFile.ParseXMLChannels(RootNode: IXMLNode);
var
  ChansNode: IXMLNode;
  Node: IXMLNode;
  I: Integer;
begin
  ChansNode := RootNode.ChildNodes.FindNode('channels');
  if not Assigned(ChansNode) then
    Exit;
  for I := 0 to ChansNode.ChildNodes.Count - 1 do
  begin
    Node := ChansNode.ChildNodes[I];
    if Node.NodeName = 'channel' then
      FChannels.Add(TOSFChannelDef.FromXMLNode(Node));
  end;
end;

procedure TOSFFile.ParseXMLInfos(RootNode: IXMLNode);
var
  InfosNode: IXMLNode;
  Node: IXMLNode;
  Item: TOSFMetaItem;
  I: Integer;
begin
  InfosNode := RootNode.ChildNodes.FindNode('infos');
  if not Assigned(InfosNode) then
    Exit;
  for I := 0 to InfosNode.ChildNodes.Count - 1 do
  begin
    Node := InfosNode.ChildNodes[I];
    if Node.NodeName = 'info' then
    begin
      Item.Name := XMLAttrStrLocal(Node, 'name', '');
      Item.Value := XMLAttrStrLocal(Node, 'value', '');
      Item.DataType := XMLAttrStrLocal(Node, 'datatype', 'string');
      Item.UnitStr := XMLAttrStrLocal(Node, 'unit', '');
      FInfoItems.Add(Item);
    end;
  end;
end;

// ── Reading: data blocks ─────────────────────────────────────────────────────

function TOSFFile.SkipExcludedBlocksUntilIncluded(out ChannelIndex: Word; out StartOffset: Int64): Boolean;
var
  ExcludedChannel: TOSFChannelDef;
begin
  repeat
    StartOffset := 0;
    if Assigned(FStream) then
      StartOffset := FStream.Position;

    if not TryReadChannelIndex(ChannelIndex) then
      Exit(False);

    if not IsChannelExcluded(ChannelIndex) then
      Exit(True);

    ExcludedChannel := FindChannel(ChannelIndex);
    if not Assigned(ExcludedChannel) then
      Exit(False); // excluded but unknown - no LengthFieldSize to skip with
    try
      SkipExcludedBlock(ExcludedChannel);
    except
      on EReadError do
        Exit(False); // truncated mid-block while skipping
    end;
  until False;
end;

function TOSFFile.ReadNextBlock(out Block: TOSFDataBlock): Boolean;
var
  ChannelIndex: Word;
  StartOffset: Int64;
  Outcome: TOSFBlockOutcome;
begin
  // Loop so that a skipped block (unknown control byte, failed frame CRC, or
  // a signature block) advances to the next block instead of ending the scan.
  // Only a real truncation stops the reader (Fix C — audit §4.2).
  repeat
    // Default() initialises managed fields (TBytes) without corrupting refcounts.
    Block := Default (TOSFDataBlock);

    if not SkipExcludedBlocksUntilIncluded(ChannelIndex, StartOffset) then
      Exit(False);
    Block.ChannelIndex := ChannelIndex;

    if ChannelIndex = OSF_INFO_CHANNEL_INDEX then
      Outcome := ReadInfoBlock(Block)
    else if (FIntegrity <> ipNone) and (ChannelIndex = OSF_SIGNATURE_CHANNEL_INDEX) then
      Outcome := ReadSignatureBlock
    else
      Outcome := ReadDataBlock(ChannelIndex, Block);

    case Outcome of
      boBlock:
        begin
          if not Block.IsInfoBlock then
            Logger.Write(SOSFLogBlockRead,
              [ChannelIndex, BlockTypeToLogString(Block.BlockType), Block.SampleCount,
               Length(Block.RawPayload)], llDebug, 'TOSFFile');
          Exit(True);
        end;
      boSkip:
        Continue; // bytes already consumed; read the next block
      boStop:
        begin
          if not Block.IsInfoBlock then
          begin
            FTruncationSeen := True;
            Logger.Write(SOSFLogTruncatedBlock, [StartOffset], llWarning, 'TOSFFile');
          end;
          Exit(False);
        end;
    end;
  until False;
end;

function TOSFFile.TryReadChannelIndex(out ChannelIndex: Word): Boolean;
var
  BytesRead: Integer;
begin
  // A zero-byte read here is clean EOF; a short read is mid-header truncation.
  // Either case: best-effort stop without raising.
  BytesRead := FStream.Read(ChannelIndex, SizeOf(ChannelIndex));
  Result := BytesRead = SizeOf(ChannelIndex);
end;

function TOSFFile.ReadInfoBlock(var Block: TOSFDataBlock): TOSFBlockOutcome;
var
  LenField: UInt32;
  Payload: TBytes;
  TypeBits: Byte;
begin
  // The info/trailer block always uses a uint32 length field (spec §$FFFF).
  Block.IsInfoBlock := True;
  try
    LenField := ReadUInt32;
    SetLength(Payload, LenField);
    if LenField > 0 then
      FStream.ReadBuffer(Payload[0], LenField);
  except
    on EReadError do
      Exit(boStop);
  end;
  if LenField > 0 then
  begin
    TypeBits := Payload[0] and OSF_BLOCK_TYPE_MASK;
    if Integer(TypeBits) > Ord(bcAbsTimeStampData) then
    begin
      Logger.Write(SOSFLogUnknownBlockTypeInfo, [TypeBits], llWarning, 'TOSFFile');
      Exit(boStop);
    end;
    Block.BlockType := TBlockContent(TypeBits);
    SetLength(Block.RawPayload, LenField - 1);
    if LenField > 1 then
      Move(Payload[1], Block.RawPayload[0], LenField - 1);
  end;
  Result := boBlock;
end;

function TOSFFile.ReadSignatureBlock: TOSFBlockOutcome;
var
  LenField: UInt32;
  Skip: TBytes;
begin
  // Signature blocks (channel 0xFFFE, control byte 9) always use a u32 length
  // field (spec clarification, analogous to the 0xFFFF info block). This reader
  // implements level crc, not level signed, so the block is skipped via its
  // length field and counted; a signed file therefore stays readable.
  try
    LenField := ReadUInt32;
    SetLength(Skip, LenField);
    if LenField > 0 then
      FStream.ReadBuffer(Skip[0], LenField);
  except
    on EReadError do
      Exit(boStop);
  end;
  Inc(FBlocksSignatureSkipped);
  Logger.Write(SOSFLogSignatureBlockSkipped, [], llInfo, 'TOSFFile');
  Result := boSkip;
end;

function TOSFFile.ReadDataBlock(ChannelIndex: Word; var Block: TOSFDataBlock): TOSFBlockOutcome;
var
  Channel: TOSFChannelDef;
  LenField: UInt32;
  Payload: TBytes;
  Offset: Int64;
begin
  // Channel must have been declared in the meta block; otherwise we cannot
  // know the length-field width and have to stop the best-effort scan here.
  Channel := FindChannel(ChannelIndex);
  if not Assigned(Channel) then
  begin
    Logger.Write(SOSFLogUnknownChannelInBlock, [ChannelIndex], llWarning, 'TOSFFile');
    Exit(boStop);
  end;

  Offset := 0;
  if Assigned(FStream) then
    Offset := FStream.Position;

  // An unrecognised length-field width is a corruption / unsupported-format
  // signal, NOT a zero-length block: without the width we cannot tell where
  // the frame ends, so the next frame boundary is unknowable and the
  // best-effort scan has to stop here - exactly like an unknown channel index
  // above. Checked before the length field is read so that this fault can
  // never be absorbed by the zero-length skip path below (OSF-UP3 applies to
  // a length field literally read as 0 from the stream).
  case Channel.LengthFieldSize of
    lfs2, lfs4: ;
  else
    Logger.Write(SOSFUnknownLengthFieldSize, [ChannelIndex], llWarning, 'TOSFFile');
    Exit(boStop);
  end;

  try
    case Channel.LengthFieldSize of
      lfs2:
        LenField := ReadUInt16;
    else
      LenField := ReadUInt32;
    end;
    // OSF-UP3: a length field of 0 is a non-conforming writer artefact - a
    // conforming block always carries at least its control byte. The frame is
    // only the channel index and the length field, both already consumed, so
    // the block is logged, counted and skipped; the scan continues with the
    // next block and this is NOT reported as a truncation. The Exit must come
    // before SetLength/ReadBuffer: reading Payload[0] of an empty dynamic
    // array raises ERangeError under {$R+}. This test also precedes the frame
    // CRC length test below, as the spec mandates.
    if LenField = 0 then
    begin
      Logger.Write(SOSFLogZeroLengthBlockSkip, [ChannelIndex, Offset], llWarning, 'TOSFFile');
      Inc(FBlocksZeroLengthSkipped);
      Exit(boSkip);
    end;
    SetLength(Payload, LenField);
    FStream.ReadBuffer(Payload[0], LenField);
  except
    on EReadError do
      Exit(boStop); // truncated mid-block - best-effort stop
  end;

  // Frame CRC (integrity level crc): verify the trailing 4 bytes over the
  // whole frame and strip them BEFORE the typed decode (fail-closed framing;
  // the CRC is counted in the length field, so effective length = LEN - 4).
  if FIntegrity <> ipNone then
  begin
    if LenField < 5 then
    begin
      Logger.Write(SOSFLogFrameCRCTooShort, [ChannelIndex], llWarning, 'TOSFFile');
      Inc(FBlocksCRCFailed);
      Exit(boSkip);
    end;
    if not VerifyFrameCRC(ChannelIndex, Channel.LengthFieldSize, LenField, Payload) then
    begin
      Logger.Write(SOSFLogFrameCRCMismatch, [ChannelIndex, Offset], llWarning, 'TOSFFile');
      Inc(FBlocksCRCFailed);
      Exit(boSkip);
    end;
    LenField := LenField - 4;
  end;

  Result := DecodeBlockPayload(Channel, Payload, LenField, Block);
end;

function TOSFFile.VerifyFrameCRC(ChannelIndex: Word; LFS: TOSFLengthFieldSize;
  LenField: UInt32; const Payload: TBytes): Boolean;
var
  Crc: TCRC32C;
  ChanLE: array[0..1] of Byte;
  LenLE: array[0..3] of Byte;
  LenBytes, ContentLen: Integer;
  Stored: UInt32;
begin
  // Stored CRC = last 4 bytes of the data area, little-endian.
  Move(Payload[LenField - 4], Stored, 4);
  ContentLen := Integer(LenField) - 4;
  // Frame scope: channel index (2 LE) + length field (2 or 4 LE) + content.
  ChanLE[0] := Byte(ChannelIndex);
  ChanLE[1] := Byte(ChannelIndex shr 8);
  LenLE[0] := Byte(LenField);
  LenLE[1] := Byte(LenField shr 8);
  LenLE[2] := Byte(LenField shr 16);
  LenLE[3] := Byte(LenField shr 24);
  if LFS = lfs2 then
    LenBytes := 2
  else
    LenBytes := 4;
  Crc.Init;
  Crc.Update(ChanLE[0], 2);
  Crc.Update(LenLE[0], LenBytes);
  if ContentLen > 0 then
    Crc.Update(Payload[0], ContentLen);
  Result := Crc.Final = Stored;
end;

function TOSFFile.DecodeBlockPayload(Channel: TOSFChannelDef; const Payload: TBytes; LenField: UInt32; var Block: TOSFDataBlock): TOSFBlockOutcome;
var
  CtrlByte: Byte;
  TypeBits: Byte;
  Offset: Integer;
  RequiredLen: Integer;
  PayloadSize: Integer;
  SampleCount: UInt32;
  Pos: Int64;
  SampleRate: Double;
begin
  if Length(Payload) < 1 then
    Exit(boStop);

  Pos := 0;
  if Assigned(FStream) then
    Pos := FStream.Position;

  // Decode the block type without going through OSFBlockTypeFromByte so we can
  // skip an unknown type byte instead of letting it raise.
  CtrlByte := Payload[0];
  TypeBits := CtrlByte and OSF_BLOCK_TYPE_MASK;
  if Integer(TypeBits) > Ord(bcAbsTimeStampData) then
  begin
    // Fix C: unknown control byte — skip via the length field (bytes already
    // consumed) and continue, rather than aborting the scan as a truncation.
    Logger.Write(SOSFLogUnknownBlockTypeSkip, [TypeBits, Block.ChannelIndex, Pos],
      llWarning, 'TOSFFile');
    Inc(FBlocksUnknownTypeSkipped);
    Exit(boSkip);
  end;
  Block.BlockType := TBlockContent(TypeBits);
  Block.MultiValue := OSFBlockHasMultipleValues(CtrlByte);

  // Pre-validate the encoded prefix length so the body needs no further checks.
  // bcStartData carries 8 bytes (int64 timestamp) + 8 bytes (double SampleRate).
  RequiredLen := 1;
  if Block.BlockType = bcStartData then
    Inc(RequiredLen, 16);
  if Block.MultiValue then
    Inc(RequiredLen, 4);
  if Integer(LenField) < RequiredLen then
    Exit(boStop);

  Offset := 1;
  if Block.BlockType = bcStartData then
  begin
    Move(Payload[Offset], Block.StartTimestampNs, 8);
    Inc(Offset, 8);
    Channel.LastTimestampNs := Block.StartTimestampNs;
    Move(Payload[Offset], SampleRate, 8);
    Inc(Offset, 8);
    Block.SampleRate := SampleRate;
    Channel.SampleRate := SampleRate;
  end;

  if Block.MultiValue then
  begin
    Move(Payload[Offset], SampleCount, 4);
    Inc(Offset, 4);
    Block.SampleCount := SampleCount;
  end
  else
    Block.SampleCount := 1;

  PayloadSize := Integer(LenField) - Offset;
  if PayloadSize > 0 then
  begin
    SetLength(Block.RawPayload, PayloadSize);
    Move(Payload[Offset], Block.RawPayload[0], PayloadSize);
  end;

  Channel.SampleCount := Channel.SampleCount + Block.SampleCount;
  Result := boBlock;
end;

function TOSFFile.VerificationStatus: string;
begin
  case FIntegrity of
    ipCrc32c:
      if FBlocksCRCFailed > 0 then
        Result := 'invalid'
      else
        Result := 'crc_valid';
    ipEd25519:
      Result := 'signature_unverifiable';
  else
    Result := 'none';
  end;
end;

// ── Writing: open + structural setup ─────────────────────────────────────────

procedure TOSFFile.CreateForWrite(const FileName: string; AVersion: TOSFVersion);
begin
  FSourceName := FileName;
  CreateForWrite(TFileStream.Create(FileName, fmCreate), True, AVersion);
end;

procedure TOSFFile.CreateForWrite(AStream: TStream; AOwnsStream: Boolean; AVersion: TOSFVersion);
begin
  if FMode <> fmClosed then
    raise EOSFException.Create(SOSFFileAlreadyOpen);
  if not(AVersion in [osvOSF4, osvOSF5]) then
    raise EOSFException.Create(SOSFCreateForWriteBadVersion);

  FStream := AStream;
  FOwnsStream := AOwnsStream;
  FMode := fmWrite;
  FVersion := AVersion;
  case AVersion of
    osvOSF4:
      FMetaFormat := mfXML;
    osvOSF5:
      FMetaFormat := mfJSON;
  end;
  FHeaderWritten := False;
  FChannels.Clear;
  FInfoItems.Clear;
end;

function TOSFFile.AddChannel(Def: TOSFChannelDef): Integer;
var
  I: Integer;
begin
  if FHeaderWritten then
    raise EOSFException.Create(SOSFAddChannelAfterHeader);

  // A duplicate index produces an unreadable file (the reader can't tell the
  // two channels apart). A duplicate name is permitted by the spec but almost
  // always indicates a caller bug, so we reject it as well.
  for I := 0 to FChannels.Count - 1 do
  begin
    if FChannels[I].Index = Def.Index then
      raise EOSFException.CreateFmt(SOSFDuplicateChannelIndex, [Def.Index]);
    if (Def.Name <> '') and (FChannels[I].Name = Def.Name) then
      raise EOSFException.CreateFmt(SOSFDuplicateChannelName, [Def.Name]);
  end;

  FChannels.Add(Def);
  Result := Def.Index;
end;

procedure TOSFFile.AddInfoItem(const AName, AValue, ADataType, AUnit: string);
var
  Item: TOSFMetaItem;
begin
  if FHeaderWritten then
    raise EOSFException.Create(SOSFAddInfoItemAfterHeader);
  Item.Name := AName;
  Item.Value := AValue;
  Item.DataType := ADataType;
  Item.UnitStr := AUnit;
  FInfoItems.Add(Item);
end;

// ── Writing: JSON meta block ─────────────────────────────────────────────────

function TOSFFile.BuildJSONMeta: TBytes;
var
  Root: TJSONObject;
  OSFNode: TJSONObject;
begin
  // Build top-down so each freshly created object becomes owned by its parent
  // before the next allocation. Freeing Root cascades through every node.
  Root := TJSONObject.Create;
  try
    OSFNode := TJSONObject.Create;
    Root.AddPair('osf', OSFNode);
    OSFNode.AddPair('format', OSF_FORMAT_OSF5);
    OSFNode.AddPair('version', TJSONNumber.Create(5));

    AppendJSONFileNode(OSFNode);
    AppendJSONChannels(OSFNode);
    AppendJSONInfoArray(OSFNode);

    Result := TEncoding.UTF8.GetBytes(Root.ToJSON);
  finally
    Root.Free;
  end;
end;

procedure TOSFFile.AppendJSONFileNode(Parent: TJSONObject);
var
  FileNode: TJSONObject;
begin
  FileNode := TJSONObject.Create;
  Parent.AddPair('file', FileNode);
  FileNode.AddPair('created_utc', FormatUTCDateTime(FMetadata.CreatedUtc));
  if FMetadata.Creator <> '' then
    FileNode.AddPair('creator', FMetadata.Creator);
  if FMetadata.Tag <> '' then
    FileNode.AddPair('tag', FMetadata.Tag);
  if FMetadata.Reason <> '' then
    FileNode.AddPair('reason', FMetadata.Reason);
  if FMetadata.Comment <> '' then
    FileNode.AddPair('comment', FMetadata.Comment);
  if FMetadata.NamespaceSep <> '' then
    FileNode.AddPair('namespacesep', FMetadata.NamespaceSep);
  if FMetadata.Longitude <> 0 then
    FileNode.AddPair('created_at_longitude', TJSONNumber.Create(FMetadata.Longitude));
  if FMetadata.Latitude <> 0 then
    FileNode.AddPair('created_at_latitude', TJSONNumber.Create(FMetadata.Latitude));
  if FMetadata.Altitude <> 0 then
    FileNode.AddPair('created_at_altitude', TJSONNumber.Create(FMetadata.Altitude));
end;

procedure TOSFFile.AppendJSONChannels(Parent: TJSONObject);
var
  ChanArr: TJSONArray;
  I: Integer;
begin
  ChanArr := TJSONArray.Create;
  Parent.AddPair('channels', ChanArr);
  for I := 0 to FChannels.Count - 1 do
    FChannels[I].AppendJSON(ChanArr);
end;

procedure TOSFFile.AppendJSONInfoArray(Parent: TJSONObject);
var
  InfoArr: TJSONArray;
  InfoObj: TJSONObject;
  Item: TOSFMetaItem;
  I: Integer;
begin
  if FInfoItems.Count = 0 then
    Exit;
  InfoArr := TJSONArray.Create;
  Parent.AddPair('info', InfoArr);
  for I := 0 to FInfoItems.Count - 1 do
  begin
    Item := FInfoItems[I];
    InfoObj := TJSONObject.Create;
    InfoArr.AddElement(InfoObj);
    InfoObj.AddPair('name', Item.Name);
    InfoObj.AddPair('value', Item.Value);
    InfoObj.AddPair('datatype', Item.DataType);
    if Item.UnitStr <> '' then
      InfoObj.AddPair('unit', Item.UnitStr);
  end;
end;

// ── Writing: XML meta block ──────────────────────────────────────────────────

function TOSFFile.BuildXMLMeta: TBytes;
var
  B: TStringBuilder;
begin
  B := TStringBuilder.Create;
  try
    B.Append('<?xml version="1.0" encoding="UTF-8"?>'#10);
    AppendXMLOpenTag(B);
    AppendXMLChannels(B);
    AppendXMLInfos(B);
    B.Append('</optimeas>'#10);
    Result := TEncoding.UTF8.GetBytes(B.ToString);
  finally
    B.Free;
  end;
end;

procedure TOSFFile.AppendXMLOpenTag(B: TStringBuilder);
var
  FS: TFormatSettings;
begin
  FS := TFormatSettings.Invariant;
  B.Append('<optimeas');
  B.AppendFormat(' creator="%s"', [XMLEscape(FMetadata.Creator)]);
  B.AppendFormat(' created_utc="%s"', [FormatUTCDateTime(FMetadata.CreatedUtc)]);
  if FMetadata.Tag <> '' then
    B.AppendFormat(' tag="%s"', [XMLEscape(FMetadata.Tag)]);
  if FMetadata.Reason <> '' then
    B.AppendFormat(' reason="%s"', [XMLEscape(FMetadata.Reason)]);
  if FMetadata.Comment <> '' then
    B.AppendFormat(' comment="%s"', [XMLEscape(FMetadata.Comment)]);
  B.AppendFormat(' namespacesep="%s"', [XMLEscape(FMetadata.NamespaceSep)]);
  if FMetadata.Longitude <> 0 then
    B.AppendFormat(' longitude="%s"', [FloatToStr(FMetadata.Longitude, FS)]);
  if FMetadata.Latitude <> 0 then
    B.AppendFormat(' latitude="%s"', [FloatToStr(FMetadata.Latitude, FS)]);
  if FMetadata.Altitude <> 0 then
    B.AppendFormat(' altitude="%s"', [FloatToStr(FMetadata.Altitude, FS)]);
  B.Append('>'#10);
end;

procedure TOSFFile.AppendXMLChannels(B: TStringBuilder);
var
  I: Integer;
begin
  B.AppendFormat('  <channels count="%d">'#10, [FChannels.Count]);
  for I := 0 to FChannels.Count - 1 do
    FChannels[I].AppendXML(B);
  B.Append('  </channels>'#10);
end;

procedure TOSFFile.AppendXMLInfos(B: TStringBuilder);
var
  I: Integer;
  Item: TOSFMetaItem;
begin
  if FInfoItems.Count = 0 then
    Exit;
  B.Append('  <infos>'#10);
  for I := 0 to FInfoItems.Count - 1 do
  begin
    Item := FInfoItems[I];
    B.Append('    <info');
    B.AppendFormat(' name="%s"', [XMLEscape(Item.Name)]);
    B.AppendFormat(' value="%s"', [XMLEscape(Item.Value)]);
    B.AppendFormat(' datatype="%s"', [XMLEscape(Item.DataType)]);
    if Item.UnitStr <> '' then
      B.AppendFormat(' unit="%s"', [XMLEscape(Item.UnitStr)]);
    B.Append('/>'#10);
  end;
  B.Append('  </infos>'#10);
end;

// ── Writing: header + data blocks ────────────────────────────────────────────

procedure TOSFFile.WriteHeader;
var
  MetaBytes: TBytes;
  HeaderLine: TBytes;
  HeaderStr: string;
  Magic: string;
begin
  if FMode <> fmWrite then
    raise EOSFException.Create(SOSFWriteHeaderNotWriteMode);
  if FHeaderWritten then
    raise EOSFException.Create(SOSFWriteHeaderAlreadyCalled);

  if FMetadata.CreatedUtc = 0 then
    FMetadata.CreatedUtc := TTimeZone.Local.ToUniversalTime(Now);
  if FMetadata.NamespaceSep = '' then
    FMetadata.NamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;

  case FVersion of
    osvOSF4:
      begin
        MetaBytes := BuildXMLMeta;
        Magic := OSF_MAGIC_OSF4;
      end;
    osvOSF5:
      begin
        MetaBytes := BuildJSONMeta;
        Magic := OSF_MAGIC_OSF5;
      end;
  else
    raise EOSFException.Create(SOSFWriteHeaderBadVersion);
  end;

  // Integrity profile is an OSF5-only feature. The writer supports level crc.
  if (FIntegrity <> ipNone) and (FVersion <> osvOSF5) then
    raise EOSFException.Create(SOSFWriterIntegrityOSF5Only);
  if FIntegrity = ipEd25519 then
    raise EOSFException.Create(SOSFWriterSigningUnsupported);

  HeaderStr := Format('%s %d', [Magic, Length(MetaBytes)]);
  if FIntegrity = ipCrc32c then
    // crc32c token: 8 uppercase hex digits of the metablock CRC.
    HeaderStr := HeaderStr + ' crc32c:' + IntToHex(CRC32C(MetaBytes[0], Length(MetaBytes)), 8);
  HeaderStr := HeaderStr + #10;
  HeaderLine := TEncoding.ASCII.GetBytes(HeaderStr);

  FStream.WriteBuffer(HeaderLine[0], Length(HeaderLine));
  FStream.WriteBuffer(MetaBytes[0], Length(MetaBytes));

  FHeaderWritten := True;
  Logger.Write(SOSFLogWritingHeader, [VersionToLogString(FVersion), FChannels.Count], llInfo, 'TOSFFile');
end;

procedure TOSFFile.WriteDataBlock(Channel: TOSFChannelDef; const Payload: TBytes);
var
  Idx: Word;
  OnWire: UInt32;
  Crc: TCRC32C;
  ChanLE: array[0..1] of Byte;
  LenLE: array[0..3] of Byte;
  CrcLE: array[0..3] of Byte;
  LenBytes: Integer;
  CrcVal: UInt32;
begin
  Idx := Word(Channel.Index);
  // At integrity level crc the frame CRC (4 bytes) is appended and counted in
  // the length field. The Delphi writer emits one block per call (no splitting
  // at the length-field boundary), so no chunk reduction is needed.
  if FIntegrity <> ipNone then
    OnWire := UInt32(Length(Payload)) + 4
  else
    OnWire := UInt32(Length(Payload));

  // Guard against a silent u16 length-field wrap-around. The writer does not
  // split blocks, so with the frame CRC (+4) counted in the length field a
  // large payload on an lfs2 channel could overflow — fail loudly instead.
  if (Channel.LengthFieldSize = lfs2) and (OnWire > $FFFF) then
    raise EOSFFormatError.CreateFmt(SOSFBlockLengthOverflow, [OnWire, Channel.Index]);

  WriteUInt16(Idx);
  case Channel.LengthFieldSize of
    lfs2:
      WriteUInt16(Word(OnWire));
    lfs4:
      WriteUInt32(OnWire);
  end;
  WriteRawBytes(Payload);

  if FIntegrity <> ipNone then
  begin
    // Frame CRC32C over channel index (2 LE) + length field (2/4 LE) + payload.
    ChanLE[0] := Byte(Idx);
    ChanLE[1] := Byte(Idx shr 8);
    LenLE[0] := Byte(OnWire);
    LenLE[1] := Byte(OnWire shr 8);
    LenLE[2] := Byte(OnWire shr 16);
    LenLE[3] := Byte(OnWire shr 24);
    if Channel.LengthFieldSize = lfs2 then
      LenBytes := 2
    else
      LenBytes := 4;
    Crc.Init;
    Crc.Update(ChanLE[0], 2);
    Crc.Update(LenLE[0], LenBytes);
    if Length(Payload) > 0 then
      Crc.Update(Payload[0], Length(Payload));
    CrcVal := Crc.Final;
    CrcLE[0] := Byte(CrcVal);
    CrcLE[1] := Byte(CrcVal shr 8);
    CrcLE[2] := Byte(CrcVal shr 16);
    CrcLE[3] := Byte(CrcVal shr 24);
    FStream.WriteBuffer(CrcLE[0], 4);
  end;
end;

procedure TOSFFile.WriteEquidistantBlock(ChannelIndex: Integer; const Samples: array of Double; FirstTimestampNs: Int64);
var
  Channel: TOSFChannelDef;
  IsStart: Boolean;
  N: Integer;
  Payload: TBytes;
begin
  if not FHeaderWritten then
    raise EOSFException.Create(SOSFWriteBeforeHeader);

  Channel := ChannelByIndex(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt(SOSFEquiUnknownChannel, [ChannelIndex]);

  N := Length(Samples);
  if N = 0 then
    Exit;

  // A non-zero FirstTimestampNs always starts a new segment (bcStartData).
  // If no timestamp was passed but no segment has been opened yet, the caller
  // forgot to seed the channel - fail loudly.
  IsStart := (FirstTimestampNs <> 0) or (not Channel.StartBlockWritten);
  if IsStart and (FirstTimestampNs = 0) then
    raise EOSFFormatError.Create(SOSFEquiNoFirstTimestamp);
  if IsStart and (Channel.SampleRate <= 0) then
    raise EOSFFormatError.Create(SOSFEquiNoSampleRate);

  Payload := EncodeEquidistantPayload(IsStart, FirstTimestampNs, Channel.SampleRate, Samples);
  WriteDataBlock(Channel, Payload);

  Channel.StartBlockWritten := True;
  Channel.SampleCount := Channel.SampleCount + N;
  if IsStart then
  begin
    Channel.StartTimestampNs := FirstTimestampNs;
    Channel.LastTimestampNs := FirstTimestampNs;
  end;
  Logger.Write(SOSFLogWriteEquidistant, [ChannelIndex, N], llDebug, 'TOSFFile');
end;

procedure TOSFFile.WriteTimestampedSample(ChannelIndex: Integer; TimestampNs: Int64; const Value: TBytes);
begin
  // A single-sample call is just a one-element batch - same byte layout because
  // the multi-value flag stays clear and no count field is written.
  WriteTimestampedBlock(ChannelIndex, [TimestampNs], [Value]);
end;

procedure TOSFFile.WriteTimestampedBlock(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of TBytes);
var
  Channel: TOSFChannelDef;
  N: Integer;
  I: Integer;
  Payload: TBytes;
begin
  if not FHeaderWritten then
    raise EOSFException.Create(SOSFWriteBeforeHeader);

  N := Length(Timestamps);
  if N = 0 then
    Exit;
  if Length(Values) <> N then
    raise EOSFException.Create(SOSFTSBlockLengthMismatch);

  Channel := ChannelByIndex(ChannelIndex);
  if not Assigned(Channel) then
    raise EOSFFormatError.CreateFmt(SOSFTSBlockUnknown, [ChannelIndex]);

  // String and binary in bcAbsTimeStampData are one-sample-per-block per
  // spec. The multi-sample form for variable-length types is not part of
  // the standard wire format and the Rust / C++ reference readers do not
  // parse the historical Delphi per-sample uint32 length-prefix layout.
  // Auto-split multi-sample variable-length calls into N single-sample
  // blocks so callers get a uniform API regardless of datatype.
  if OSFDataTypeIsVariableLength(Channel.DataType) and (N > 1) then
  begin
    for I := 0 to N - 1 do
      WriteTimestampedSample(ChannelIndex, Timestamps[I], Values[I]);
    Exit;
  end;

  Payload := EncodeTimestampedPayload(FVersion, OSFDataTypeIsVariableLength(Channel.DataType), Timestamps, Values);
  WriteDataBlock(Channel, Payload);

  Channel.SampleCount := Channel.SampleCount + N;
  Channel.LastTimestampNs := Timestamps[N - 1];
  Logger.Write(SOSFLogWriteTimestamped, [ChannelIndex, N], llDebug, 'TOSFFile');
end;

procedure TOSFFile.WriteTimestampedDoubles(ChannelIndex: Integer; const Timestamps: array of Int64; const Values: array of Double);
var
  EncodedValues: array of TBytes;
  I: Integer;
  D: Double;
begin
  if Length(Values) <> Length(Timestamps) then
    raise EOSFException.Create(SOSFTSDoublesLengthMismatch);
  SetLength(EncodedValues, Length(Values));
  for I := 0 to High(Values) do
  begin
    SetLength(EncodedValues[I], SizeOf(Double));
    D := Values[I];
    Move(D, EncodedValues[I][0], SizeOf(Double));
  end;
  WriteTimestampedBlock(ChannelIndex, Timestamps, EncodedValues);
end;

// ── Lookup ───────────────────────────────────────────────────────────────────

function TOSFFile.FindChannel(Index: Integer): TOSFChannelDef;
var
  I: Integer;
begin
  for I := 0 to FChannels.Count - 1 do
    if FChannels[I].Index = Index then
      Exit(FChannels[I]);
  Result := nil;
end;

function TOSFFile.ChannelByName(const Name: string): TOSFChannelDef;
var
  I: Integer;
begin
  for I := 0 to FChannels.Count - 1 do
    if FChannels[I].Name = Name then
      Exit(FChannels[I]);
  Result := nil;
end;

function TOSFFile.ChannelByIndex(Index: Integer): TOSFChannelDef;
begin
  Result := FindChannel(Index);
end;

function TOSFFile.IsChannelIncluded(ChannelIndex: Integer): Boolean;
var
  Included: Boolean;
begin
  if Length(FChannelFilter) = 0 then
    Exit(True);
  if FChannelIncluded.TryGetValue(Word(ChannelIndex), Included) then
    Result := Included
  else
    // No metablock entry for this index - by convention treat as included so
    // that any downstream block-read warning still fires through the normal
    // path rather than being suppressed by the filter.
    Result := True;
end;

end.
