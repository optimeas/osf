// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// In-memory data channels populated from an OSF stream by TOSFDataManager.
// The Filer (OSF.Filer) is the streaming reader; this unit is the typed
// representation tailored for charting, statistics and ad-hoc inspection.
unit OSF.Data.Channels;

interface

uses
  System.SysUtils,
  System.Classes,
  System.DateUtils,
  System.Generics.Collections,
  OSF.Types,
  OSF.Channel;

type
  // A contiguous run of samples in an equidistant channel. Recorded between
  // bcStartData blocks: every bcStartData opens a new segment, every
  // bcContinuedData appends samples to the most recent segment. The flat
  // Values list of the channel grows linearly across segments — Segments
  // describe how it splits up in time.
  TOSFChannelSegment = record
    StartTimestampNs: Int64; // absolute start time of the first sample (ns since epoch)
    StartIndex: Integer; // index of the first sample in the channel's flat Values list
    SampleCount: Integer; // number of samples that belong to this segment
  end;

  // ── Abstract base ───────────────────────────────────────────────────────────

  // Common interface for every typed data channel held by TOSFDataManager.
  // Concrete subclasses pick one timing strategy (timestamped or equidistant)
  // and one value type (Double, Int32, String, ...).
  TOSFDataChannel = class abstract(TPersistent)
  private
    FChannelDef: TOSFChannelDef;
    function GetName: string;
    function GetPhysicalUnit: string;
    function GetComment: string;
    function GetMimeType: string;
    function GetIsEquidistant: Boolean;
    function GetOriginalDataType: TOSFDataType;
    function GetStartTimeUtc: TDateTime;
    function GetEndTimeUtc: TDateTime;
  protected
    FStartTimestampNs: Int64;
    FEndTimestampNs: Int64;
    FStartAssigned: Boolean;
    function GetSampleCount: Integer; virtual; abstract;
    // Updates Start/EndTimestampNs based on a newly added sample.
    procedure UpdateTimeRange(TimestampNs: Int64);
  public
    constructor Create(ADef: TOSFChannelDef); virtual;

    // Timestamp access — works for both equidistant and timestamped.
    function TimestampNsAt(Index: Integer): Int64; virtual; abstract;
    function TimestampUtcAt(Index: Integer): TDateTime;

    // Value access for charting and calculations.
    function ValueAsDouble(Index: Integer): Double; virtual; abstract;
    function ValueAsString(Index: Integer): string; virtual; abstract;

    // True for Int64/UInt64 channels where conversion to Double may lose
    // information for values > 2^53 (~9.0e15). False for every other type.
    function HasDoublePrecisionLoss: Boolean; virtual;

    // Deep copy. Concrete subclasses produce an instance of the same class.
    function Clone: TOSFDataChannel; virtual; abstract;

    // Called by TOSFDataManager during loading. Decodes RawBytes according to
    // OriginalDataType and appends the result to the channel's value list.
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); virtual; abstract;

    // Metadata — copied from TOSFChannelDef on load. ChannelDef is referenced
    // only; the data channel does not own it. The setter is exposed so a
    // higher-level container (e.g. TOSFDataManager) can rebind a cloned
    // channel to its own private def copy without rebuilding the data.
    property ChannelDef: TOSFChannelDef read FChannelDef write FChannelDef;
    property Name: string read GetName;
    property PhysicalUnit: string read GetPhysicalUnit;
    property Comment: string read GetComment;
    property OriginalDataType: TOSFDataType read GetOriginalDataType;
    property IsEquidistant: Boolean read GetIsEquidistant;
    property MimeType: string read GetMimeType;

    // Timing.
    property StartTimestampNs: Int64 read FStartTimestampNs;
    property EndTimestampNs: Int64 read FEndTimestampNs;
    property StartTimeUtc: TDateTime read GetStartTimeUtc;
    property EndTimeUtc: TDateTime read GetEndTimeUtc;
    property SampleCount: Integer read GetSampleCount;
  end;

  // ── Timing-strategy bases ───────────────────────────────────────────────────

  // Stores one timestamp per sample. Used for irregularly sampled data.
  TOSFTimestampedDataChannel = class abstract(TOSFDataChannel)
  protected
    FTimestamps: TList<Int64>;
    procedure CopyTimingTo(Other: TOSFTimestampedDataChannel);
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function TimestampNsAt(Index: Integer): Int64; override;
    property Timestamps: TList<Int64> read FTimestamps;
  end;

  // Stores only a start timestamp and a fixed increment. Timestamps are
  // computed on demand: t_i = StartTimestampNs + i * TimeIncrementNs.
  //
  // Equidistant channels may consist of multiple segments separated by time
  // gaps — each bcStartData block opens a new segment, bcContinuedData blocks
  // append to the most recent one. The Segments list describes how the flat
  // Values list maps onto absolute time.
  TOSFEquidistantDataChannel = class abstract(TOSFDataChannel)
  protected
    FTimeIncrementNs: Int64;
    FSegments: TList<TOSFChannelSegment>;
    procedure CopyTimingTo(Other: TOSFEquidistantDataChannel);
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function TimestampNsAt(Index: Integer): Int64; override;

    // Opens a new segment starting at StartTimestampNs. The segment's
    // StartIndex is set to the channel's current sample count, SampleCount to 0.
    // Called by the data manager when a bcStartData block arrives.
    procedure BeginSegment(StartTimestampNs: Int64);
    // Bumps the most recent segment's SampleCount. Called by the data manager
    // after decoding a bcStartData or bcContinuedData block. No-op if Segments
    // is empty (defensive — should not happen with a well-formed file).
    procedure AppendToCurrentSegment(Count: Integer);

    property TimeIncrementNs: Int64 read FTimeIncrementNs;
    property Segments: TList<TOSFChannelSegment> read FSegments;
  end;

  // ── Concrete: Double (covers OSF dtDouble and dtFloat) ──────────────────────

  TOSFTimestampedDoubleChannel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<Double>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Double> read FValues;
  end;

  TOSFEquidistantDoubleChannel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<Double>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Double> read FValues;
  end;

  // ── Concrete: Int32 (covers dtInt8, dtInt16, dtInt32) ───────────────────────

  TOSFTimestampedInt32Channel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<Int32>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Int32> read FValues;
  end;

  TOSFEquidistantInt32Channel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<Int32>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Int32> read FValues;
  end;

  // ── Concrete: UInt32 (covers dtUInt8, dtUInt16, dtUInt32) ───────────────────

  TOSFTimestampedUInt32Channel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<UInt32>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<UInt32> read FValues;
  end;

  TOSFEquidistantUInt32Channel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<UInt32>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<UInt32> read FValues;
  end;

  // ── Concrete: Int64 ─────────────────────────────────────────────────────────

  TOSFTimestampedInt64Channel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<Int64>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function HasDoublePrecisionLoss: Boolean; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Int64> read FValues;
  end;

  TOSFEquidistantInt64Channel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<Int64>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function HasDoublePrecisionLoss: Boolean; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Int64> read FValues;
  end;

  // ── Concrete: UInt64 ────────────────────────────────────────────────────────

  TOSFTimestampedUInt64Channel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<UInt64>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function HasDoublePrecisionLoss: Boolean; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<UInt64> read FValues;
  end;

  TOSFEquidistantUInt64Channel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<UInt64>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function HasDoublePrecisionLoss: Boolean; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<UInt64> read FValues;
  end;

  // ── Concrete: Bool ──────────────────────────────────────────────────────────

  TOSFTimestampedBoolChannel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<Boolean>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Boolean> read FValues;
  end;

  TOSFEquidistantBoolChannel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<Boolean>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<Boolean> read FValues;
  end;

  // ── Concrete: String ────────────────────────────────────────────────────────

  TOSFTimestampedStringChannel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<string>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<string> read FValues;
  end;

  TOSFEquidistantStringChannel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<string>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<string> read FValues;
  end;

  // ── Concrete: Binary ────────────────────────────────────────────────────────

  TOSFTimestampedBinaryChannel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<TBytes>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<TBytes> read FValues;
  end;

  TOSFEquidistantBinaryChannel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<TBytes>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<TBytes> read FValues;
  end;

  // ── Concrete: GPS ───────────────────────────────────────────────────────────

  TOSFTimestampedGpsChannel = class(TOSFTimestampedDataChannel)
  private
    FValues: TList<TOSFGpsLocation>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<TOSFGpsLocation> read FValues;
  end;

  TOSFEquidistantGpsChannel = class(TOSFEquidistantDataChannel)
  private
    FValues: TList<TOSFGpsLocation>;
  protected
    function GetSampleCount: Integer; override;
  public
    constructor Create(ADef: TOSFChannelDef); override;
    destructor Destroy; override;
    function ValueAsDouble(Index: Integer): Double; override;
    function ValueAsString(Index: Integer): string; override;
    function Clone: TOSFDataChannel; override;
    procedure AddRawSample(TimestampNs: Int64; const RawBytes: TBytes); override;
    property Values: TList<TOSFGpsLocation> read FValues;
  end;

  // Picks the right concrete subclass based on Def.DataType and Def.IsEquidistant.
  // The returned channel keeps a non-owning reference to Def — the caller (typically
  // TOSFDataManager) is responsible for keeping the def alive at least as long as
  // the channel.
