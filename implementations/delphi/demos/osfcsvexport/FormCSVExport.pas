// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

unit FormCSVExport;

interface

uses
  Winapi.Windows, Winapi.Messages,
  System.SysUtils, System.Classes, System.IOUtils, System.UITypes,
  Vcl.Graphics, Vcl.Controls, Vcl.Forms, Vcl.Dialogs,
  Vcl.Menus, Vcl.StdCtrls, Vcl.ExtCtrls, Vcl.ComCtrls,
  OSF.Types, OSF.Log,
  OSF.Channel, OSF.Data.Channels, OSF.Data.Manager,
  OSF.Export, OSF.Export.CSV;

type
  TFormCSVExport = class(TForm)
    MainMenu1: TMainMenu;
    miFile: TMenuItem;
    miFileOpen: TMenuItem;
    miFileExport: TMenuItem;
    miFileExit: TMenuItem;
    gbSource: TGroupBox;
    edSourceFile: TEdit;
    btBrowse: TButton;
    gbExportOptions: TGroupBox;
    cbExcludeEmpty: TCheckBox;
    cbAbsoluteTimestamp: TCheckBox;
    lblDecimal: TLabel;
    cbDecimalSep: TComboBox;
    lblEncoding: TLabel;
    cbEncoding: TComboBox;
    gbChannels: TGroupBox;
    lvChannels: TListView;
    btExport: TButton;
    cbDebug: TCheckBox;
    mLog: TMemo;
    StatusBar1: TStatusBar;
    OpenDialog: TOpenDialog;
    SaveDialog: TSaveDialog;
    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure miFileOpenClick(Sender: TObject);
    procedure miFileExportClick(Sender: TObject);
    procedure miFileExitClick(Sender: TObject);
    procedure btBrowseClick(Sender: TObject);
    procedure btExportClick(Sender: TObject);
    procedure cbExcludeEmptyClick(Sender: TObject);
    procedure cbDebugClick(Sender: TObject);
  private
    FDataManager : TOSFDataManager;
    FLastExporter: TOSFCSVExporter;   // kept around so cbDebugClick can flip
                                       // its DebugEnabled mid-run without
                                       // having to re-create
    FListener: TLoggerListener;

    procedure HandleLog(const Msg: string; Level: TOSFLogLevel;
                        const Sender: string);
    procedure DoLoadFile(const FileName: string);
    procedure DoExport(const FileName: string);
    procedure RefreshChannelList;
    procedure UpdateListenerLevel;

    function  ChosenDecimalSeparator: Char;
  end;

var
  FrmCSVExport: TFormCSVExport;

implementation

{$R *.dfm}

procedure TFormCSVExport.FormCreate(Sender: TObject);
begin
  FDataManager := TOSFDataManager.Create;
  FListener := TLoggerListener.Create;
  FListener.MinLevel := llUser;
  FListener.OnAddLogMessage := HandleLog;
  Logger.RegisterListener(FListener);

  // Defaults per the brief.
  cbExcludeEmpty.Checked      := True;
  cbAbsoluteTimestamp.Checked := True;
  cbDecimalSep.Items.Clear;
  cbDecimalSep.Items.Add('Comma (,)');
  cbDecimalSep.Items.Add('Dot (.)');
  cbDecimalSep.ItemIndex := 0;
  cbEncoding.Items.Clear;
  cbEncoding.Items.Add('ISO-8859-1');
  cbEncoding.Items.Add('UTF-8');
  cbEncoding.ItemIndex := 0;
  cbDebug.Checked      := False;

  // ListView columns.
  lvChannels.Columns.Clear;
  with lvChannels.Columns.Add do begin Caption := 'Name';      Width := 240; end;
  with lvChannels.Columns.Add do begin Caption := 'Unit';      Width := 60;  end;
  with lvChannels.Columns.Add do begin Caption := 'Samples';   Width := 80;  end;
  with lvChannels.Columns.Add do begin Caption := 'Data type'; Width := 100; end;
  with lvChannels.Columns.Add do begin Caption := 'Exported';  Width := 80;  end;
  lvChannels.ViewStyle := vsReport;
  lvChannels.ReadOnly  := True;

  OpenDialog.Filter      := 'OSF files (*.osf;*.osfz)|*.osf;*.osfz|All files (*.*)|*.*';
  SaveDialog.Filter      := 'CSV files (*.csv)|*.csv|All files (*.*)|*.*';
  SaveDialog.DefaultExt  := 'csv';

  StatusBar1.SimpleText := 'Ready';
end;

procedure TFormCSVExport.FormDestroy(Sender: TObject);
begin
  // FLastExporter is owned by DoExport's local lifetime — never destroy here.
  Logger.UnregisterListener(FListener);
  FListener.Free;
  FDataManager.Free;
end;

procedure TFormCSVExport.HandleLog(const Msg: string; Level: TOSFLogLevel;
  const Sender: string);
const
  LevelStr: array[TOSFLogLevel] of string = ('DEBUG', 'INFO ', 'USER ', 'WARN ', 'ERROR');
begin
  mLog.Lines.Add(Format('[%-5s] %s', [LevelStr[Level], Msg]));
  // Auto-scroll to bottom.
  mLog.Perform(WM_VSCROLL, SB_BOTTOM, 0);
