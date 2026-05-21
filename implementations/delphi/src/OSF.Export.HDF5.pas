// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Renders a TOSFDataManager as an HDF5 file. Every channel becomes one
// chunked, shuffled and deflated 1-D dataset of compound records
// {int64 timestamp_ns; value}; the hierarchical channel name is split on
// the namespace separator into an HDF5 group path. File-level metadata
// lands in root attributes, per-channel metadata in dataset attributes.
//
// The actual HDF5 work goes through the Hdf5.* DLL wrapper units; this
// unit only maps OSF concepts onto them. Windows-only (HDF5 export targets
// Win64) — compiles empty on other platforms.
unit OSF.Export.HDF5;

interface

{$IFDEF MSWINDOWS}
uses
  System.SysUtils,
  System.Generics.Collections,
  OSF.Types,
  OSF.Channel,
  OSF.Log,
  OSF.Data.Manager,
  OSF.Data.Channels,
  OSF.Export,
  Hdf5.Types,
  Hdf5.Api,
  Hdf5.Wrapper;

type
  TOSFHDF5Exporter = class(TOSFExporter)
  private
    FChunkSize     : Integer;
    FDeflateLevel  : Integer;
    FUseShuffle    : Boolean;
    FLibraryDir    : string;
    FNamespaceSep  : string;
    FChannelsWritten : Integer;

    procedure SetChunkSize(Value: Integer);
    procedure SetDeflateLevel(Value: Integer);

    // Mapping helpers.
    function  DatasetPath(const ChannelName: string): UTF8String;
    function  ValueByteSize(DataType: TOSFDataType): Integer;
    function  IsSupported(DataType: TOSFDataType): Boolean;
    function  BuildCompoundType(DataType: TOSFDataType): THdf5Datatype;

    // Typed sample access for the datatypes that cannot go through the
    // lossy ValueAsDouble path (Int64/UInt64 precision, GPS triple).
    function  Int64ValueAt (Channel: TOSFDataChannel; Index: Integer): Int64;
    function  UInt64ValueAt(Channel: TOSFDataChannel; Index: Integer): UInt64;
    function  GpsValueAt   (Channel: TOSFDataChannel; Index: Integer): TOSFGpsLocation;

    // Per-sample timestamp. Segment-aware for equidistant channels so the
    // gaps between bcStartData segments are honoured.
    function  SampleTimestampNs(Channel: TOSFDataChannel; SampleIndex: Integer;
                                var SegmentCursor: Integer): Int64;

    // Fills Buffer with Count packed compound records starting at StartIndex.
    procedure FillBatch(Channel: TOSFDataChannel; DataType: TOSFDataType;
                        StartIndex, Count, RecordSize: Integer;
                        var SegmentCursor: Integer;
                        var Buffer: TBytes; var StringKeep: TArray<UTF8String>);

    procedure WriteRootAttributes(H5File: THdf5File);
    procedure WriteChannelAttributes(Dataset: THdf5Dataset; Channel: TOSFDataChannel);
    procedure WriteChannel(H5File: THdf5File; Channel: TOSFDataChannel;
                           Lcpl, Dcpl: THdf5PropertyList);
  protected
    procedure DoExport(const FileName: string); override;
  public
    constructor Create(DataManager: TOSFDataManager);

    // Samples per HDF5 chunk. Also bounds the in-memory conversion buffer.
    // Default 8192; clamped to a minimum of 1.
    property ChunkSize: Integer read FChunkSize write SetChunkSize;
    // gzip/deflate level 0..9 (0 disables compression). Default 4.
    property DeflateLevel: Integer read FDeflateLevel write SetDeflateLevel;
    // Enable the HDF5 byte-shuffle filter ahead of deflate. Default True.
    property UseShuffle: Boolean read FUseShuffle write FUseShuffle;
    // Directory to search first for hdf5.dll. Empty = default resolution.
    property LibraryDir: string read FLibraryDir write FLibraryDir;
    // Separator that splits a hierarchical channel name into HDF5 groups.
    // Default '.' (the OSF default namespace separator).
    property NamespaceSep: string read FNamespaceSep write FNamespaceSep;
  end;

