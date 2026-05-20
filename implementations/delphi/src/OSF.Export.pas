// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Abstract base class for file exporters that read from a TOSFDataManager.
// Concrete subclasses (e.g. TOSFCSVExporter) override DoExport to render the
// active channels in a specific format. Export() wraps DoExport with
// info/error logging and lets subclasses focus on the format itself.
unit OSF.Export;

interface

uses
  System.SysUtils,
  OSF.Types,
  OSF.Log,
  OSF.Data.Manager,
  OSF.Data.Channels;

type
  TOSFExporter = class(TOSFLoggable)
  private
    FDataManager          : TOSFDataManager;
    FExcludeEmptyChannels : Boolean;
    FAbsoluteTimestamps   : Boolean;
  protected
    // Returns the channels that should appear in the exported output.
    // Filters out empty channels (SampleCount = 0) when ExcludeEmptyChannels
    // is True. Channel order is preserved.
    function ActiveChannels: TArray<TOSFDataChannel>;

    // Concrete subclasses override this and write the file. Export() takes
    // care of the surrounding info/error logging.
    procedure DoExport(const FileName: string); virtual; abstract;
  public
    constructor Create(DataManager: TOSFDataManager);

    // Drives DoExport with logging:
    //   • Logs llInfo before and after a successful export.
    //   • Logs llError on exception and re-raises so the caller can react.
    procedure Export(const FileName: string);

    property DataManager: TOSFDataManager read FDataManager;
    // Skip channels with SampleCount = 0 when building ActiveChannels.
    // Default: True.
    property ExcludeEmptyChannels: Boolean
      read FExcludeEmptyChannels write FExcludeEmptyChannels;
    // Hint to subclasses: emit absolute (UTC) timestamps rather than
    // file-relative offsets. Default: True. Whether and how it is honoured
    // is up to the concrete exporter.
    property AbsoluteTimestamps: Boolean
      read FAbsoluteTimestamps write FAbsoluteTimestamps;
  end;

resourcestring
  // Log messages emitted by Export() before, after and on failure.
  SOSFLogExportStarted  = 'Export started: %s';
  SOSFLogExportFinished = 'Export finished: %s';
  SOSFLogExportFailed   = 'Export failed: %s - %s';

implementation

constructor TOSFExporter.Create(DataManager: TOSFDataManager);
begin
  inherited Create;
  FDataManager          := DataManager;
  FExcludeEmptyChannels := True;
  FAbsoluteTimestamps   := True;
end;

function TOSFExporter.ActiveChannels: TArray<TOSFDataChannel>;
var
  I, Cnt : Integer;
  Ch     : TOSFDataChannel;
  Keep   : Boolean;
begin
  if not Assigned(FDataManager) then
  begin
    SetLength(Result, 0);
    Exit;
  end;
  // Pre-size to the maximum so we can fill in one pass without reallocation.
  SetLength(Result, FDataManager.ChannelCount);
  Cnt := 0;
  for I := 0 to FDataManager.ChannelCount - 1 do
  begin
    Ch   := FDataManager.Channels[I];
    Keep := (not FExcludeEmptyChannels) or (Ch.SampleCount > 0);
    if Keep then
    begin
      Result[Cnt] := Ch;
      Inc(Cnt);
    end;
  end;
  SetLength(Result, Cnt);
end;

procedure TOSFExporter.Export(const FileName: string);
begin
  Log(llInfo, SOSFLogExportStarted, [FileName]);
  try
    DoExport(FileName);
    Log(llInfo, SOSFLogExportFinished, [FileName]);
  except
    on E: Exception do
    begin
      Log(llError, SOSFLogExportFailed, [FileName, E.Message]);
      raise;
    end;
  end;
end;

end.