function CreateOSFDataChannel(Def: TOSFChannelDef): TOSFDataChannel;

implementation

const
  UNIX_EPOCH_DATETIME: TDateTime = 25569.0; // 1970-01-01 as TDateTime
  NS_PER_DAY: Double = 86400.0 * 1.0E9;

  // ── Local helpers ────────────────────────────────────────────────────────────

function NsToUtcDateTime(Ns: Int64): TDateTime;
begin
  // Double-precision arithmetic loses ~3 decimal digits at nanosecond scale
  // (~µs precision). That's fine for human-readable timestamps; sub-millisecond
  // work should use the StartTimestampNs / EndTimestampNs Int64 properties.
  Result := UNIX_EPOCH_DATETIME + Ns / NS_PER_DAY;
end;

function FormatDoubleInvariant(Value: Double): string;
begin
  Result := FloatToStr(Value, TFormatSettings.Invariant);
end;

// Decodes RawBytes into a Double according to the source OSF data type.
function DecodeAsDouble(const RawBytes: TBytes; OrigType: TOSFDataType): Double;
var
  SingleVal: Single;
begin
  Result := 0.0;
  case OrigType of
    dtDouble:
      if Length(RawBytes) >= 8 then
        Move(RawBytes[0], Result, 8);
    dtFloat:
      if Length(RawBytes) >= 4 then
      begin
        Move(RawBytes[0], SingleVal, 4);
        Result := SingleVal;
      end;
  end;
