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

// VCL demo: opens an OSF file via TOSFDataManager, shows channel metadata in
// a list box, and renders the selected channel in a TChart.
unit FormMain;

interface

uses
  Winapi.Windows,
  Winapi.Messages,
  System.SysUtils,
  System.Classes,
  System.UITypes,
  Vcl.Graphics,
  Vcl.Controls,
  Vcl.Forms,
  Vcl.Dialogs,
  Vcl.StdCtrls,
  Vcl.ExtCtrls,
  Vcl.Menus,
  VCLTee.TeEngine,
  VCLTee.Series,
  VCLTee.TeeProcs,
  VCLTee.Chart,
  OSF.Types,
  OSF.Log,
  OSF.Data.Manager,
  OSF.Data.Channels;

type
  TFormOSFViewer = class(TForm)
    MainMenu1: TMainMenu;
    miFile: TMenuItem;
    miOpen: TMenuItem;
    miSep1: TMenuItem;
    miExit: TMenuItem;
    OpenDialog1: TOpenDialog;
    pnlLeft: TPanel;
    lblChannels: TLabel;
    lbChannels: TListBox;
    splVert: TSplitter;
    pnlRight: TPanel;
    chtData: TChart;
    lblNoChart: TLabel;
    splHorz: TSplitter;
    pnlBottomLog: TPanel;
    memLog: TMemo;
    Panel1: TPanel;
    cbDebug: TCheckBox;
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure miOpenClick(Sender: TObject);
    procedure miExitClick(Sender: TObject);
    procedure lbChannelsClick(Sender: TObject);
    procedure lbChannelsDrawItem(Control: TWinControl; Index: Integer; Rect: TRect; State: TOwnerDrawState);
    procedure cbDebugClick(Sender: TObject);
    procedure FormShow(Sender: TObject);
  private
    FDataManager: TOSFDataManager;

    procedure HandleManagerLog(Level: TOSFLogLevel; const Msg: string);
    procedure AppendLogLine(Level: TOSFLogLevel; const Msg: string);
    procedure LoadFile(const FileName: string);
    procedure PopulateChannelList;
    procedure ShowChannel(Index: Integer);
    procedure ClearChartSeries;
    procedure SetNoChartMode(NoChart: Boolean);
    procedure TryLoadDefaultExample;
    function IsChartableType(DT: TOSFDataType): Boolean;
  end;

var
  FormOSFViewer: TFormOSFViewer;

implementation

{$R *.dfm}

const
  // Window title used while no file is loaded.
  WINDOW_TITLE_EMPTY = 'OSF Viewer — no file loaded';
  WINDOW_TITLE_PREFIX = 'OSF Viewer — ';
  WINDOW_TITLE_BASE = 'OSF Viewer';

  // Decimation kicks in above this sample count so the chart stays responsive.
  CHART_MAX_POINTS = 10000;

  // Two locations for the bundled demo file: the spec calls for 'generated/'
  // but the repo currently only has the flat path. Try both at startup.
  DEFAULT_EXAMPLE_REL_GENERATED = '..\..\..\..\examples\generated\steam_loco.osf';
  DEFAULT_EXAMPLE_REL_FLAT = '..\..\..\..\examples\steam_loco.osf';

  // Custom orange — Vcl.Graphics has no clOrange constant.
  CHART_ORANGE: TColor = TColor($000080FF);

resourcestring
  // Log prefixes — padded to 9 characters so all messages line up nicely.
  SLogPrefixDebug = '[DEBUG]';
  SLogPrefixInfo = '[INFO]';
  SLogPrefixWarning = '[WARNING]';
  SLogPrefixError = '[ERROR]';

  // List-box rendering — empty unit fallback drops the trailing field.
  SListItemWithUnit = '%s  [%d samples, %s]';
  SListItemWithoutUnit = '%s  [%d samples]';

  // Decimation log message produced by the form itself.
  SDecimationApplied = 'Channel %s decimated: %d samples shown as %d points (every %dth)';

  // Error dialog when the load itself raises.
  SLoadFailed = 'Failed to load file:%s%s';

  // Title shown when a non-chartable type is selected.
  SNoChartMessage = 'Channel type cannot be displayed as a chart';

procedure TFormOSFViewer.FormCreate(Sender: TObject);
begin
  FDataManager := TOSFDataManager.Create;
  FDataManager.OnLog := HandleManagerLog;
  FDataManager.DebugEnabled := cbDebug.Checked;

  Caption := WINDOW_TITLE_BASE;
  lblNoChart.Caption := SNoChartMessage;
  lblNoChart.Visible := False;

  // Leave SetNoChartMode(False) implicit — chtData starts visible.
  TryLoadDefaultExample;