resourcestring
  // Log messages emitted from DoExport.
  SOSFLogHDF5Started         = 'HDF5 export started: %s  channels=%d';
  SOSFLogHDF5Finished        = 'HDF5 export finished: %s  channels_written=%d';
  SOSFLogHDF5SkipChannel     = 'Skipping channel [%s]: %s';
  SOSFLogHDF5UnsupportedType = 'Skipping channel [%s]: data type "%s" is not supported by the HDF5 export';
  SOSFLogHDF5Failed          = 'HDF5 export failed: %s';
  SOSFHDF5UnsupportedValueType = 'Unsupported HDF5 value type: %d';
{$ENDIF}

implementation

{$IFDEF MSWINDOWS}

// Technical constants — defaults and the wire-level timestamp format.
const
  DEFAULT_CHUNK_SIZE    = 8192;
  DEFAULT_DEFLATE_LEVEL = 4;
  MAX_DEFLATE_LEVEL     = 9;
  ISO8601_UTC_FORMAT    = 'yyyy-mm-dd"T"hh:nn:ss"."zzz"Z"';
  TIMESTAMP_FIELD_SIZE  = 8;  // int64 timestamp_ns, always at compound offset 0

// Returns the OSF version token for a root attribute.
function OsfVersionName(Version: TOSFVersion): string;
begin
  case Version of
    osvOSF4: Result := 'OSF4';
    osvOSF5: Result := 'OSF5';
  else
    Result := 'unknown';
  end;
end;

// ── Construction ──────────────────────────────────────────────────────────────

constructor TOSFHDF5Exporter.Create(DataManager: TOSFDataManager);
begin
  inherited Create(DataManager);
  FChunkSize    := DEFAULT_CHUNK_SIZE;
  FDeflateLevel := DEFAULT_DEFLATE_LEVEL;
  FUseShuffle   := True;
  FLibraryDir   := '';
  FNamespaceSep := OSF_DEFAULT_NAMESPACE_SEP;
end;

procedure TOSFHDF5Exporter.SetChunkSize(Value: Integer);
begin
  if Value < 1 then
    FChunkSize := 1
  else
    FChunkSize := Value;
end;

procedure TOSFHDF5Exporter.SetDeflateLevel(Value: Integer);
begin
  if Value < 0 then
    FDeflateLevel := 0
  else if Value > MAX_DEFLATE_LEVEL then
    FDeflateLevel := MAX_DEFLATE_LEVEL
  else
    FDeflateLevel := Value;
end;

// ── Mapping helpers ───────────────────────────────────────────────────────────

function TOSFHDF5Exporter.DatasetPath(const ChannelName: string): UTF8String;
var
  Path: string;
begin
  Path := ChannelName;
  // Translate the OSF namespace separator into the HDF5 path separator so
  // each name segment becomes one group level.
  if (FNamespaceSep <> '') and (FNamespaceSep <> '/') then
    Path := StringReplace(Path, FNamespaceSep, '/', [rfReplaceAll]);
  Result := UTF8String(Path);
end;

function TOSFHDF5Exporter.ValueByteSize(DataType: TOSFDataType): Integer;
begin
  case DataType of
    dtBool, dtInt8, dtUInt8:     Result := 1;
    dtInt16, dtUInt16:           Result := 2;
    dtInt32, dtUInt32, dtFloat:  Result := 4;
    dtInt64, dtUInt64, dtDouble: Result := 8;
    dtGpsLocation:               Result := SizeOf(TOSFGpsLocation);
    dtString:                    Result := SizeOf(Pointer);  // VLEN: a char pointer
  else
    Result := 0;
  end;
end;

function TOSFHDF5Exporter.IsSupported(DataType: TOSFDataType): Boolean;
begin
  // Everything except dtBinary, which is deferred (variable-length opaque
  // blobs would need their own dataset shape).
  Result := DataType in [dtBool,
                         dtInt8,  dtInt16,  dtInt32,  dtInt64,
                         dtUInt8, dtUInt16, dtUInt32, dtUInt64,
                         dtFloat, dtDouble, dtString, dtGpsLocation];
end;

