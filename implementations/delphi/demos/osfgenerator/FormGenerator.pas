// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

unit FormGenerator;

interface

uses
  Winapi.Windows, Winapi.Messages,
  System.SysUtils, System.Classes, System.IOUtils,
  Vcl.Graphics, Vcl.Controls, Vcl.Forms, Vcl.Dialogs,
  Vcl.Menus, Vcl.StdCtrls, Vcl.ExtCtrls, Vcl.ComCtrls, Vcl.Samples.Spin,
  Vcl.FileCtrl,
  OSF.Types, OSF.Log,
  OSFDemoGenerator;

type
  TFormGenerator = class(TForm)
    MainMenu1: TMainMenu;
    miFile: TMenuItem;
    miFileExit: TMenuItem;
    miGenerate: TMenuItem;
    miGenerateAll: TMenuItem;
    miSep1: TMenuItem;
    miGenOSF4: TMenuItem;
    miGenOSF5: TMenuItem;
    gbOutput: TGroupBox;
    edOutputDir: TEdit;
    btBrowse: TButton;
    gbOptions: TGroupBox;
    cbOSF4: TCheckBox;
    cbOSF5: TCheckBox;
    lblSampleCount: TLabel;
    spSampleCount: TSpinEdit;
    btGenerate: TButton;
    mLog: TMemo;
    StatusBar1: TStatusBar;
    procedure FormCreate(Sender: TObject);
    procedure btBrowseClick(Sender: TObject);
    procedure btGenerateClick(Sender: TObject);
    procedure miFileExitClick(Sender: TObject);
    procedure miGenerateAllClick(Sender: TObject);
    procedure miGenOSF4Click(Sender: TObject);
    procedure miGenOSF5Click(Sender: TObject);
  private
    FFileCount: Integer;

    function  ResolveDefaultOutputDir: string;
    procedure HandleLog(const Msg: string; Level: TOSFLogLevel;
                        const Sender: string);
    procedure RunGenerator(IncludeOSF4, IncludeOSF5: Boolean);
  end;

var
  FrmGenerator: TFormGenerator;

implementation

{$R *.dfm}

const
  EXAMPLES_REL = '..\..\..\..\examples\generated';

procedure TFormGenerator.FormCreate(Sender: TObject);
begin
  edOutputDir.Text := ResolveDefaultOutputDir;
  FFileCount       := 0;
  StatusBar1.SimpleText := 'Ready';
end;

function TFormGenerator.ResolveDefaultOutputDir: string;
begin
  Result := TPath.GetFullPath(
    TPath.Combine(ExtractFilePath(ParamStr(0)), EXAMPLES_REL));
end;

procedure TFormGenerator.btBrowseClick(Sender: TObject);
var
  Dir: string;
begin
  Dir := edOutputDir.Text;
  if SelectDirectory('Select output directory', '', Dir,
                     [sdNewUI, sdNewFolder]) then
    edOutputDir.Text := Dir;
end;

procedure TFormGenerator.miFileExitClick(Sender: TObject);
begin
  Close;
end;

procedure TFormGenerator.btGenerateClick(Sender: TObject);
begin
  RunGenerator(cbOSF4.Checked, cbOSF5.Checked);
end;

procedure TFormGenerator.miGenerateAllClick(Sender: TObject);
begin
  RunGenerator(cbOSF4.Checked, cbOSF5.Checked);
end;

procedure TFormGenerator.miGenOSF4Click(Sender: TObject);
begin
  RunGenerator(True, False);
end;

procedure TFormGenerator.miGenOSF5Click(Sender: TObject);
begin
  RunGenerator(False, True);
end;

procedure TFormGenerator.HandleLog(const Msg: string; Level: TOSFLogLevel;
  const Sender: string);
const
  LevelStr: array[TOSFLogLevel] of string = ('DEBUG', 'INFO ', 'USER ', 'WARN ', 'ERROR');
begin
  mLog.Lines.Add(Format('[%-5s] %s', [LevelStr[Level], Msg]));
  // Auto-scroll to bottom.
  mLog.Perform(WM_VSCROLL, SB_BOTTOM, 0);

  if Level = llInfo then
  begin
    Inc(FFileCount);
    StatusBar1.SimpleText := Format('Last: %s  |  total messages: %d',
                                     [Msg, FFileCount]);
  end;
end;

procedure TFormGenerator.RunGenerator(IncludeOSF4, IncludeOSF5: Boolean);
var
  Gen     : TOSFDemoGenerator;
  Listener: TLoggerListener;
  OutDir  : string;
  Samples : Integer;
begin
  if not (IncludeOSF4 or IncludeOSF5) then
  begin
    mLog.Lines.Add('[WARNING] Neither OSF4 nor OSF5 selected — nothing to do.');
    Exit;
  end;

  OutDir  := edOutputDir.Text;
  Samples := spSampleCount.Value;

  if OutDir = '' then
  begin
    mLog.Lines.Add('[ERROR  ] Output directory is empty.');
    Exit;
  end;

  btGenerate.Enabled := False;
  Screen.Cursor      := crHourGlass;
  Listener := TLoggerListener.Create;
  try
    Listener.MinLevel := llDebug;
    Listener.OnAddLogMessage := HandleLog;
    Logger.RegisterListener(Listener);
    Gen := TOSFDemoGenerator.Create;
    try
      if IncludeOSF4 then
        Gen.GenerateAll(OutDir, osvOSF4, Samples);
      if IncludeOSF5 then
        Gen.GenerateAll(OutDir, osvOSF5, Samples);
    finally
      Gen.Free;
    end;
  finally
    Logger.UnregisterListener(Listener);
    Listener.Free;
    Screen.Cursor      := crDefault;
    btGenerate.Enabled := True;
  end;
end;

end.
