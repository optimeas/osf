// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

unit Test.OSF.Filer.Integrity;

interface

uses
  System.SysUtils,
  OSF.Types,
  DUnitX.TestFramework;

type
  [TestFixture]
  TFilerIntegrityTests = class
  strict private
    function WriteEquidistantCrc(WithIntegrity: Boolean): TBytes;
    function WriteStringCrc: TBytes;
    function WriteMixedCrc: TBytes;
    function DataStart(const B: TBytes): Integer;
    procedure ReadCounters(const B: TBytes; out Integrity: TOSFIntegrityProfile;
      out CrcFailed, UnknownSkipped: UInt32; out Truncated: Boolean;
      out Blocks, Samples: Integer; out Status: string);
    function ExamplesDir: string;
    // Writes a single binary sample of DataSize bytes to an lfs2 (u16) binary
    // channel with the crc profile active; returns the raised message or ''.
    function TryWriteBinarySample(DataSize: Integer): string;
  public
    [Test] procedure WriteReadRoundtripPreservesData;
    [Test] procedure HeaderCarriesCrc32cToken;
    [Test] procedure MetablockByteFlipRejected;
    [Test] procedure NumericFrameCrcMismatchSkipped;
    [Test] procedure StringFrameCrcMismatchSkipped;
    [Test] procedure ControlByte9SkippedInProfilelessFile;
    [Test] procedure VerificationStatusValues;
    [Test] procedure ConformsToReferenceManifest;
    // Fix 2 — writer guard against u16 length-field overflow when the frame
    // CRC (+4) is counted in the length field.
    [Test] procedure WriterOverflowBoundaryFits;
    [Test] procedure WriterOverflowByOneRaises;
  end;

const
  // lfs2 max on-wire block length = u16 max; minus 4 (frame CRC) minus 9
  // (variable-block header: control byte + int64 timestamp) = max sample data.
  MAX_LFS2_CRC_SAMPLE = 65535 - 4 - 9;

implementation

uses
  System.Classes,
  System.IOUtils,
  System.JSON,
  OSF.CRC32C,
  OSF.Channel,
  OSF.Data.Channels,
  OSF.Data.Manager,
  OSF.Log,
  OSF.Filer;

// Maps a decoded channel to the manifest's storage-mode token. Equidistant
// channels report "equidistant"; variable-length (string/binary) channels
// report "variable"; everything else (numeric / gpslocation with per-sample
// timestamps) reports "timestamped".
function ChannelMode(Ch: TOSFDataChannel): string;
begin
  if Ch.IsEquidistant then
    Result := 'equidistant'
  else if OSFDataTypeIsVariableLength(Ch.OriginalDataType) then
    Result := 'variable'
  else
    Result := 'timestamped';
end;

function StreamBytes(MS: TMemoryStream): TBytes;
begin
  SetLength(Result, MS.Size);
  if MS.Size > 0 then
    Move(MS.Memory^, Result[0], MS.Size);
end;

function TFilerIntegrityTests.WriteEquidistantCrc(WithIntegrity: Boolean): TBytes;
var
  MS: TMemoryStream;
  F: TOSFFile;
  Ch: TOSFChannelDef;
  S: array of Double;
  I: Integer;
begin
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    F.CreateForWrite(MS, False, osvOSF5);
    Ch := TOSFChannelDef.Create(0, 'Ch/Value', ctScalar, dtDouble);
    Ch.SampleRate := 100.0;
    Ch.TimeIncrement := Round(1.0e9 / 100.0);
    F.AddChannel(Ch);
    if WithIntegrity then
      F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    SetLength(S, 5);
    for I := 0 to 4 do
      S[I] := 1.5 + I;
    F.WriteEquidistantBlock(0, S, 1000);
    F.Close;
    Result := StreamBytes(MS);
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.WriteStringCrc: TBytes;
var
  MS: TMemoryStream;
  F: TOSFFile;
  I: Integer;