function TOSFHDF5Exporter.BuildCompoundType(DataType: TOSFDataType): THdf5Datatype;
var
  ValueType: hid_t;
  ValueTypeObj: THdf5Datatype;
begin
  Result := THdf5Datatype.CreateCompound(TIMESTAMP_FIELD_SIZE + ValueByteSize(DataType));
  try
    Result.Insert('timestamp_ns', 0, H5T_STD_I64LE);
    // ValueTypeObj holds any datatype that has to be created on the fly
    // (VLEN string, GPS sub-compound); predefined _g types need no object.
    ValueTypeObj := nil;
    try
      case DataType of
        dtBool, dtUInt8: ValueType := H5T_STD_U8LE;
        dtInt8:          ValueType := H5T_STD_I8LE;
        dtInt16:         ValueType := H5T_STD_I16LE;
        dtUInt16:        ValueType := H5T_STD_U16LE;
        dtInt32:         ValueType := H5T_STD_I32LE;
        dtUInt32:        ValueType := H5T_STD_U32LE;
        dtInt64:         ValueType := H5T_STD_I64LE;
        dtUInt64:        ValueType := H5T_STD_U64LE;
        dtFloat:         ValueType := H5T_IEEE_F32LE;
        dtDouble:        ValueType := H5T_IEEE_F64LE;
        dtString:
          begin
            ValueTypeObj := THdf5Datatype.CreateCopy(H5T_C_S1);
            ValueTypeObj.SetVariableLength;
            ValueTypeObj.SetCharSet(H5T_CSET_UTF8);
            ValueType := ValueTypeObj.Handle;
          end;
        dtGpsLocation:
          begin
            ValueTypeObj := THdf5Datatype.CreateCompound(SizeOf(TOSFGpsLocation));
            ValueTypeObj.Insert('latitude',  0, H5T_IEEE_F64LE);
            ValueTypeObj.Insert('longitude', 8, H5T_IEEE_F64LE);
            ValueTypeObj.Insert('altitude', 16, H5T_IEEE_F64LE);
            ValueType := ValueTypeObj.Handle;
          end;
      else
        // Unreachable — WriteChannel checks IsSupported first.
        raise EOSFException.CreateFmt(SOSFHDF5UnsupportedValueType, [Ord(DataType)]);
      end;
      // H5Tinsert copies the member type, so ValueTypeObj may be freed after.
      Result.Insert('value', TIMESTAMP_FIELD_SIZE, ValueType);
    finally
      ValueTypeObj.Free;
    end;
  except
    Result.Free;
    raise;
  end;
end;

function TOSFHDF5Exporter.Int64ValueAt(Channel: TOSFDataChannel; Index: Integer): Int64;
begin
  if Channel is TOSFTimestampedInt64Channel then
    Result := TOSFTimestampedInt64Channel(Channel).Values[Index]
  else
    Result := TOSFEquidistantInt64Channel(Channel).Values[Index];
end;

function TOSFHDF5Exporter.UInt64ValueAt(Channel: TOSFDataChannel; Index: Integer): UInt64;
begin
  if Channel is TOSFTimestampedUInt64Channel then
    Result := TOSFTimestampedUInt64Channel(Channel).Values[Index]
  else
    Result := TOSFEquidistantUInt64Channel(Channel).Values[Index];
end;

function TOSFHDF5Exporter.GpsValueAt(Channel: TOSFDataChannel; Index: Integer): TOSFGpsLocation;
begin
  if Channel is TOSFTimestampedGpsChannel then
    Result := TOSFTimestampedGpsChannel(Channel).Values[Index]
  else
    Result := TOSFEquidistantGpsChannel(Channel).Values[Index];
end;

function TOSFHDF5Exporter.SampleTimestampNs(Channel: TOSFDataChannel;
  SampleIndex: Integer; var SegmentCursor: Integer): Int64;
var
  Equidistant: TOSFEquidistantDataChannel;
  Segments: TList<TOSFChannelSegment>;
  IncrementNs: Int64;