end;

procedure TFormOSFViewer.FormDestroy(Sender: TObject);
begin
  FDataManager.Free;
end;

procedure TFormOSFViewer.FormShow(Sender: TObject);
begin
  // Auto-scroll the memo so the most recent line is always visible.
  memLog.SelStart := Length(memLog.Text);
  memLog.SelLength := 0;
end;

procedure TFormOSFViewer.TryLoadDefaultExample;
var
  ExeDir: string;
  Path: string;
begin
  ExeDir := ExtractFilePath(ParamStr(0));
  Path := ExeDir + DEFAULT_EXAMPLE_REL_GENERATED;
  if not FileExists(Path) then
    Path := ExeDir + DEFAULT_EXAMPLE_REL_FLAT;
  if FileExists(Path) then
    LoadFile(Path)
  else
    Caption := WINDOW_TITLE_EMPTY;
end;

// ── Menu actions ────────────────────────────────────────────────────────────

procedure TFormOSFViewer.miOpenClick(Sender: TObject);
begin
  if OpenDialog1.Execute then
    LoadFile(OpenDialog1.FileName);
end;

procedure TFormOSFViewer.miExitClick(Sender: TObject);
begin
  Close;
end;

procedure TFormOSFViewer.cbDebugClick(Sender: TObject);
begin
  FDataManager.DebugEnabled := cbDebug.Checked;
end;

// ── Loading / list population ───────────────────────────────────────────────

procedure TFormOSFViewer.LoadFile(const FileName: string);
begin
  try
    FDataManager.LoadFromFile(FileName);
    PopulateChannelList;
    Caption := WINDOW_TITLE_PREFIX + ExtractFileName(FileName);
    if lbChannels.Count > 0 then
    begin
      lbChannels.ItemIndex := 0;
      ShowChannel(0);
    end;
  except
    on E: Exception do
    begin
      AppendLogLine(llError, Format('LoadFile: %s', [E.Message]));
      MessageDlg(Format(SLoadFailed, [sLineBreak, E.Message]), mtError, [mbOK], 0);
    end;
  end;
end;

procedure TFormOSFViewer.PopulateChannelList;
var
  I: Integer;
  Ch: TOSFDataChannel;
  Item: string;
begin
  lbChannels.Items.BeginUpdate;
  try
    lbChannels.Clear;
    // Items.AddObject stashes the channel reference next to the displayed
    // text, so when the list is sorted (Sorted = True) the position no longer
    // matches FDataManager.Channels[I] but Items.Objects[I] still resolves
    // to the right channel.
    for I := 0 to FDataManager.ChannelCount - 1 do
    begin
      Ch := FDataManager.Channels[I];
      if Ch.PhysicalUnit <> '' then
        Item := Format(SListItemWithUnit, [Ch.Name, Ch.SampleCount, Ch.PhysicalUnit])
      else
        Item := Format(SListItemWithoutUnit, [Ch.Name, Ch.SampleCount]);
      lbChannels.Items.AddObject(Item, Ch);
    end;
  finally
    lbChannels.Items.EndUpdate;
  end;
end;

// Returns the data channel rendered at the given list-box position, or nil
// when the position is out of range. Centralised so every callsite resolves
// the sorted display order back to the underlying channel the same way.
function ChannelAtListIndex(LB: TListBox; Index: Integer): TOSFDataChannel;
begin
  if (Index < 0) or (Index >= LB.Count) then
    Result := nil
  else
    Result := TOSFDataChannel(LB.Items.Objects[Index]);
end;

// Owner-draw renders empty channels in clGrayText so the user immediately
// sees which channels carry no samples.
procedure TFormOSFViewer.lbChannelsDrawItem(Control: TWinControl; Index: Integer; Rect: TRect; State: TOwnerDrawState);
var
  LB: TListBox;
  Cnv: TCanvas;
  Ch: TOSFDataChannel;
  IsEmpty: Boolean;
begin
  LB := Control as TListBox;
  Cnv := LB.Canvas;
  Cnv.FillRect(Rect);

  Ch := ChannelAtListIndex(LB, Index);
  if not Assigned(Ch) then Exit;
  IsEmpty := Ch.SampleCount = 0;

  if odSelected in State then
    Cnv.Font.Color := clHighlightText
  else if IsEmpty then
    Cnv.Font.Color := clGrayText
  else
    Cnv.Font.Color := clWindowText;

  Cnv.TextOut(Rect.Left + 4, Rect.Top + 1, LB.Items[Index]);
end;