end;

// Decodes RawBytes into a signed 32-bit integer with sign extension from the
// source width (Int8, Int16, Int32).
function DecodeAsInt32(const RawBytes: TBytes; OrigType: TOSFDataType): Int32;
var
  V8: Int8;
  V16: Int16;
  V32: Int32;
begin
  Result := 0;
  case OrigType of
    dtInt8:
      if Length(RawBytes) >= 1 then
      begin
        Move(RawBytes[0], V8, 1);
        Result := V8;
      end;
    dtInt16:
      if Length(RawBytes) >= 2 then
      begin
        Move(RawBytes[0], V16, 2);
        Result := V16;
      end;
    dtInt32:
      if Length(RawBytes) >= 4 then
      begin
        Move(RawBytes[0], V32, 4);
        Result := V32;
      end;
  end;
end;

function DecodeAsUInt32(const RawBytes: TBytes; OrigType: TOSFDataType): UInt32;
var
  V8: UInt8;
  V16: UInt16;
  V32: UInt32;
begin
  Result := 0;
  case OrigType of
    dtUInt8:
      if Length(RawBytes) >= 1 then
      begin
        Move(RawBytes[0], V8, 1);
        Result := V8;
      end;
    dtUInt16:
      if Length(RawBytes) >= 2 then
      begin
        Move(RawBytes[0], V16, 2);
        Result := V16;
      end;
    dtUInt32:
      if Length(RawBytes) >= 4 then
      begin
        Move(RawBytes[0], V32, 4);
        Result := V32;
      end;
  end;
end;

function DecodeAsInt64(const RawBytes: TBytes): Int64;
begin
  Result := 0;
  if Length(RawBytes) >= 8 then
    Move(RawBytes[0], Result, 8);
end;

function DecodeAsUInt64(const RawBytes: TBytes): UInt64;
begin
  Result := 0;
  if Length(RawBytes) >= 8 then
    Move(RawBytes[0], Result, 8);
end;

function DecodeAsBoolean(const RawBytes: TBytes): Boolean;
begin
  Result := (Length(RawBytes) >= 1) and (RawBytes[0] <> 0);
end;

function DecodeAsString(const RawBytes: TBytes): string;
begin
  if Length(RawBytes) = 0 then
    Result := ''
  else
    Result := TEncoding.UTF8.GetString(RawBytes);
end;

function DecodeAsBinary(const RawBytes: TBytes): TBytes;
begin
  // Copy() on a dynamic array yields an independent copy.
  Result := Copy(RawBytes, 0, Length(RawBytes));