begin
  if not (Channel is TOSFEquidistantDataChannel) then
    // Timestamped channel: TimestampNsAt returns the stored value directly.
    Exit(Channel.TimestampNsAt(SampleIndex));

  Equidistant := TOSFEquidistantDataChannel(Channel);
  Segments := Equidistant.Segments;
  if Segments.Count = 0 then
    Exit(Channel.TimestampNsAt(SampleIndex));

  // Advance the cursor (samples are visited in order) to the segment that
  // contains SampleIndex. TimestampNsAt cannot be used here: it assumes one
  // continuous timeline and would ignore the gaps between segments.
  while (SegmentCursor < Segments.Count - 1) and
        (SampleIndex >= Segments[SegmentCursor].StartIndex +
                        Segments[SegmentCursor].SampleCount) do
    Inc(SegmentCursor);

  IncrementNs := Equidistant.TimeIncrementNs;
  Result := Segments[SegmentCursor].StartTimestampNs +
            Int64(SampleIndex - Segments[SegmentCursor].StartIndex) * IncrementNs;
end;

procedure TOSFHDF5Exporter.FillBatch(Channel: TOSFDataChannel; DataType: TOSFDataType;
  StartIndex, Count, RecordSize: Integer; var SegmentCursor: Integer;
  var Buffer: TBytes; var StringKeep: TArray<UTF8String>);
var
  Index, FlatIndex, RecordOffset, ValueOffset: Integer;
  Gps: TOSFGpsLocation;
begin
  SetLength(Buffer, Count * RecordSize);
  // VLEN string records store a char pointer; the UTF-8 backing strings must
  // outlive the H5Dwrite call, so they are kept in StringKeep for the batch.
  if DataType = dtString then
    SetLength(StringKeep, Count)
  else
    SetLength(StringKeep, 0);

  for Index := 0 to Count - 1 do
  begin
    FlatIndex    := StartIndex + Index;
    RecordOffset := Index * RecordSize;
    ValueOffset  := RecordOffset + TIMESTAMP_FIELD_SIZE;

    PInt64(@Buffer[RecordOffset])^ := SampleTimestampNs(Channel, FlatIndex, SegmentCursor);

    case DataType of
      dtBool, dtUInt8:
        Buffer[ValueOffset] := Byte(Trunc(Channel.ValueAsDouble(FlatIndex)));
      dtInt8:
        PShortInt(@Buffer[ValueOffset])^ := ShortInt(Trunc(Channel.ValueAsDouble(FlatIndex)));
      dtInt16:
        PSmallInt(@Buffer[ValueOffset])^ := SmallInt(Trunc(Channel.ValueAsDouble(FlatIndex)));
      dtUInt16:
        PWord(@Buffer[ValueOffset])^ := Word(Trunc(Channel.ValueAsDouble(FlatIndex)));
      dtInt32:
        PInteger(@Buffer[ValueOffset])^ := Integer(Trunc(Channel.ValueAsDouble(FlatIndex)));
      dtUInt32:
        PCardinal(@Buffer[ValueOffset])^ := Cardinal(Trunc(Channel.ValueAsDouble(FlatIndex)));
      dtInt64:
        PInt64(@Buffer[ValueOffset])^ := Int64ValueAt(Channel, FlatIndex);
      dtUInt64:
        PUInt64(@Buffer[ValueOffset])^ := UInt64ValueAt(Channel, FlatIndex);
      dtFloat:
        PSingle(@Buffer[ValueOffset])^ := Channel.ValueAsDouble(FlatIndex);
      dtDouble:
        PDouble(@Buffer[ValueOffset])^ := Channel.ValueAsDouble(FlatIndex);
      dtGpsLocation:
        begin
          Gps := GpsValueAt(Channel, FlatIndex);
          Move(Gps, Buffer[ValueOffset], SizeOf(Gps));
        end;
      dtString:
        begin
          StringKeep[Index] := UTF8String(Channel.ValueAsString(FlatIndex));
          PPointer(@Buffer[ValueOffset])^ := PAnsiChar(StringKeep[Index]);
        end;
    end;
  end;
end;

// ── Attribute writers ─────────────────────────────────────────────────────────

procedure TOSFHDF5Exporter.WriteRootAttributes(H5File: THdf5File);
var
  Root: hid_t;
  Mgr: TOSFDataManager;
