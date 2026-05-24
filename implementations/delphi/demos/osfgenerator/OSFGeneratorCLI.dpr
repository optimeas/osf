// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// Headless companion to OSFGenerator.exe (FormGenerator.pas). Generates
// both the OSF4 and OSF5 reference sets non-interactively so the suite
// can be regenerated from CI or from a one-shot terminal session.
//
// Usage:
//   OSFGeneratorCLI [output-dir] [samples-per-channel]
// Defaults: output-dir = ../../../../examples/generated relative to the
// exe (matching the GUI's own default), samples-per-channel = 100
// (matching the spSampleCount default in FormGenerator.dfm).
program OSFGeneratorCLI;

{$APPTYPE CONSOLE}

uses
  System.SysUtils,
  System.IOUtils,
  OSFDemoGenerator  in 'OSFDemoGenerator.pas',
  OSF.Types         in '..\..\src\OSF.Types.pas',
  OSF.Channel       in '..\..\src\OSF.Channel.pas',
  OSF.Log           in '..\..\src\OSF.Log.pas',
  OSF.Filer         in '..\..\src\OSF.Filer.pas';

const
  // Relative path from the exe to examples/generated/. The exe lives
  // in implementations\delphi\demos\osfgenerator\, the target lives at
  // the repo-root examples\ tree -- four parent steps.
  EXAMPLES_REL = '..\..\..\..\examples\generated';
  DEFAULT_SAMPLE_COUNT = 100;

procedure StdoutLog(Level: TOSFLogLevel; const Msg: string);
const
  LevelStr: array[TOSFLogLevel] of string = ('DEBUG', 'INFO', 'WARNING', 'ERROR');
begin
  Writeln(Format('[%-7s] %s', [LevelStr[Level], Msg]));
end;

var
  Gen: TOSFDemoGenerator;
  OutDir: string;
  Samples: Integer;
begin
  try
    if ParamCount >= 1 then
      OutDir := ParamStr(1)
    else
      OutDir := TPath.GetFullPath(
        TPath.Combine(ExtractFilePath(ParamStr(0)), EXAMPLES_REL));

    if ParamCount >= 2 then
      Samples := StrToIntDef(ParamStr(2), DEFAULT_SAMPLE_COUNT)
    else
      Samples := DEFAULT_SAMPLE_COUNT;

    if not TDirectory.Exists(OutDir) then
      TDirectory.CreateDirectory(OutDir);

    Writeln('OSF Reference Generator (CLI)');
    Writeln('Output directory: ', OutDir);
    Writeln('Samples per channel: ', Samples);
    Writeln;

    Gen := TOSFDemoGenerator.Create;
    try
      Gen.OnLog := StdoutLog;
      Gen.GenerateAll(OutDir, osvOSF4, Samples);
      Gen.GenerateAll(OutDir, osvOSF5, Samples);
    finally
      Gen.Free;
    end;

    Writeln;
    Writeln('Done.');
  except
    on E: Exception do
    begin
      Writeln(ErrOutput, 'ERROR: ', E.ClassName, ': ', E.Message);
      ExitCode := 1;
    end;
  end;
end.
