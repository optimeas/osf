// Copyright 2026 Optimeas GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

unit OSF.Channel;

interface

uses
  System.SysUtils,
  System.Classes,
  System.JSON,
  Xml.XMLIntf,
  OSF.Types;

type
  TOSFChannelDef = class
  private
    FIndex             : Integer;
    FName              : string;
    FDisplayName       : string;
    FReference         : string;
    FComment           : string;
    FDataIdentifier    : Integer;
    FTimeIncrement     : Int64;
    FStartTimestampNs  : Int64;
    FDataType          : TOSFDataType;
    FChannelType       : TOSFChannelType;
    FLengthFieldSize   : TOSFLengthFieldSize;
    FPhysicalUnit      : string;
    FPhysicalDimension : string;
    FMimeType          : string;
    FSpectrumType      : string;
    FLastTimestampNs   : Int64;
    FSampleCount       : Int64;
    FStartBlockWritten : Boolean;
    function  GetIsEquidistant: Boolean;
    function  GetIsTimestamped: Boolean;
  public
    constructor Create(AIndex: Integer; const AName: string;
                       AChannelType: TOSFChannelType; ADataType: TOSFDataType);

    // Factory: build from a parsed XML <channel> node (OSF4 meta block).
    class function FromXMLNode(Node: IXMLNode): TOSFChannelDef;
    // Factory: build from a parsed JSON channel object (OSF5 meta block).
    class function FromJSONObject(Obj: TJSONObject): TOSFChannelDef;

    // Serialize this channel as a <channel .../> XML line into Builder (OSF4 meta block).
    procedure AppendXML(Builder: TStringBuilder);
    // Serialize this channel as a JSON object appended to Arr (OSF5 meta block).
    procedure AppendJSON(Arr: TJSONArray);

    // Identity
    property Index             : Integer            read FIndex             write FIndex;
    property Name              : string             read FName              write FName;
    property DisplayName       : string             read FDisplayName       write FDisplayName;
    property Reference         : string             read FReference         write FReference;
    property Comment           : string             read FComment           write FComment;
    property DataIdentifier    : Integer            read FDataIdentifier    write FDataIdentifier;

    // Time basis. TimeIncrement > 0 = equidistant (nanoseconds per sample).
    // TimeIncrement = 0 means every sample carries its own absolute timestamp.
    // For OSF4 equidistant channels, set StartTimestampNs before WriteHeader so
    // the writer emits the starttime attribute. For OSF5 equidistant channels,
    // the start timestamp is embedded in the bcStartData block and need not be
    // pre-set.
    property TimeIncrement     : Int64              read FTimeIncrement     write FTimeIncrement;
    property StartTimestampNs  : Int64              read FStartTimestampNs  write FStartTimestampNs;
    property IsEquidistant     : Boolean            read GetIsEquidistant;
    property IsTimestamped     : Boolean            read GetIsTimestamped;

    // Data structure
    property DataType          : TOSFDataType       read FDataType          write FDataType;
    property ChannelType       : TOSFChannelType    read FChannelType       write FChannelType;
    property LengthFieldSize   : TOSFLengthFieldSize read FLengthFieldSize  write FLengthFieldSize;

    // Physical properties.
    property PhysicalUnit      : string             read FPhysicalUnit      write FPhysicalUnit;
    property PhysicalDimension : string             read FPhysicalDimension write FPhysicalDimension;
    property MimeType          : string             read FMimeType          write FMimeType;
    property SpectrumType      : string             read FSpectrumType      write FSpectrumType;

    // Read/write state — updated by TOSFFile during sequential I/O.
    property LastTimestampNs   : Int64              read FLastTimestampNs   write FLastTimestampNs;
    property SampleCount       : Int64              read FSampleCount       write FSampleCount;
    // Set to True by TOSFFile after the first bcStartData block is written for this channel.
    property StartBlockWritten : Boolean            read FStartBlockWritten write FStartBlockWritten;
  end;

implementation

// ── XML attribute helpers ────────────────────────────────────────────────────

function XMLAttrStr(Node: IXMLNode; const AttrName, Default: string): string;
begin
  if Node.HasAttribute(AttrName) then
    Result := Node.Attributes[AttrName]
  else
    Result := Default;
end;

function XMLAttrInt(Node: IXMLNode; const AttrName: string; Default: Integer): Integer;
begin
  if Node.HasAttribute(AttrName) then
    Result := StrToIntDef(Node.Attributes[AttrName], Default)
  else
    Result := Default;
end;

function XMLAttrInt64(Node: IXMLNode; const AttrName: string; Default: Int64): Int64;
begin
  if Node.HasAttribute(AttrName) then
    Result := StrToInt64Def(Node.Attributes[AttrName], Default)
  else
    Result := Default;
end;