begin
  Root := H5File.Handle;
  Mgr := DataManager;
  THdf5Attribute.WriteUtf8String(Root, 'osf_version', UTF8String(OsfVersionName(Mgr.Version)));
  THdf5Attribute.WriteUtf8String(Root, 'namespace_separator', UTF8String(FNamespaceSep));
  THdf5Attribute.WriteUtf8String(Root, 'generator', 'osftool / TOSFHDF5Exporter');
  if Mgr.CreatedUtc <> 0 then
    THdf5Attribute.WriteUtf8String(Root, 'created_utc',
      UTF8String(FormatDateTime(ISO8601_UTC_FORMAT, Mgr.CreatedUtc)));
  if Mgr.Creator <> '' then
    THdf5Attribute.WriteUtf8String(Root, 'creator', UTF8String(Mgr.Creator));
  if Mgr.Reason <> '' then
    THdf5Attribute.WriteUtf8String(Root, 'reason', UTF8String(Mgr.Reason));
  if Mgr.Tag <> '' then
    THdf5Attribute.WriteUtf8String(Root, 'tag', UTF8String(Mgr.Tag));
  if Mgr.SourceFileName <> '' then
    THdf5Attribute.WriteUtf8String(Root, 'source_file',
      UTF8String(ExtractFileName(Mgr.SourceFileName)));
  if (Mgr.Latitude <> 0) or (Mgr.Longitude <> 0) or (Mgr.Altitude <> 0) then
  begin
    THdf5Attribute.WriteDouble(Root, 'geo_lat', Mgr.Latitude);
    THdf5Attribute.WriteDouble(Root, 'geo_lon', Mgr.Longitude);
    THdf5Attribute.WriteDouble(Root, 'geo_alt', Mgr.Altitude);
  end;
end;

procedure TOSFHDF5Exporter.WriteChannelAttributes(Dataset: THdf5Dataset;
  Channel: TOSFDataChannel);
var
  Loc: hid_t;
  Def: TOSFChannelDef;
begin
  Loc := Dataset.Handle;
  Def := Channel.ChannelDef;
  THdf5Attribute.WriteUtf8String(Loc, 'osf_datatype',
    UTF8String(OSFDataTypeToString(Channel.OriginalDataType)));
  THdf5Attribute.WriteBoolean(Loc, 'osf_was_equidistant', Channel.IsEquidistant);
  THdf5Attribute.WriteInt64(Loc, 'sample_count', Channel.SampleCount);
  if Channel.PhysicalUnit <> '' then
    THdf5Attribute.WriteUtf8String(Loc, 'units', UTF8String(Channel.PhysicalUnit));
  if Channel.Comment <> '' then
    THdf5Attribute.WriteUtf8String(Loc, 'comment', UTF8String(Channel.Comment));
  if Channel.MimeType <> '' then
    THdf5Attribute.WriteUtf8String(Loc, 'mime_type', UTF8String(Channel.MimeType));
  if not Assigned(Def) then
    Exit;
  THdf5Attribute.WriteUInt16(Loc, 'osf_channel_index', Word(Def.Index));
  THdf5Attribute.WriteUtf8String(Loc, 'osf_channel_type',
    UTF8String(OSFChannelTypeToString(Def.ChannelType)));
  THdf5Attribute.WriteInt64(Loc, 'sample_period_ns', Def.TimeIncrement);
  if Def.SampleRate > 0 then
    THdf5Attribute.WriteDouble(Loc, 'sample_rate_hz', Def.SampleRate);
  if Def.PhysicalDimension <> '' then
    THdf5Attribute.WriteUtf8String(Loc, 'physical_dimension',
      UTF8String(Def.PhysicalDimension));
  if Def.DisplayName <> '' then
    THdf5Attribute.WriteUtf8String(Loc, 'long_name', UTF8String(Def.DisplayName));
  if Def.Reference <> '' then
    THdf5Attribute.WriteUtf8String(Loc, 'reference_uuid', UTF8String(Def.Reference));
end;

// ── Per-channel write ─────────────────────────────────────────────────────────

procedure TOSFHDF5Exporter.WriteChannel(H5File: THdf5File; Channel: TOSFDataChannel;
  Lcpl, Dcpl: THdf5PropertyList);
