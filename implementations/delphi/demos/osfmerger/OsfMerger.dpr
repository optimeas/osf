program OsfMerger;

{$R *.res}

uses
  Vcl.Forms,
  FormMerger       in 'FormMerger.pas' {FormMerger},
  OSF.Types        in '..\..\src\OSF.Types.pas',
  OSF.Channel      in '..\..\src\OSF.Channel.pas',
  OSF.Log          in '..\..\src\OSF.Log.pas',
  OSF.Filer        in '..\..\src\OSF.Filer.pas',
  OSF.Data.Channels in '..\..\src\OSF.Data.Channels.pas',
  OSF.Data.Manager in '..\..\src\OSF.Data.Manager.pas',
  OSF.Meta.Cache   in '..\..\src\OSF.Meta.Cache.pas',
  OSF.Merger       in '..\..\src\OSF.Merger.pas';

begin
  Application.Initialize;
  Application.MainFormOnTaskbar := True;
  Application.Title := 'OSF Merger';
  Application.CreateForm(TFormMerger, OsfMergerForm);
  Application.Run;
end.