end;

function DecodeAsGpsLocation(const RawBytes: TBytes): TOSFGpsLocation;
begin
  FillChar(Result, SizeOf(Result), 0);
  if Length(RawBytes) >= SizeOf(Result) then
    Move(RawBytes[0], Result, SizeOf(Result));
end;

// ── TOSFDataChannel ──────────────────────────────────────────────────────────

constructor TOSFDataChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create;
  FChannelDef := ADef;
  FStartTimestampNs := 0;
  FEndTimestampNs := 0;
  FStartAssigned := False;
end;

function TOSFDataChannel.GetName: string;
begin
  if Assigned(FChannelDef) then
    Result := FChannelDef.Name
  else
    Result := '';
end;

function TOSFDataChannel.GetPhysicalUnit: string;
begin
  if Assigned(FChannelDef) then
    Result := FChannelDef.PhysicalUnit
  else
    Result := '';
end;

function TOSFDataChannel.GetComment: string;
begin
  if Assigned(FChannelDef) then
    Result := FChannelDef.Comment
  else
    Result := '';
end;

function TOSFDataChannel.GetMimeType: string;
begin
  if Assigned(FChannelDef) then
    Result := FChannelDef.MimeType
  else
    Result := '';
end;

function TOSFDataChannel.GetIsEquidistant: Boolean;
begin
  Result := Assigned(FChannelDef) and FChannelDef.IsEquidistant;
end;

function TOSFDataChannel.GetOriginalDataType: TOSFDataType;
begin
  if Assigned(FChannelDef) then
    Result := FChannelDef.DataType
  else
    Result := dtDouble;
end;

function TOSFDataChannel.GetStartTimeUtc: TDateTime;
begin
  Result := NsToUtcDateTime(FStartTimestampNs);
end;

function TOSFDataChannel.GetEndTimeUtc: TDateTime;
begin
  Result := NsToUtcDateTime(FEndTimestampNs);
end;

function TOSFDataChannel.TimestampUtcAt(Index: Integer): TDateTime;
begin
  Result := NsToUtcDateTime(TimestampNsAt(Index));
end;

function TOSFDataChannel.HasDoublePrecisionLoss: Boolean;
begin
  Result := False;
end;

procedure TOSFDataChannel.UpdateTimeRange(TimestampNs: Int64);
begin
  if not FStartAssigned then
  begin
    FStartTimestampNs := TimestampNs;
    FStartAssigned := True;
  end;
  FEndTimestampNs := TimestampNs;
end;

// ── TOSFTimestampedDataChannel ───────────────────────────────────────────────

constructor TOSFTimestampedDataChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FTimestamps := TList<Int64>.Create;
end;

destructor TOSFTimestampedDataChannel.Destroy;
begin
  FTimestamps.Free;
  inherited;
end;

function TOSFTimestampedDataChannel.TimestampNsAt(Index: Integer): Int64;
begin
  Result := FTimestamps[Index];
end;

procedure TOSFTimestampedDataChannel.CopyTimingTo(Other: TOSFTimestampedDataChannel);
begin
  Other.FTimestamps.AddRange(Self.FTimestamps);
  Other.FStartTimestampNs := Self.FStartTimestampNs;
  Other.FEndTimestampNs := Self.FEndTimestampNs;
  Other.FStartAssigned := Self.FStartAssigned;
end;

// ── TOSFEquidistantDataChannel ───────────────────────────────────────────────

constructor TOSFEquidistantDataChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FSegments := TList<TOSFChannelSegment>.Create;
  if Assigned(ADef) then
    FTimeIncrementNs := ADef.TimeIncrement
  else
    FTimeIncrementNs := 0;
  // OSF4 equidistant channels carry the start timestamp as an XML attribute;
  // when present, seed the range so TimestampNsAt(0) returns the right value
  // even before AddRawSample is called.
  if Assigned(ADef) and (ADef.StartTimestampNs > 0) then
  begin
    FStartTimestampNs := ADef.StartTimestampNs;
    FEndTimestampNs := ADef.StartTimestampNs;
    FStartAssigned := True;
  end;
end;

destructor TOSFEquidistantDataChannel.Destroy;
begin
  FSegments.Free;
  inherited;
end;

function TOSFEquidistantDataChannel.TimestampNsAt(Index: Integer): Int64;
begin
  Result := FStartTimestampNs + Int64(Index) * FTimeIncrementNs;
end;