var
  DataType: TOSFDataType;
  CompoundType: THdf5Datatype;
  Space: THdf5Dataspace;
  Dataset: THdf5Dataset;
  RecordSize, SampleCount, BatchStart, BatchCount, SegmentCursor: Integer;
  Buffer: TBytes;
  StringKeep: TArray<UTF8String>;
begin
  DataType := Channel.OriginalDataType;
  if not IsSupported(DataType) then
  begin
    Log(llWarning, SOSFLogHDF5UnsupportedType,
      [Channel.Name, OSFDataTypeToString(DataType)]);
    Exit;
  end;

  RecordSize    := TIMESTAMP_FIELD_SIZE + ValueByteSize(DataType);
  SampleCount   := Channel.SampleCount;
  SegmentCursor := 0;

  CompoundType := BuildCompoundType(DataType);
  try
    Space := THdf5Dataspace.CreateUnlimited1D(0);
    try
      Dataset := THdf5Dataset.Create(H5File.Handle, DatasetPath(Channel.Name),
        CompoundType.Handle, Space.Handle, Dcpl.Handle, Lcpl.Handle);
      try
        WriteChannelAttributes(Dataset, Channel);
        BatchStart := 0;
        while BatchStart < SampleCount do
        begin
          BatchCount := SampleCount - BatchStart;
          if BatchCount > FChunkSize then
            BatchCount := FChunkSize;
          FillBatch(Channel, DataType, BatchStart, BatchCount, RecordSize,
            SegmentCursor, Buffer, StringKeep);
          Dataset.AppendChunk(@Buffer[0], BatchCount, CompoundType.Handle);
          Inc(BatchStart, BatchCount);
        end;
      finally
        Dataset.Free;
      end;
    finally
      Space.Free;
    end;
  finally
    CompoundType.Free;
  end;
  Inc(FChannelsWritten);
end;

// ── DoExport ──────────────────────────────────────────────────────────────────

procedure TOSFHDF5Exporter.DoExport(const FileName: string);
var
  Active: TArray<TOSFDataChannel>;
  H5File: THdf5File;
  Lcpl, Dcpl: THdf5PropertyList;
  Channel: TOSFDataChannel;
begin
  Active := ActiveChannels;
  FChannelsWritten := 0;
  Log(llInfo, SOSFLogHDF5Started, [FileName, Length(Active)]);
  try
    TH5Lib.EnsureLoaded(UTF8String(FLibraryDir));
    H5File := THdf5File.Create(UTF8String(FileName), H5F_ACC_TRUNC);
    try
      WriteRootAttributes(H5File);

      // One link-creation and one dataset-creation property list, shared by
      // every channel. HDF5 copies their settings into each object.
      Lcpl := THdf5PropertyList.Create(H5P_CLS_LINK_CREATE_ID);
      Dcpl := THdf5PropertyList.Create(H5P_CLS_DATASET_CREATE_ID);
      try
        Lcpl.SetCharEncoding(H5T_CSET_UTF8);
        Lcpl.SetCreateIntermediateGroup(True);
        Dcpl.SetChunk(hsize_t(FChunkSize));
        if FUseShuffle then
          Dcpl.SetShuffle;
        if FDeflateLevel > 0 then
          Dcpl.SetDeflate(Cardinal(FDeflateLevel));

        for Channel in Active do
          try
            WriteChannel(H5File, Channel, Lcpl, Dcpl);
          except
            on E: Exception do
              Log(llWarning, SOSFLogHDF5SkipChannel, [Channel.Name, E.Message]);
          end;
      finally
        Dcpl.Free;
        Lcpl.Free;
      end;

      Log(llInfo, SOSFLogHDF5Finished, [FileName, FChannelsWritten]);
    finally
      H5File.Free;
    end;
  except
    on E: Exception do
    begin
      // HDF5-specific failure log; the inherited Export wrapper adds the
      // generic 'Export failed: ...' message after we re-raise.
      Log(llError, SOSFLogHDF5Failed, [E.Message]);
      raise;
    end;
  end;
end;
{$ENDIF}

end.