procedure TFormOSFViewer.lbChannelsClick(Sender: TObject);
begin
  if lbChannels.ItemIndex >= 0 then
    ShowChannel(lbChannels.ItemIndex);
end;

// ── Chart rendering ─────────────────────────────────────────────────────────

function TFormOSFViewer.IsChartableType(DT: TOSFDataType): Boolean;
begin
  // Non-numeric and structured types — ValueAsDouble is not meaningful
  // for plotting, so we show a placeholder label instead of an empty chart.
  Result := not(DT in [dtString, dtBinary, dtGpsData]);
end;

procedure TFormOSFViewer.ClearChartSeries;
begin
  while chtData.SeriesCount > 0 do
    chtData.Series[0].Free;
end;

procedure TFormOSFViewer.SetNoChartMode(NoChart: Boolean);
begin
  if NoChart then
  begin
    chtData.Visible := False;
    lblNoChart.Visible := True;
    lblNoChart.BringToFront;
  end
  else
  begin
    lblNoChart.Visible := False;
    chtData.Visible := True;
    chtData.BringToFront;
  end;
end;

procedure TFormOSFViewer.ShowChannel(Index: Integer);
const
  ChartColors: array [0 .. 5] of TColor = (clBlue, clRed, clGreen, clPurple, $000080FF, clTeal);
var
  Ch: TOSFDataChannel;
  Series: TLineSeries;
  Color: TColor;
  I: Integer;
  Step: Integer;
  Drawn: Integer;
begin
  // Index is the list-box position which is now sorted alphabetically and
  // therefore no longer matches FDataManager.Channels[I]. The channel
  // reference was stashed via AddObject in PopulateChannelList.
  Ch := ChannelAtListIndex(lbChannels, Index);
  if not Assigned(Ch) then Exit;

  ClearChartSeries;
  chtData.Title.Text.Text := Ch.Name;

  if not IsChartableType(Ch.OriginalDataType) then
  begin
    SetNoChartMode(True);
    Exit;
  end;

  SetNoChartMode(False);

  Color := ChartColors[Index mod Length(ChartColors)];

  Series := TLineSeries.Create(chtData);
  chtData.AddSeries(Series);
  Series.Title := Ch.Name;
  Series.XValues.DateTime := True;
  Series.LinePen.Color := Color;
  Series.SeriesColor := Color;
  Series.Pointer.Visible := False;

  if Ch.PhysicalUnit <> '' then
    chtData.LeftAxis.Title.Caption := Ch.PhysicalUnit
  else
    chtData.LeftAxis.Title.Caption := '';

  // Chart auto-detects the X axis as date/time when AddXY receives TDateTime
  // values, but make the bottom axis format explicit so the labels look
  // sensible regardless of zoom level.
  chtData.BottomAxis.DateTimeFormat := 'yyyy-mm-dd hh:nn:ss';

  // Decimation: very dense channels are downsampled so the chart stays
  // responsive. The precise threshold matches the spec's CHART_MAX_POINTS.
  if Ch.SampleCount > CHART_MAX_POINTS then
    Step := Ch.SampleCount div CHART_MAX_POINTS
  else
    Step := 1;

  Drawn := 0;
  I := 0;
  while I < Ch.SampleCount do
  begin
    Series.AddXY(Ch.TimestampUtcAt(I), Ch.ValueAsDouble(I));
    Inc(Drawn);
    Inc(I, Step);
  end;

  if Step > 1 then
    AppendLogLine(llWarning, Format(SDecimationApplied, [Ch.Name, Ch.SampleCount, Drawn, Step]));
end;

// ── Logging ─────────────────────────────────────────────────────────────────

procedure TFormOSFViewer.HandleManagerLog(Level: TOSFLogLevel; const Msg: string);
begin
  AppendLogLine(Level, Msg);
end;

procedure TFormOSFViewer.AppendLogLine(Level: TOSFLogLevel; const Msg: string);
const
  // %-9s pads the prefix to 9 characters (the longest, '[WARNING]', is 9).
  ROW_FMT = '%-9s %s';
var
  Prefix: string;
begin
  case Level of
    llDebug:
      Prefix := SLogPrefixDebug;
    llInfo:
      Prefix := SLogPrefixInfo;
    llWarning:
      Prefix := SLogPrefixWarning;
    llError:
      Prefix := SLogPrefixError;
  else
    Prefix := SLogPrefixInfo;
  end;
  memLog.Lines.Add(Format(ROW_FMT, [Prefix, Msg]));
  // Auto-scroll the memo so the most recent line is always visible.
  memLog.SelStart := Length(memLog.Text);
  memLog.SelLength := 0;
end;

end.
