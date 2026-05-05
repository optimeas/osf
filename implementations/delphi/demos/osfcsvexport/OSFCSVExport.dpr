program OSFCSVExport;

uses
  Vcl.Forms,
  FormCSVExport     in 'FormCSVExport.pas' {FormCSVExport},
  OSF.Types         in '..\..\src\OSF.Types.pas',
  OSF.Channel       in '..\..\src\OSF.Channel.pas',
  OSF.Log           in '..\..\src\OSF.Log.pas',
  OSF.Filer         in '..\..\src\OSF.Filer.pas',
  OSF.Data.Channels in '..\..\src\OSF.Data.Channels.pas',
  OSF.Data.Manager  in '..\..\src\OSF.Data.Manager.pas',
  OSF.Export        in '..\..\src\OSF.Export.pas',
  OSF.Export.CSV    in '..\..\src\OSF.Export.CSV.pas';

{$R *.res}

begin
  Application.Initialize;
  Application.MainFormOnTaskbar := True;
  Application.Title := 'OSF to CSV Exporter';
  Application.CreateForm(TFormCSVExport, FrmCSVExport);
  Application.Run;
end.