end;

procedure TFormCSVExport.UpdateListenerLevel;
begin
  if Assigned(FListener) then
    if cbDebug.Checked then
      FListener.MinLevel := llDebug
    else
      FListener.MinLevel := llUser;
end;

// ── Loading ─────────────────────────────────────────────────────────────────

procedure TFormCSVExport.miFileOpenClick(Sender: TObject);
begin
  if OpenDialog.Execute then
    DoLoadFile(OpenDialog.FileName);
end;

procedure TFormCSVExport.btBrowseClick(Sender: TObject);
begin
  miFileOpenClick(Sender);
end;

procedure TFormCSVExport.DoLoadFile(const FileName: string);
begin
  Screen.Cursor := crHourGlass;
  try
    FDataManager.Clear;
    UpdateListenerLevel;
    try
      FDataManager.LoadFromFile(FileName);
      edSourceFile.Text := FileName;
      RefreshChannelList;
      btExport.Enabled       := True;
      miFileExport.Enabled   := True;
      StatusBar1.SimpleText  := Format('Loaded: %s  |  %d channels',
                                        [ExtractFileName(FileName),
                                         FDataManager.ChannelCount]);
    except
      on E: Exception do
      begin
        btExport.Enabled     := False;
        miFileExport.Enabled := False;
        edSourceFile.Text    := '';
        MessageDlg(E.Message, mtError, [mbOK], 0);
      end;
    end;
  finally
    Screen.Cursor := crDefault;
  end;
end;

procedure TFormCSVExport.RefreshChannelList;
var
  I       : Integer;
  Ch      : TOSFDataChannel;
  Item    : TListItem;
  Exported: string;
begin
  lvChannels.Items.BeginUpdate;
  try
    lvChannels.Items.Clear;
    for I := 0 to FDataManager.ChannelCount - 1 do
    begin
      Ch       := FDataManager.Channels[I];
      Item     := lvChannels.Items.Add;
      Item.Caption := Ch.Name;
      Item.SubItems.Add(Ch.PhysicalUnit);
      Item.SubItems.Add(IntToStr(Ch.SampleCount));
      Item.SubItems.Add(OSFDataTypeToString(Ch.OriginalDataType));
      if (not cbExcludeEmpty.Checked) or (Ch.SampleCount > 0) then
        Exported := 'Yes'
      else
        Exported := 'No';
      Item.SubItems.Add(Exported);
    end;
  finally
    lvChannels.Items.EndUpdate;
  end;
end;

procedure TFormCSVExport.cbExcludeEmptyClick(Sender: TObject);
begin
  // The flag may be toggled before any file is loaded — guard the refresh.
  if FDataManager.ChannelCount > 0 then
    RefreshChannelList;
end;

// ── Exporting ───────────────────────────────────────────────────────────────

procedure TFormCSVExport.miFileExportClick(Sender: TObject);
var
  Suggested: string;
begin
  if edSourceFile.Text = '' then Exit;
  Suggested := ChangeFileExt(ExtractFileName(edSourceFile.Text), '.csv');
  SaveDialog.FileName := Suggested;
  if SaveDialog.Execute then
    DoExport(SaveDialog.FileName);
end;

procedure TFormCSVExport.btExportClick(Sender: TObject);
begin
  miFileExportClick(Sender);
end;

procedure TFormCSVExport.DoExport(const FileName: string);
var
  Exporter: TOSFCSVExporter;
begin
  Screen.Cursor := crHourGlass;
  Exporter := TOSFCSVExporter.Create(FDataManager);
  try
    Exporter.ExcludeEmptyChannels := cbExcludeEmpty.Checked;
    Exporter.AbsoluteTimestamps   := cbAbsoluteTimestamp.Checked;
    Exporter.DecimalSeparator     := ChosenDecimalSeparator;
    // Encoding default is ISO-8859-1 (constructed and owned by the exporter).
    // Only override it for UTF-8, where TEncoding.UTF8 is a singleton and the
    // exporter's setter correctly skips freeing it.
    if cbEncoding.ItemIndex = 1 then
      Exporter.Encoding := TEncoding.UTF8;
    FLastExporter := Exporter;
    try
      Exporter.Export(FileName);
      StatusBar1.SimpleText := 'Export complete: ' + ExtractFileName(FileName);
      ShowMessage('Export complete.'#13#10 + FileName);
    except
      on E: Exception do
      begin
        StatusBar1.SimpleText := 'Export failed: ' + E.Message;
        MessageDlg(E.Message, mtError, [mbOK], 0);
      end;
    end;
  finally
    FLastExporter := nil;
    Exporter.Free;
    Screen.Cursor := crDefault;
  end;
end;

procedure TFormCSVExport.cbDebugClick(Sender: TObject);
begin
  UpdateListenerLevel;
end;

procedure TFormCSVExport.miFileExitClick(Sender: TObject);
begin
  Close;
end;

// ── Option helpers ──────────────────────────────────────────────────────────

function TFormCSVExport.ChosenDecimalSeparator: Char;
begin
  if cbDecimalSep.ItemIndex = 1 then
    Result := '.'
  else
    Result := ',';
end;

end.