function XMLEscape(const S: string): string;
begin
  Result := S;
  Result := StringReplace(Result, '&',  '&amp;',  [rfReplaceAll]);
  Result := StringReplace(Result, '"',  '&quot;', [rfReplaceAll]);
  Result := StringReplace(Result, '<',  '&lt;',   [rfReplaceAll]);
  Result := StringReplace(Result, '>',  '&gt;',   [rfReplaceAll]);
end;

// ── JSON helpers ─────────────────────────────────────────────────────────────

function JSONStr(Obj: TJSONObject; const Key, Default: string): string;
var
  Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  if Assigned(Val) then Result := Val.Value
  else                   Result := Default;
end;

function JSONInt(Obj: TJSONObject; const Key: string; Default: Integer): Integer;
var
  Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  if Assigned(Val) then Result := (Val as TJSONNumber).AsInt
  else                   Result := Default;
end;

function JSONInt64(Obj: TJSONObject; const Key: string; Default: Int64): Int64;
var
  Val: TJSONValue;
begin
  Val := Obj.GetValue(Key);
  // Use Val.Value (the raw JSON number string) to avoid Double precision loss
  // on large Int64 values such as nanosecond timestamps (~1.7e18).
  if Assigned(Val) then Result := StrToInt64Def(Val.Value, Default)
  else                   Result := Default;
end;

// ── TOSFChannelDef ────────────────────────────────────────────────────────────

constructor TOSFChannelDef.Create(AIndex: Integer; const AName: string;
                                   AChannelType: TOSFChannelType;
                                   ADataType: TOSFDataType);
begin
  inherited Create;
  FIndex             := AIndex;
  FName              := AName;
  FChannelType       := AChannelType;
  FDataType          := ADataType;
  FLengthFieldSize   := lfs2;
  FTimeIncrement     := 0;
  FStartTimestampNs  := 0;
  FDataIdentifier    := 0;
  FLastTimestampNs   := 0;
  FSampleCount       := 0;
  FStartBlockWritten := False;
end;

function TOSFChannelDef.GetIsEquidistant: Boolean;
begin
  Result := FTimeIncrement > 0;
end;

function TOSFChannelDef.GetIsTimestamped: Boolean;
begin
  Result := FTimeIncrement = 0;
end;

class function TOSFChannelDef.FromXMLNode(Node: IXMLNode): TOSFChannelDef;
var
  Ch: TOSFChannelDef;
begin
  Ch := TOSFChannelDef.Create(
    XMLAttrInt(Node, 'index', 0),
    XMLAttrStr(Node, 'name',  ''),
    OSFChannelTypeFromString(XMLAttrStr(Node, 'channeltype', 'scalar')),
    OSFDataTypeFromString(XMLAttrStr(Node, 'datatype', 'double'))
  );
  try
    Ch.TimeIncrement      := XMLAttrInt64(Node, 'timeincrement',     0);
    Ch.StartTimestampNs   := XMLAttrInt64(Node, 'starttime',         0);
    Ch.DataIdentifier     := XMLAttrInt  (Node, 'dataidentifier',    0);
    Ch.LengthFieldSize    := OSFLengthFieldSizeFromInt(
                               XMLAttrInt(Node, 'sizeoflengthvalue', OSF_DEFAULT_LENGTH_FIELD_SIZE));
    Ch.PhysicalUnit       := XMLAttrStr   (Node, 'physicalunit',       '');
    Ch.PhysicalDimension  := XMLAttrStr   (Node, 'physicaldimension',  '');
    Ch.MimeType           := XMLAttrStr   (Node, 'mimetype',            '');
    Ch.SpectrumType       := XMLAttrStr   (Node, 'spectrumtype',        '');
    Ch.DisplayName        := XMLAttrStr   (Node, 'displayname',         '');
    Ch.Reference          := XMLAttrStr   (Node, 'reference',           '');
    // OSF4 legacy uses the 'description' attribute; map to Comment if 'comment' is absent.
    Ch.Comment            := XMLAttrStr   (Node, 'comment',
                                XMLAttrStr(Node, 'description',         ''));

    Result := Ch;
  except
    Ch.Free;
    raise;
  end;
end;

class function TOSFChannelDef.FromJSONObject(Obj: TJSONObject): TOSFChannelDef;
var
  Ch: TOSFChannelDef;