procedure TOSFEquidistantDataChannel.BeginSegment(StartTimestampNs: Int64);
var
  Seg: TOSFChannelSegment;
begin
  Seg.StartTimestampNs := StartTimestampNs;
  Seg.StartIndex := GetSampleCount;
  Seg.SampleCount := 0;
  FSegments.Add(Seg);
end;

procedure TOSFEquidistantDataChannel.AppendToCurrentSegment(Count: Integer);
var
  Seg: TOSFChannelSegment;
  Idx: Integer;
begin
  if FSegments.Count = 0 then
    Exit;
  Idx := FSegments.Count - 1;
  Seg := FSegments[Idx];
  Inc(Seg.SampleCount, Count);
  FSegments[Idx] := Seg;
end;

procedure TOSFEquidistantDataChannel.CopyTimingTo(Other: TOSFEquidistantDataChannel);
var
  I: Integer;
begin
  Other.FStartTimestampNs := Self.FStartTimestampNs;
  Other.FEndTimestampNs := Self.FEndTimestampNs;
  Other.FStartAssigned := Self.FStartAssigned;
  Other.FTimeIncrementNs := Self.FTimeIncrementNs;
  Other.FSegments.Clear;
  Other.FSegments.Capacity := Self.FSegments.Count;
  for I := 0 to Self.FSegments.Count - 1 do
    Other.FSegments.Add(Self.FSegments[I]);
end;

// ── Double channels ──────────────────────────────────────────────────────────

constructor TOSFTimestampedDoubleChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Double>.Create;
end;

destructor TOSFTimestampedDoubleChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedDoubleChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedDoubleChannel.ValueAsDouble(Index: Integer): Double;
begin
  Result := FValues[Index];
end;

function TOSFTimestampedDoubleChannel.ValueAsString(Index: Integer): string;
begin
  Result := FormatDoubleInvariant(FValues[Index]);
end;

procedure TOSFTimestampedDoubleChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsDouble(RawBytes, OriginalDataType));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedDoubleChannel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedDoubleChannel;
begin
  C := TOSFTimestampedDoubleChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantDoubleChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Double>.Create;
end;

destructor TOSFEquidistantDoubleChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantDoubleChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantDoubleChannel.ValueAsDouble(Index: Integer): Double;
begin
  Result := FValues[Index];
end;

function TOSFEquidistantDoubleChannel.ValueAsString(Index: Integer): string;
begin
  Result := FormatDoubleInvariant(FValues[Index]);
end;

procedure TOSFEquidistantDoubleChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsDouble(RawBytes, OriginalDataType));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantDoubleChannel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantDoubleChannel;
begin
  C := TOSFEquidistantDoubleChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── Int32 channels ───────────────────────────────────────────────────────────

constructor TOSFTimestampedInt32Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Int32>.Create;
end;

destructor TOSFTimestampedInt32Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedInt32Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedInt32Channel.ValueAsDouble(Index: Integer): Double;
begin
  Result := FValues[Index];
end;

function TOSFTimestampedInt32Channel.ValueAsString(Index: Integer): string;
begin
  Result := IntToStr(FValues[Index]);
end;

procedure TOSFTimestampedInt32Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsInt32(RawBytes, OriginalDataType));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedInt32Channel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedInt32Channel;
begin
  C := TOSFTimestampedInt32Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantInt32Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Int32>.Create;
end;

destructor TOSFEquidistantInt32Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantInt32Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantInt32Channel.ValueAsDouble(Index: Integer): Double;
begin
  Result := FValues[Index];
end;

function TOSFEquidistantInt32Channel.ValueAsString(Index: Integer): string;
begin
  Result := IntToStr(FValues[Index]);
end;

procedure TOSFEquidistantInt32Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsInt32(RawBytes, OriginalDataType));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantInt32Channel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantInt32Channel;
begin
  C := TOSFEquidistantInt32Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── UInt32 channels ──────────────────────────────────────────────────────────

constructor TOSFTimestampedUInt32Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<UInt32>.Create;
end;

destructor TOSFTimestampedUInt32Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedUInt32Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedUInt32Channel.ValueAsDouble(Index: Integer): Double;
begin
  Result := FValues[Index];
end;

function TOSFTimestampedUInt32Channel.ValueAsString(Index: Integer): string;
begin
  Result := UIntToStr(FValues[Index]);
end;

procedure TOSFTimestampedUInt32Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsUInt32(RawBytes, OriginalDataType));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedUInt32Channel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedUInt32Channel;
begin
  C := TOSFTimestampedUInt32Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantUInt32Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<UInt32>.Create;
