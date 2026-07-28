// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

// DUnitX console test suite for the OSF Delphi library.
//
// Build (from implementations/delphi/tests) with the DUnitX source on the
// unit + include path and the .dcu output routed to a writable directory
// (the DUnitX source tree under Program Files is read-only):
//   set DX=C:\Program Files (x86)\Embarcadero\Studio\23.0\source\DUnitX
//   dcc32 -B -Q -U"%DX%" -I"%DX%" -NU"dcu32" OSFTests.dpr
//   dcc64 -B -Q -U"%DX%" -I"%DX%" -NU"dcu64" OSFTests.dpr
// Then run OSFTests[.exe]; exit code 0 = all passed, 1 = failures.
program OSFTests;

{$APPTYPE CONSOLE}
{$STRONGLINKTYPES ON}

uses
  System.SysUtils,
  DUnitX.Loggers.Console,
  DUnitX.TestFramework,
  OSF.Types              in '..\src\OSF.Types.pas',
  OSF.CRC32C             in '..\src\OSF.CRC32C.pas',
  OSF.Channel            in '..\src\OSF.Channel.pas',
  OSF.Log                in '..\src\OSF.Log.pas',
  OSF.Filer              in '..\src\OSF.Filer.pas',
  Test.OSF.CRC32C          in 'Test.OSF.CRC32C.pas',
  Test.OSF.Filer.Header    in 'Test.OSF.Filer.Header.pas',
  Test.OSF.Filer.Integrity in 'Test.OSF.Filer.Integrity.pas',
  Test.OSF.Filer.ZeroLengthBlock in 'Test.OSF.Filer.ZeroLengthBlock.pas';

var
  Runner: ITestRunner;
  Results: IRunResults;
  Logger: ITestLogger;
begin
  try
    Runner := TDUnitX.CreateRunner;
    Runner.UseRTTI := True;
    Runner.FailsOnNoAsserts := False;
    Logger := TDUnitXConsoleLogger.Create(True);
    Runner.AddLogger(Logger);
    Results := Runner.Execute;
    if not Results.AllPassed then
      System.ExitCode := 1;
  except
    on E: Exception do
    begin
      System.Writeln(E.ClassName, ': ', E.Message);
      System.ExitCode := 2;
    end;
  end;
end.