begin
  Ch := TOSFChannelDef.Create(
    JSONInt(Obj, 'index', 0),
    JSONStr(Obj, 'name',  ''),
    OSFChannelTypeFromString(JSONStr(Obj, 'channeltype', 'scalar')),
    OSFDataTypeFromString(JSONStr(Obj, 'datatype', 'double'))
  );
  try
    Ch.TimeIncrement      := JSONInt64 (Obj, 'timeincrement',      0);
    Ch.StartTimestampNs   := JSONInt64 (Obj, 'starttime',          0);
    Ch.DataIdentifier     := JSONInt   (Obj, 'dataidentifier',     0);
    Ch.LengthFieldSize    := OSFLengthFieldSizeFromInt(
                               JSONInt(Obj, 'sizeoflengthvalue',   OSF_DEFAULT_LENGTH_FIELD_SIZE));
    Ch.PhysicalUnit       := JSONStr   (Obj, 'physicalunit',       '');
    Ch.PhysicalDimension  := JSONStr   (Obj, 'physicaldimension',  '');
    Ch.MimeType           := JSONStr   (Obj, 'mimetype',            '');
    Ch.SpectrumType       := JSONStr   (Obj, 'spectrumtype',        '');
    Ch.DisplayName        := JSONStr   (Obj, 'displayname',         '');
    Ch.Reference          := JSONStr   (Obj, 'reference',           '');
    Ch.Comment            := JSONStr   (Obj, 'comment',
                                JSONStr(Obj, 'description',         ''));

    Result := Ch;
  except
    Ch.Free;
    raise;
  end;
end;

procedure TOSFChannelDef.AppendXML(Builder: TStringBuilder);
begin
  Builder.Append('    <channel');
  Builder.AppendFormat(' index="%d"',             [FIndex]);
  Builder.AppendFormat(' name="%s"',              [XMLEscape(FName)]);
  Builder.AppendFormat(' channeltype="%s"',       [OSFChannelTypeToString(FChannelType)]);
  Builder.AppendFormat(' datatype="%s"',          [OSFDataTypeToString(FDataType)]);
  Builder.AppendFormat(' sizeoflengthvalue="%d"', [Ord(FLengthFieldSize)]);
  if FTimeIncrement > 0 then
    Builder.AppendFormat(' timeincrement="%d"', [FTimeIncrement]);
  if FStartTimestampNs > 0 then
    Builder.AppendFormat(' starttime="%d"', [FStartTimestampNs]);
  if FDataIdentifier <> 0 then
    Builder.AppendFormat(' dataidentifier="%d"', [FDataIdentifier]);
  if FPhysicalUnit <> '' then
    Builder.AppendFormat(' physicalunit="%s"', [XMLEscape(FPhysicalUnit)]);
  if FPhysicalDimension <> '' then
    Builder.AppendFormat(' physicaldimension="%s"', [XMLEscape(FPhysicalDimension)]);
  if FMimeType <> '' then
    Builder.AppendFormat(' mimetype="%s"', [XMLEscape(FMimeType)]);
  if FSpectrumType <> '' then
    Builder.AppendFormat(' spectrumtype="%s"', [XMLEscape(FSpectrumType)]);
  if FDisplayName <> '' then
    Builder.AppendFormat(' displayname="%s"', [XMLEscape(FDisplayName)]);
  if FReference <> '' then
    Builder.AppendFormat(' reference="%s"', [XMLEscape(FReference)]);
  if FComment <> '' then
    Builder.AppendFormat(' comment="%s"', [XMLEscape(FComment)]);
  Builder.Append('/>'#10);
end;

procedure TOSFChannelDef.AppendJSON(Arr: TJSONArray);
var
  Obj: TJSONObject;
begin
  Obj := TJSONObject.Create;
  Obj.AddPair('index',             TJSONNumber.Create(FIndex));
  Obj.AddPair('name',              FName);
  Obj.AddPair('channeltype',       OSFChannelTypeToString(FChannelType));
  Obj.AddPair('datatype',          OSFDataTypeToString(FDataType));
  Obj.AddPair('sizeoflengthvalue', TJSONNumber.Create(Ord(FLengthFieldSize)));
  if FTimeIncrement > 0 then
    Obj.AddPair('timeincrement', TJSONNumber.Create(FTimeIncrement));
  if FStartTimestampNs > 0 then
    Obj.AddPair('starttime', TJSONNumber.Create(FStartTimestampNs));
  if FDataIdentifier <> 0 then
    Obj.AddPair('dataidentifier', TJSONNumber.Create(FDataIdentifier));
  if FPhysicalUnit <> '' then
    Obj.AddPair('physicalunit', FPhysicalUnit);
  if FPhysicalDimension <> '' then
    Obj.AddPair('physicaldimension', FPhysicalDimension);
  if FMimeType <> '' then
    Obj.AddPair('mimetype', FMimeType);
  if FSpectrumType <> '' then
    Obj.AddPair('spectrumtype', FSpectrumType);
  if FDisplayName <> '' then
    Obj.AddPair('displayname', FDisplayName);
  if FReference <> '' then
    Obj.AddPair('reference', FReference);
  if FComment <> '' then
    Obj.AddPair('comment', FComment);
  Arr.AddElement(Obj);
end;

end.