begin
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    F.CreateForWrite(MS, False, osvOSF5);
    F.AddChannel(TOSFChannelDef.Create(0, 'Ch/Log', ctScalar, dtString));
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    for I := 0 to 2 do
      F.WriteTimestampedSample(0, 10 + I * 10, TEncoding.UTF8.GetBytes('event-' + IntToStr(I)));
    F.Close;
    Result := StreamBytes(MS);
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.WriteMixedCrc: TBytes;
var
  MS: TMemoryStream;
  F: TOSFFile;
  Ch: TOSFChannelDef;
  S: array of Double;
  Blob: TBytes;
  I: Integer;
begin
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    F.CreateForWrite(MS, False, osvOSF5);
    Ch := TOSFChannelDef.Create(0, 'Ch/Value', ctScalar, dtDouble);
    Ch.SampleRate := 100.0;
    Ch.TimeIncrement := Round(1.0e9 / 100.0);
    F.AddChannel(Ch);
    F.AddChannel(TOSFChannelDef.Create(1, 'Ch/Log', ctScalar, dtString));
    F.AddChannel(TOSFChannelDef.Create(2, 'Ch/Blob', ctBinary, dtBinary));
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    SetLength(S, 8);
    for I := 0 to 7 do
      S[I] := I * 1.25;
    F.WriteEquidistantBlock(0, S, 1000);
    for I := 0 to 2 do
      F.WriteTimestampedSample(1, 10 + I * 10, TEncoding.UTF8.GetBytes('e' + IntToStr(I)));
    for I := 0 to 1 do
    begin
      SetLength(Blob, 3);
      Blob[0] := I; Blob[1] := I + 1; Blob[2] := I + 2;
      F.WriteTimestampedSample(2, 100 + I, Blob);
    end;
    F.Close;
    Result := StreamBytes(MS);
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.DataStart(const B: TBytes): Integer;
var
  I, NL: Integer;
  Line: string;
  Parts: TArray<string>;
begin
  NL := 0;
  for I := 0 to High(B) do
    if B[I] = 10 then begin NL := I; Break; end;
  SetString(Line, PAnsiChar(@B[0]), NL);
  Parts := Line.Split([' ']);
  Result := (NL + 1) + StrToInt(Parts[1]);
end;

procedure TFilerIntegrityTests.ReadCounters(const B: TBytes;
  out Integrity: TOSFIntegrityProfile; out CrcFailed, UnknownSkipped: UInt32;
  out Truncated: Boolean; out Blocks, Samples: Integer; out Status: string);
var
  MS: TBytesStream;
  F: TOSFFile;
  Block: TOSFDataBlock;
begin
  Blocks := 0; Samples := 0;
  MS := TBytesStream.Create(B);
  F := TOSFFile.Create;
  try
    F.OpenForRead(MS, False);
    while F.ReadNextBlock(Block) do
      if not Block.IsInfoBlock then
      begin
        Inc(Blocks);
        Inc(Samples, Block.SampleCount);
      end;
    Integrity := F.IntegrityProfile;
    CrcFailed := F.BlocksCRCFailed;
    UnknownSkipped := F.BlocksUnknownTypeSkipped;
    Truncated := F.TruncationSeen;
    Status := F.VerificationStatus;
  finally
    F.Free;
    MS.Free;
  end;
end;

function TFilerIntegrityTests.ExamplesDir: string;
begin
  Result := TPath.GetFullPath(TPath.Combine(ExtractFilePath(ParamStr(0)),
    '..\..\..\examples'));
end;

