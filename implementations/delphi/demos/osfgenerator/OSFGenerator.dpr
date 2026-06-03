// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Optimeas GmbH

program OSFGenerator;

uses
  Vcl.Forms,
  FormGenerator     in 'FormGenerator.pas' {FormGenerator},
  OSFDemoGenerator  in 'OSFDemoGenerator.pas',
  OSF.Types         in '..\..\src\OSF.Types.pas',
  OSF.Channel       in '..\..\src\OSF.Channel.pas',
  OSF.Log           in '..\..\src\OSF.Log.pas',
  OSF.Filer         in '..\..\src\OSF.Filer.pas';

{$R *.res}

begin
  Application.Initialize;
  Application.MainFormOnTaskbar := True;
  Application.Title := 'OSF Demo Generator';
  Application.CreateForm(TFormGenerator, FrmGenerator);
  Application.Run;
end.
