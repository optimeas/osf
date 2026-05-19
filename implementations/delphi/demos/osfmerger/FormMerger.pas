// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// VCL front-end for OSF.Merger. Lets the user pick a directory or an
// explicit file list, define a time interval and channel filter, scan,
// preview the result, and write it out as a single OSF file.
unit FormMerger;

interface

uses
  Winapi.Windows,
  Winapi.Messages,
  System.SysUtils,
  System.Classes,
  System.DateUtils,
  System.IOUtils,
  System.UITypes,
  Vcl.Controls,
  Vcl.Forms,
  Vcl.StdCtrls,
  Vcl.ExtCtrls,
  Vcl.ComCtrls,
  Vcl.Menus,
  Vcl.Dialogs,
  Vcl.FileCtrl,
  OSF.Types,
  OSF.Log,
  OSF.Data.Manager,
  OSF.Data.Channels,
  OSF.Meta.Cache,
  OSF.Merger;

type
  TFormMerger = class(TForm)
    mmMain: TMainMenu;
    miFile: TMenuItem;
    miExit: TMenuItem;
    miActions: TMenuItem;
    miActScan: TMenuItem;
    miActMerge: TMenuItem;
    miActSave: TMenuItem;

    pcSource: TPageControl;
    tsDirectory: TTabSheet;
    tsFileList: TTabSheet;

    gbRoot: TGroupBox;
    lblRoot: TLabel;
    edRootDir: TEdit;
    btBrowseRoot: TButton;

    gbInterval: TGroupBox;
    lblStart: TLabel;
    dtpStartDate: TDateTimePicker;
    dtpStartTime: TDateTimePicker;
    lblTo: TLabel;
    dtpEndDate: TDateTimePicker;
    dtpEndTime: TDateTimePicker;

    gbFoundFiles: TGroupBox;
    lvFiles: TListView;

    lbFiles: TListBox;
    pnlFileButtons: TPanel;
    btFilesAdd: TButton;
    btFilesAddDir: TButton;
    btFilesRemove: TButton;

    gbChannelFilter: TGroupBox;
    lblChannelFilter: TLabel;
    mChannelFilter: TMemo;

    gbOptions: TGroupBox;
    lblOverlap: TLabel;
    cbOverlap: TComboBox;
    lblOutputFmt: TLabel;
    cbOutputFmt: TComboBox;

    pnlActions: TPanel;
    btScan: TButton;
    btMerge: TButton;
    btSave: TButton;

    gbResult: TGroupBox;
    lvResult: TListView;

    pnlBottom: TPanel;
    cbDebug: TCheckBox;
    mLog: TMemo;
    sbStatus: TStatusBar;

    OpenFilesDialog: TOpenDialog;
    SaveOSFDialog: TSaveDialog;

    procedure FormCreate(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure miExitClick(Sender: TObject);
    procedure btBrowseRootClick(Sender: TObject);
    procedure btFilesAddClick(Sender: TObject);
    procedure btFilesAddDirClick(Sender: TObject);
    procedure btFilesRemoveClick(Sender: TObject);
    procedure btScanClick(Sender: TObject);
    procedure btMergeClick(Sender: TObject);
    procedure btSaveClick(Sender: TObject);
    procedure cbDebugClick(Sender: TObject);
  strict private
    FDataManager: TOSFDataManager;
    procedure HandleLog(Level: TOSFLogLevel; const Msg: string);
    procedure AppendLog(const Msg: string);
    function BuildMerger: TOSFMerger;
    function GetStartUtc: TDateTime;
    function GetEndUtc: TDateTime;
    procedure PopulateScanResult(const Entries: TArray<TOSFFileEntry>);
    procedure PopulateResultList;
    procedure SetStatus(const Msg: string);
  end;

var
  OsfMergerForm: TFormMerger;

implementation

{$R *.dfm}

const
  C_DEMO_VERSION = '0.1.0';
  C_TS_DISPLAY_FMT = 'yyyy-mm-dd"T"hh:nn:ss"Z"';

// ── Lifecycle ────────────────────────────────────────────────────────────────

procedure TFormMerger.FormCreate(Sender: TObject);
begin
  Caption := 'OSF Merger ' + C_DEMO_VERSION;
  FDataManager := nil;

  // Sensible defaults for the time pickers: today 00:00 .. today 23:59.
  dtpStartDate.Date := Date;
  dtpStartTime.Time := EncodeTime(0, 0, 0, 0);
  dtpEndDate.Date := Date;
  dtpEndTime.Time := EncodeTime(23, 59, 59, 0);

  cbOverlap.Items.Clear;
  cbOverlap.Items.Add('Skip');
  cbOverlap.Items.Add('Overwrite');
  cbOverlap.ItemIndex := 0;

  cbOutputFmt.Items.Clear;
  cbOutputFmt.Items.Add('OSF5');
  cbOutputFmt.Items.Add('OSF4');
  cbOutputFmt.ItemIndex := 0;

  btSave.Enabled := False;
  SetStatus('Ready.');
end;

procedure TFormMerger.FormDestroy(Sender: TObject);
begin
  FreeAndNil(FDataManager);
end;

procedure TFormMerger.miExitClick(Sender: TObject);
begin
  Close;
end;

// ── Logging ──────────────────────────────────────────────────────────────────

procedure TFormMerger.AppendLog(const Msg: string);
begin
  mLog.Lines.Add(Msg);
  mLog.Perform(WM_VSCROLL, SB_BOTTOM, 0);
end;

procedure TFormMerger.HandleLog(Level: TOSFLogLevel; const Msg: string);
const
  C_LEVEL: array[TOSFLogLevel] of string = ('DEBUG', 'INFO   ', 'WARNING', 'ERROR  ');
begin
  AppendLog(Format('[%s] %s', [C_LEVEL[Level], Msg]));
end;

procedure TFormMerger.cbDebugClick(Sender: TObject);
begin
  if cbDebug.Checked then
    AppendLog('[INFO   ] Debug output enabled.')
  else
    AppendLog('[INFO   ] Debug output disabled.');
end;

procedure TFormMerger.SetStatus(const Msg: string);
begin
  sbStatus.SimpleText := Msg;
end;

// ── Source selection helpers ─────────────────────────────────────────────────

procedure TFormMerger.btBrowseRootClick(Sender: TObject);
var
  Dir: string;
begin
  Dir := edRootDir.Text;
  if SelectDirectory('Select OSF root directory', '', Dir) then
    edRootDir.Text := Dir;
end;

procedure TFormMerger.btFilesAddClick(Sender: TObject);
var
  I: Integer;
begin
  if OpenFilesDialog.Execute then
    for I := 0 to OpenFilesDialog.Files.Count - 1 do
      if lbFiles.Items.IndexOf(OpenFilesDialog.Files[I]) < 0 then
        lbFiles.Items.Add(OpenFilesDialog.Files[I]);
end;

procedure TFormMerger.btFilesAddDirClick(Sender: TObject);
var
  Dir: string;
  Files: TArray<string>;
  F: string;
begin
  Dir := '';
  if not SelectDirectory('Select directory of OSF / OSFZ files', '', Dir) then
    Exit;
  Files := TDirectory.GetFiles(Dir, '*.osf*', TSearchOption.soAllDirectories);
  for F in Files do
    if SameText(ExtractFileExt(F), '.osf') or SameText(ExtractFileExt(F), '.osfz') then
      if lbFiles.Items.IndexOf(F) < 0 then
        lbFiles.Items.Add(F);
end;

procedure TFormMerger.btFilesRemoveClick(Sender: TObject);
var
  I: Integer;
begin
  for I := lbFiles.Items.Count - 1 downto 0 do
    if lbFiles.Selected[I] then
      lbFiles.Items.Delete(I);
end;

// ── Merger configuration ─────────────────────────────────────────────────────

function TFormMerger.GetStartUtc: TDateTime;
begin
  Result := Trunc(dtpStartDate.Date) + Frac(dtpStartTime.Time);
end;

function TFormMerger.GetEndUtc: TDateTime;
begin
  Result := Trunc(dtpEndDate.Date) + Frac(dtpEndTime.Time);
end;

function TFormMerger.BuildMerger: TOSFMerger;
var
  Filter: TArray<string>;
  Lines: TStringList;
  I: Integer;
begin
  Result := TOSFMerger.Create;
  try
    Result.OnLog := HandleLog;
    Result.DebugEnabled := cbDebug.Checked;

    if pcSource.ActivePage = tsDirectory then
      Result.RootDirectory := edRootDir.Text
    else
    begin
      SetLength(Filter, lbFiles.Items.Count);
      for I := 0 to lbFiles.Items.Count - 1 do
        Filter[I] := lbFiles.Items[I];
      Result.FileList := Filter;
    end;

    Result.SetInterval(GetStartUtc, GetEndUtc);

    // Channel filter from the memo: one channel name per line, trimmed,
    // empty lines dropped.
    Lines := TStringList.Create;
    try
      Lines.Text := mChannelFilter.Lines.Text;
      SetLength(Filter, 0);
      for I := 0 to Lines.Count - 1 do
        if Trim(Lines[I]) <> '' then
        begin
          SetLength(Filter, Length(Filter) + 1);
          Filter[High(Filter)] := Trim(Lines[I]);
        end;
      Result.ChannelFilter := Filter;
    finally
      Lines.Free;
    end;

    if cbOverlap.ItemIndex = 1 then
      Result.OverlapStrategy := osOverwrite
    else
      Result.OverlapStrategy := osSkip;

    if cbOutputFmt.ItemIndex = 1 then
      Result.OutputVersion := osvOSF4
    else
      Result.OutputVersion := osvOSF5;
  except
    Result.Free;
    raise;
  end;
end;

// ── Actions ──────────────────────────────────────────────────────────────────

procedure TFormMerger.PopulateScanResult(const Entries: TArray<TOSFFileEntry>);
var
  I: Integer;
  Item: TListItem;
  Cache: TOSFMetaCache;
begin
  lvFiles.Items.BeginUpdate;
  try
    lvFiles.Items.Clear;
    for I := 0 to High(Entries) do
    begin
      Cache := Entries[I].Cache;
      Item := lvFiles.Items.Add;
      Item.Caption := ExtractFileName(Entries[I].FilePath);
      Item.SubItems.Add(FormatDateTime(C_TS_DISPLAY_FMT, Cache.FirstTimestampUtc));
      Item.SubItems.Add(FormatDateTime(C_TS_DISPLAY_FMT, Cache.LastTimestampUtc));
      Item.SubItems.Add(IntToStr(Length(Cache.Channels)));
      Item.SubItems.Add('Yes');
    end;
  finally
    lvFiles.Items.EndUpdate;
  end;
end;

procedure TFormMerger.btScanClick(Sender: TObject);
var
  Merger: TOSFMerger;
  Entries: TArray<TOSFFileEntry>;
begin
  Merger := BuildMerger;
  try
    Entries := Merger.Scan;
    PopulateScanResult(Entries);
    SetStatus(Format('Scan: %d files in interval.', [Length(Entries)]));
  finally
    Merger.Free;
  end;
end;

procedure TFormMerger.PopulateResultList;
var
  I: Integer;
  Item: TListItem;
  Ch: TOSFDataChannel;
begin
  lvResult.Items.BeginUpdate;
  try
    lvResult.Items.Clear;
    if not Assigned(FDataManager) then
      Exit;
    for I := 0 to FDataManager.Channels.Count - 1 do
    begin
      Ch := FDataManager.Channels[I];
      Item := lvResult.Items.Add;
      Item.Caption := Ch.Name;
      Item.SubItems.Add(OSFDataTypeToString(Ch.OriginalDataType));
      Item.SubItems.Add(Ch.PhysicalUnit);
      Item.SubItems.Add(IntToStr(Ch.SampleCount));
      Item.SubItems.Add(FormatDateTime(C_TS_DISPLAY_FMT, Ch.StartTimeUtc));
      Item.SubItems.Add(FormatDateTime(C_TS_DISPLAY_FMT, Ch.EndTimeUtc));
    end;
  finally
    lvResult.Items.EndUpdate;
  end;
end;

procedure TFormMerger.btMergeClick(Sender: TObject);
var
  Merger: TOSFMerger;
  TotalSamples: Int64;
  I: Integer;
begin
  Merger := BuildMerger;
  try
    FreeAndNil(FDataManager);
    FDataManager := Merger.Merge;
    PopulateResultList;
    TotalSamples := 0;
    for I := 0 to FDataManager.Channels.Count - 1 do
      TotalSamples := TotalSamples + FDataManager.Channels[I].SampleCount;
    btSave.Enabled := True;
    SetStatus(Format('Merged: %d channels, %d samples total.',
      [FDataManager.Channels.Count, TotalSamples]));
  finally
    Merger.Free;
  end;
end;

procedure TFormMerger.btSaveClick(Sender: TObject);
var
  Merger: TOSFMerger;
begin
  if not SaveOSFDialog.Execute then
    Exit;
  Merger := BuildMerger;
  try
    Merger.SaveToFile(SaveOSFDialog.FileName);
    SetStatus('Written: ' + SaveOSFDialog.FileName);
    AppendLog('[INFO   ] Written: ' + SaveOSFDialog.FileName);
  finally
    Merger.Free;
  end;
end;

end.