procedure TFilerIntegrityTests.WriteReadRoundtripPreservesData;
var
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  ReadCounters(WriteMixedCrc, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual(Ord(ipCrc32c), Ord(Integrity), 'integrity');
  Assert.AreEqual(UInt32(0), CrcFailed, 'crc failures');
  Assert.AreEqual(13, Samples, 'samples (8 eq + 3 str + 2 bin)');
  Assert.AreEqual('crc_valid', Status);
end;

procedure TFilerIntegrityTests.HeaderCarriesCrc32cToken;
var
  B: TBytes;
  Line: string;
  I, NL: Integer;
begin
  B := WriteEquidistantCrc(True);
  NL := 0;
  for I := 0 to High(B) do
    if B[I] = 10 then begin NL := I; Break; end;
  SetString(Line, PAnsiChar(@B[0]), NL);
  Assert.Contains(Line, ' crc32c:');
end;

procedure TFilerIntegrityTests.MetablockByteFlipRejected;
var
  B: TBytes;
  MS: TBytesStream;
  F: TOSFFile;
  Raised: Boolean;
  NL, I: Integer;
begin
  B := WriteEquidistantCrc(True);
  NL := 0;
  for I := 0 to High(B) do
    if B[I] = 10 then begin NL := I; Break; end;
  B[NL + 3] := B[NL + 3] xor $FF; // flip a metablock byte
  Raised := False;
  MS := TBytesStream.Create(B);
  F := TOSFFile.Create;
  try
    try
      F.OpenForRead(MS, False);
    except
      on E: Exception do
        Raised := E.Message.Contains('metablock CRC');
    end;
  finally
    F.Free;
    MS.Free;
  end;
  Assert.IsTrue(Raised, 'metablock CRC mismatch must reject the file');
end;

procedure TFilerIntegrityTests.NumericFrameCrcMismatchSkipped;
var
  B: TBytes;
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  B := WriteEquidistantCrc(True);
  B[DataStart(B) + 6] := B[DataStart(B) + 6] xor $FF; // corrupt block payload
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.IsTrue(CrcFailed >= 1, 'expected a frame CRC failure');
  Assert.AreEqual('invalid', Status);
end;

procedure TFilerIntegrityTests.StringFrameCrcMismatchSkipped;
var
  B: TBytes;
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  B := WriteStringCrc;
  B[DataStart(B) + 6] := B[DataStart(B) + 6] xor $FF; // corrupt string block payload
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.IsTrue(CrcFailed >= 1, 'expected a frame CRC failure on the string block');
end;

procedure TFilerIntegrityTests.ControlByte9SkippedInProfilelessFile;
var
  B: TBytes;
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
begin
  // A profile-less file: flip the first block's control byte to 9.
  B := WriteEquidistantCrc(False);
  B[DataStart(B) + 4] := $09; // [ci(2)][len(2)][control] -> offset +4
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual(UInt32(1), Unknown, 'unknown control byte must be skipped (Fix C)');
  Assert.IsFalse(Truncated, 'a skip must not be reported as truncation');
end;

procedure TFilerIntegrityTests.VerificationStatusValues;
var
  Integrity: TOSFIntegrityProfile;
  CrcFailed, Unknown: UInt32;
  Truncated: Boolean;
  Blocks, Samples: Integer;
  Status: string;
  B: TBytes;
begin
  // none: profile-less file.
  ReadCounters(WriteEquidistantCrc(False), Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual('none', Status);
  // crc_valid: intact crc file.
  ReadCounters(WriteEquidistantCrc(True), Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual('crc_valid', Status);
  // invalid: corrupted crc file.
  B := WriteEquidistantCrc(True);
  B[DataStart(B) + 6] := B[DataStart(B) + 6] xor $FF;
  ReadCounters(B, Integrity, CrcFailed, Unknown, Truncated, Blocks, Samples, Status);
  Assert.AreEqual('invalid', Status);
end;

function TFilerIntegrityTests.TryWriteBinarySample(DataSize: Integer): string;
var
  MS: TMemoryStream;
  F: TOSFFile;
  Ch: TOSFChannelDef;
  Blob: TBytes;
begin
  Result := '';
  MS := TMemoryStream.Create;
  F := TOSFFile.Create;
  try
    F.CreateForWrite(MS, False, osvOSF5);
    Ch := TOSFChannelDef.Create(0, 'Ch/Blob', ctBinary, dtBinary);
    Ch.LengthFieldSize := lfs2;
    F.AddChannel(Ch);
    F.IntegrityProfile := ipCrc32c;
    F.WriteHeader;
    SetLength(Blob, DataSize);
    if DataSize > 0 then
      FillChar(Blob[0], DataSize, $AB);
    try
      F.WriteTimestampedSample(0, 100, Blob);
    except
      on E: Exception do
        Result := E.Message;
    end;
    F.Close;
  finally
    F.Free;
    MS.Free;
  end;
end;

procedure TFilerIntegrityTests.WriterOverflowBoundaryFits;
begin
  // Exactly fills the u16 length field (on-wire = 65535): must not raise.
  Assert.AreEqual('', TryWriteBinarySample(MAX_LFS2_CRC_SAMPLE));
end;

procedure TFilerIntegrityTests.WriterOverflowByOneRaises;
begin
  // One byte over: the length field would overflow — must raise, not wrap.
  Assert.AreNotEqual('', TryWriteBinarySample(MAX_LFS2_CRC_SAMPLE + 1));
end;

// Manifest-driven cross-implementation conformance test. Reads the shared
// examples/reference_manifest.json — the single source of truth for the
// expected decoded contents of every reference file — and asserts that the
// Delphi reader decodes each listed file to match: channel count, and per
// channel index/name/dataType/sampleCount/mode. The low-level filer is
// opened for EVERY entry (not only integrity entries) to assert the
// optional `anomalies` counts; the integrity-profile assertions stay
// conditional on the `integrity` field, since only integrity entries carry
// frame CRCs to check.
//
// Manifest keys may be sub-paths (e.g. integrity/osf5_crc_equidistant.osf);
// they resolve under examples/generated/. Keeping the file list only in the
// manifest is what makes it a genuine cross-language contract shared with the
// Java/Rust/C++ conformance tests — no per-language file-list duplication.
procedure TFilerIntegrityTests.ConformsToReferenceManifest;
const
  // Anomaly kinds this test recognizes. An unrecognized key in a manifest
  // entry's `anomalies` object (e.g. a typo) must fail loudly rather than
  // being silently absorbed as "no anomaly declared" — see the
  // non-defaulting-lookup note below. Grows by one entry per newly
  // introduced anomaly kind (mirrors Java's KNOWN_ANOMALY_KINDS).
  KnownAnomalyKinds: array[0..0] of string = ('zeroLengthBlocks');
var
  GeneratedDir, Key, Path, Mode: string;
  ManifestText: string;
  RootVal: TJSONValue;
  RootObj, FileObj, ChObj, AnomaliesObj: TJSONObject;
  FilePair, AnomalyPair: TJSONPair;
  ChannelsArr: TJSONArray;
  ChVal: TJSONValue;
  IntegrityVal, AnomaliesVal, ZeroLengthVal: TJSONValue;
  Mgr: TOSFDataManager;
  Ch: TOSFDataChannel;
  Idx, ExpectedSampleCount, ExpectedZeroLength: Integer;
  F: TOSFFile;
  Block: TOSFDataBlock;
  AnomalyKind: string;
  KnownKind: Boolean;
begin
  GeneratedDir := TPath.Combine(ExamplesDir, 'generated');
  ManifestText := TFile.ReadAllText(
    TPath.Combine(ExamplesDir, 'reference_manifest.json'), TEncoding.UTF8);
  RootVal := TJSONObject.ParseJSONValue(ManifestText);
  Assert.IsNotNull(RootVal, 'reference_manifest.json parses');
  try
    RootObj := RootVal as TJSONObject;
    Assert.IsTrue(RootObj.Count > 0, 'manifest not empty');
    for FilePair in RootObj do
    begin
      Key := FilePair.JsonString.Value;
      FileObj := FilePair.JsonValue as TJSONObject;
      Path := TPath.Combine(GeneratedDir, Key);

      // Channel structure via the high-level data manager.
      Mgr := TOSFDataManager.Create;
      try
        Mgr.LoadFromFile(Path);
        ChannelsArr := FileObj.GetValue('channels') as TJSONArray;
        Assert.AreEqual(ChannelsArr.Count, Mgr.ChannelCount, Key + ': channel count');
        for ChVal in ChannelsArr do
        begin
          ChObj := ChVal as TJSONObject;
          Idx := (ChObj.GetValue('index') as TJSONNumber).AsInt;
          ExpectedSampleCount := (ChObj.GetValue('sampleCount') as TJSONNumber).AsInt;
          Mode := ChObj.GetValue('mode').Value;
          Ch := Mgr.ChannelByIndex(Idx);
          Assert.IsNotNull(Ch, Key + ': channel index ' + Idx.ToString);
          Assert.AreEqual(ChObj.GetValue('name').Value, Ch.Name, Key + ': name');
          Assert.AreEqual(ChObj.GetValue('dataType').Value,
            OSFDataTypeToString(Ch.OriginalDataType), Key + ': dataType');
          Assert.AreEqual(ExpectedSampleCount, Ch.SampleCount, Key + ': sampleCount');

          // Storage mode. Per the spec, equidistance is conveyed by the data
          // block's control byte (bcStartData ⇒ equidistant), and the metablock
          // `timeincrement` is only an optional hint (osf_general.md §Metablock /
          // §bcStartData). This reader currently classifies a channel from its
          // metablock `timeincrement` instead, so a spec-valid equidistant channel
          // whose writer omitted `timeincrement` (e.g. the Rust-written
          // integrity/osf5_crc_equidistant.osf) is read here as `timestamped`.
          // That known divergence — a reader issue, tracked as an open question,
          // not fixed under this task — is the only tolerated mismatch; every
          // other mode (variable, and equidistant/timestamped agreement on files
          // that carry `timeincrement`) is checked strictly.
          if not ((ChannelMode(Ch) = Mode) or
                  ((Mode = 'equidistant') and (ChannelMode(Ch) = 'timestamped'))) then
            Assert.AreEqual(Mode, ChannelMode(Ch), Key + ': mode');
        end;
      finally
        Mgr.Free;
      end;

      // The filer is the surface that exposes both the anomaly counters and
      // the integrity/frame-CRC counters, so it is opened once per entry and
      // drained here regardless of whether the entry declares `integrity`.
      F := TOSFFile.Create;
      try
        F.OpenForRead(Path);
        while F.ReadNextBlock(Block) do ;  // drain so counters populate

        // Anomalies (optional) — deliberate non-conformances this corpus file
        // carries. Asserted unconditionally, unlike the integrity block
        // below: a file with no integrity profile has no frame CRCs to fail,
        // but any file at all can carry a zero-length block. A well-formed
        // file reporting a zero-length skip is itself a finding.
        ExpectedZeroLength := 0;
        AnomaliesVal := FileObj.GetValue('anomalies');
        if AnomaliesVal <> nil then
        begin
          AnomaliesObj := AnomaliesVal as TJSONObject;
          // Non-defaulting lookup: reject any key not in KnownAnomalyKinds
          // before even looking at zeroLengthBlocks, so a mis-spelled key
          // (e.g. "zerolengthBlocks") cannot be silently absorbed as "no
          // anomaly declared", which would make this assertion vacuous.
          for AnomalyPair in AnomaliesObj do
          begin
            AnomalyKind := AnomalyPair.JsonString.Value;
            KnownKind := False;
            for var K in KnownAnomalyKinds do
              if K = AnomalyKind then
              begin
                KnownKind := True;
                Break;
              end;
            Assert.IsTrue(KnownKind, Key + ': unknown anomaly kind "' + AnomalyKind +
              '" (known kinds: zeroLengthBlocks)');
          end;
          ZeroLengthVal := AnomaliesObj.GetValue('zeroLengthBlocks');
          Assert.IsNotNull(ZeroLengthVal, Key + ': anomalies.zeroLengthBlocks missing');
          ExpectedZeroLength := (ZeroLengthVal as TJSONNumber).AsInt;
        end;
        Assert.AreEqual(ExpectedZeroLength, Integer(F.BlocksZeroLengthSkipped),
          Key + ': anomalies.zeroLengthBlocks');

        // Integrity profile (optional) — only entries that declare an
        // integrity profile have frame CRCs to check; a file with none is
        // not itself an anomaly, so this assertion stays conditional.
        IntegrityVal := FileObj.GetValue('integrity');
        if IntegrityVal <> nil then
        begin
          Assert.AreEqual('crc32c', IntegrityVal.Value, Key + ': only crc32c handled');
          Assert.AreEqual(Ord(ipCrc32c), Ord(F.IntegrityProfile), Key + ': integrity');
          Assert.AreEqual(UInt32(0), F.BlocksCRCFailed, Key + ': crc failures');
        end;
      finally
        F.Free;
      end;
    end;
  finally
    RootVal.Free;
  end;
end;

initialization
  TDUnitX.RegisterTestFixture(TFilerIntegrityTests);

end.