end;

destructor TOSFEquidistantUInt32Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantUInt32Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantUInt32Channel.ValueAsDouble(Index: Integer): Double;
begin
  Result := FValues[Index];
end;

function TOSFEquidistantUInt32Channel.ValueAsString(Index: Integer): string;
begin
  Result := UIntToStr(FValues[Index]);
end;

procedure TOSFEquidistantUInt32Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsUInt32(RawBytes, OriginalDataType));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantUInt32Channel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantUInt32Channel;
begin
  C := TOSFEquidistantUInt32Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── Int64 channels (precision-loss documented) ───────────────────────────────

constructor TOSFTimestampedInt64Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Int64>.Create;
end;

destructor TOSFTimestampedInt64Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedInt64Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedInt64Channel.HasDoublePrecisionLoss: Boolean;
begin
  Result := True;
end;

function TOSFTimestampedInt64Channel.ValueAsDouble(Index: Integer): Double;
begin
  // Double has only ~15.95 significant decimal digits. Int64 values whose
  // magnitude exceeds 2^53 (~9.0e15) cannot be represented exactly here.
  // HasDoublePrecisionLoss returns True so callers can warn the user.
  Result := FValues[Index];
end;

function TOSFTimestampedInt64Channel.ValueAsString(Index: Integer): string;
begin
  Result := IntToStr(FValues[Index]);
end;

