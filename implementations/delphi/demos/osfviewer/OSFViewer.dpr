program OSFViewer;

uses
  Vcl.Forms,
  FormMain         in 'FormMain.pas' {FormOSFViewer},
  OSF.Types         in '..\..\src\OSF.Types.pas',
  OSF.Channel       in '..\..\src\OSF.Channel.pas',
  OSF.Log           in '..\..\src\OSF.Log.pas',
  OSF.Filer         in '..\..\src\OSF.Filer.pas',
  OSF.Data.Channels in '..\..\src\OSF.Data.Channels.pas',
  OSF.Data.Manager  in '..\..\src\OSF.Data.Manager.pas';

{$R *.res}

begin
  Application.Initialize;
  Application.MainFormOnTaskbar := True;
  Application.Title := 'OSF Viewer';
  Application.CreateForm(TFormOSFViewer, FormOSFViewer);
  Application.Run;
end.