procedure TOSFTimestampedInt64Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsInt64(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedInt64Channel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedInt64Channel;
begin
  C := TOSFTimestampedInt64Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantInt64Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Int64>.Create;
end;

destructor TOSFEquidistantInt64Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantInt64Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantInt64Channel.HasDoublePrecisionLoss: Boolean;
begin
  Result := True;
end;

function TOSFEquidistantInt64Channel.ValueAsDouble(Index: Integer): Double;
begin
  // See note on TOSFTimestampedInt64Channel.ValueAsDouble.
  Result := FValues[Index];
end;

function TOSFEquidistantInt64Channel.ValueAsString(Index: Integer): string;
begin
  Result := IntToStr(FValues[Index]);
end;

procedure TOSFEquidistantInt64Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsInt64(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantInt64Channel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantInt64Channel;
begin
  C := TOSFEquidistantInt64Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── UInt64 channels (precision-loss documented) ──────────────────────────────

constructor TOSFTimestampedUInt64Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<UInt64>.Create;
end;

destructor TOSFTimestampedUInt64Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedUInt64Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedUInt64Channel.HasDoublePrecisionLoss: Boolean;
begin
  Result := True;
end;

function TOSFTimestampedUInt64Channel.ValueAsDouble(Index: Integer): Double;
begin
  // Same precision caveat as Int64: values above 2^53 lose accuracy when
  // converted to Double. HasDoublePrecisionLoss returns True.
  Result := FValues[Index];
end;

function TOSFTimestampedUInt64Channel.ValueAsString(Index: Integer): string;
begin
  Result := UIntToStr(FValues[Index]);
end;

procedure TOSFTimestampedUInt64Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsUInt64(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedUInt64Channel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedUInt64Channel;
begin
  C := TOSFTimestampedUInt64Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantUInt64Channel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<UInt64>.Create;
end;

destructor TOSFEquidistantUInt64Channel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantUInt64Channel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantUInt64Channel.HasDoublePrecisionLoss: Boolean;
begin
  Result := True;
end;

function TOSFEquidistantUInt64Channel.ValueAsDouble(Index: Integer): Double;
begin
  // See note on TOSFTimestampedUInt64Channel.ValueAsDouble.
  Result := FValues[Index];
end;

function TOSFEquidistantUInt64Channel.ValueAsString(Index: Integer): string;
begin
  Result := UIntToStr(FValues[Index]);
end;

procedure TOSFEquidistantUInt64Channel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsUInt64(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantUInt64Channel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantUInt64Channel;
begin
  C := TOSFEquidistantUInt64Channel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── Bool channels ────────────────────────────────────────────────────────────

constructor TOSFTimestampedBoolChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Boolean>.Create;
end;

destructor TOSFTimestampedBoolChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedBoolChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedBoolChannel.ValueAsDouble(Index: Integer): Double;
begin
  if FValues[Index] then
    Result := 1.0
  else
    Result := 0.0;
end;

function TOSFTimestampedBoolChannel.ValueAsString(Index: Integer): string;
begin
  if FValues[Index] then
    Result := 'True'
  else
    Result := 'False';
end;

procedure TOSFTimestampedBoolChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsBoolean(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedBoolChannel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedBoolChannel;
begin
  C := TOSFTimestampedBoolChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantBoolChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<Boolean>.Create;
end;

destructor TOSFEquidistantBoolChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantBoolChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantBoolChannel.ValueAsDouble(Index: Integer): Double;
begin
  if FValues[Index] then
    Result := 1.0
  else
    Result := 0.0;
end;

function TOSFEquidistantBoolChannel.ValueAsString(Index: Integer): string;
begin
  if FValues[Index] then
    Result := 'True'
  else
    Result := 'False';
end;

procedure TOSFEquidistantBoolChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsBoolean(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantBoolChannel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantBoolChannel;
begin
  C := TOSFEquidistantBoolChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── String channels ──────────────────────────────────────────────────────────

constructor TOSFTimestampedStringChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<string>.Create;
end;

destructor TOSFTimestampedStringChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedStringChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedStringChannel.ValueAsDouble(Index: Integer): Double;
begin
  // Strings are not numeric. Callers use ValueAsString instead.
  Result := 0.0;
end;

function TOSFTimestampedStringChannel.ValueAsString(Index: Integer): string;
begin
  Result := FValues[Index];
end;

procedure TOSFTimestampedStringChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsString(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedStringChannel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedStringChannel;
begin
  C := TOSFTimestampedStringChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantStringChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<string>.Create;
end;

destructor TOSFEquidistantStringChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantStringChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantStringChannel.ValueAsDouble(Index: Integer): Double;
begin
  Result := 0.0;
end;

function TOSFEquidistantStringChannel.ValueAsString(Index: Integer): string;
begin
  Result := FValues[Index];
end;

procedure TOSFEquidistantStringChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsString(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantStringChannel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantStringChannel;
begin
  C := TOSFEquidistantStringChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── Binary channels ──────────────────────────────────────────────────────────

constructor TOSFTimestampedBinaryChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<TBytes>.Create;
end;

destructor TOSFTimestampedBinaryChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedBinaryChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedBinaryChannel.ValueAsDouble(Index: Integer): Double;
begin
  // Binary blobs are not numeric.
  Result := 0.0;
end;

function TOSFTimestampedBinaryChannel.ValueAsString(Index: Integer): string;
begin
  Result := Format('<%d bytes>', [Length(FValues[Index])]);
end;

procedure TOSFTimestampedBinaryChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsBinary(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedBinaryChannel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedBinaryChannel;
  I: Integer;
begin
  C := TOSFTimestampedBinaryChannel.Create(FChannelDef);
  CopyTimingTo(C);
  // TBytes is a dynamic array — AddRange would only copy references.
  // Deep copy each blob so the clone is fully independent.
  for I := 0 to Self.FValues.Count - 1 do
    C.FValues.Add(Copy(Self.FValues[I], 0, Length(Self.FValues[I])));
  Result := C;
end;

constructor TOSFEquidistantBinaryChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<TBytes>.Create;
end;

destructor TOSFEquidistantBinaryChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantBinaryChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantBinaryChannel.ValueAsDouble(Index: Integer): Double;
begin
  Result := 0.0;
end;

function TOSFEquidistantBinaryChannel.ValueAsString(Index: Integer): string;
begin
  Result := Format('<%d bytes>', [Length(FValues[Index])]);
end;

procedure TOSFEquidistantBinaryChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsBinary(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantBinaryChannel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantBinaryChannel;
  I: Integer;
begin
  C := TOSFEquidistantBinaryChannel.Create(FChannelDef);
  CopyTimingTo(C);
  for I := 0 to Self.FValues.Count - 1 do
    C.FValues.Add(Copy(Self.FValues[I], 0, Length(Self.FValues[I])));
  Result := C;
end;

// ── GPS channels ─────────────────────────────────────────────────────────────

constructor TOSFTimestampedGpsChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<TOSFGpsLocation>.Create;
end;

destructor TOSFTimestampedGpsChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFTimestampedGpsChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFTimestampedGpsChannel.ValueAsDouble(Index: Integer): Double;
begin
  // ValueAsDouble is meant for charting — Latitude is the most natural single-axis projection.
  Result := FValues[Index].Latitude;
end;

function TOSFTimestampedGpsChannel.ValueAsString(Index: Integer): string;
var
  G: TOSFGpsLocation;
begin
  G := FValues[Index];
  Result := Format('lat=%s lon=%s alt=%s', [FormatDoubleInvariant(G.Latitude), FormatDoubleInvariant(G.Longitude),
    FormatDoubleInvariant(G.Altitude)]);
end;

procedure TOSFTimestampedGpsChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FTimestamps.Add(TimestampNs);
  FValues.Add(DecodeAsGpsLocation(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFTimestampedGpsChannel.Clone: TOSFDataChannel;
var
  C: TOSFTimestampedGpsChannel;
begin
  C := TOSFTimestampedGpsChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

constructor TOSFEquidistantGpsChannel.Create(ADef: TOSFChannelDef);
begin
  inherited Create(ADef);
  FValues := TList<TOSFGpsLocation>.Create;
end;

destructor TOSFEquidistantGpsChannel.Destroy;
begin
  FValues.Free;
  inherited;
end;

function TOSFEquidistantGpsChannel.GetSampleCount: Integer;
begin
  Result := FValues.Count;
end;

function TOSFEquidistantGpsChannel.ValueAsDouble(Index: Integer): Double;
begin
  Result := FValues[Index].Latitude;
end;

function TOSFEquidistantGpsChannel.ValueAsString(Index: Integer): string;
var
  G: TOSFGpsLocation;
begin
  G := FValues[Index];
  Result := Format('lat=%s lon=%s alt=%s', [FormatDoubleInvariant(G.Latitude), FormatDoubleInvariant(G.Longitude),
    FormatDoubleInvariant(G.Altitude)]);
end;

procedure TOSFEquidistantGpsChannel.AddRawSample(TimestampNs: Int64; const RawBytes: TBytes);
begin
  FValues.Add(DecodeAsGpsLocation(RawBytes));
  UpdateTimeRange(TimestampNs);
end;

function TOSFEquidistantGpsChannel.Clone: TOSFDataChannel;
var
  C: TOSFEquidistantGpsChannel;
begin
  C := TOSFEquidistantGpsChannel.Create(FChannelDef);
  CopyTimingTo(C);
  C.FValues.AddRange(Self.FValues);
  Result := C;
end;

// ── Factory ──────────────────────────────────────────────────────────────────

function CreateOSFDataChannel(Def: TOSFChannelDef): TOSFDataChannel;
begin
  if Def.IsEquidistant then
  begin
    case Def.DataType of
      dtDouble, dtFloat:
        Result := TOSFEquidistantDoubleChannel.Create(Def);
      dtInt8, dtInt16, dtInt32:
        Result := TOSFEquidistantInt32Channel.Create(Def);
      dtUInt8, dtUInt16, dtUInt32:
        Result := TOSFEquidistantUInt32Channel.Create(Def);
      dtInt64:
        Result := TOSFEquidistantInt64Channel.Create(Def);
      dtUInt64:
        Result := TOSFEquidistantUInt64Channel.Create(Def);
      dtBool:
        Result := TOSFEquidistantBoolChannel.Create(Def);
      dtString:
        Result := TOSFEquidistantStringChannel.Create(Def);
      dtBinary:
        Result := TOSFEquidistantBinaryChannel.Create(Def);
      dtGpsLocation:
        Result := TOSFEquidistantGpsChannel.Create(Def);
    else
      Result := TOSFEquidistantDoubleChannel.Create(Def);
    end;
  end
  else
  begin
    case Def.DataType of
      dtDouble, dtFloat:
        Result := TOSFTimestampedDoubleChannel.Create(Def);
      dtInt8, dtInt16, dtInt32:
        Result := TOSFTimestampedInt32Channel.Create(Def);
      dtUInt8, dtUInt16, dtUInt32:
        Result := TOSFTimestampedUInt32Channel.Create(Def);
      dtInt64:
        Result := TOSFTimestampedInt64Channel.Create(Def);
      dtUInt64:
        Result := TOSFTimestampedUInt64Channel.Create(Def);
      dtBool:
        Result := TOSFTimestampedBoolChannel.Create(Def);
      dtString:
        Result := TOSFTimestampedStringChannel.Create(Def);
      dtBinary:
        Result := TOSFTimestampedBinaryChannel.Create(Def);
      dtGpsLocation:
        Result := TOSFTimestampedGpsChannel.Create(Def);
    else
      Result := TOSFTimestampedDoubleChannel.Create(Def);
    end;
  end;
end;

end.
