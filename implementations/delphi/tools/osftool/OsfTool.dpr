program OsfTool;

{$APPTYPE CONSOLE}

uses
  System.SysUtils,
  OsfTool.Dispatcher      in 'OsfTool.Dispatcher.pas',
  Cmd.Base                in 'Cmd.Base.pas',
  OsfToolConfig           in 'OsfToolConfig.pas',
  Cmd.Merge               in 'Cmd.Merge.pas',
  Cmd.Export              in 'Cmd.Export.pas',
  Cmd.Info                in 'Cmd.Info.pas',
  Cmd.Channels            in 'Cmd.Channels.pas',
  Cmd.Stat                in 'Cmd.Stat.pas',
  Cmd.Cache               in 'Cmd.Cache.pas',
  Cmd.Config              in 'Cmd.Config.pas',
  Cmd.Convert             in 'Cmd.Convert.pas',
  Cmd.Verify              in 'Cmd.Verify.pas',
  OSF.Types               in '..\..\src\OSF.Types.pas',
  OSF.Channel             in '..\..\src\OSF.Channel.pas',
  OSF.Log                 in '..\..\src\OSF.Log.pas',
  OSF.Filer               in '..\..\src\OSF.Filer.pas',
  OSF.Data.Channels       in '..\..\src\OSF.Data.Channels.pas',
  OSF.Data.Manager        in '..\..\src\OSF.Data.Manager.pas',
  OSF.Export              in '..\..\src\OSF.Export.pas',
  OSF.Export.CSV          in '..\..\src\OSF.Export.CSV.pas',
  OSF.Export.CSV.Unified  in '..\..\src\OSF.Export.CSV.Unified.pas',
  Hdf5.Types              in '..\..\src\hdf5\Hdf5.Types.pas',
  Hdf5.Api                in '..\..\src\hdf5\Hdf5.Api.pas',
  Hdf5.Wrapper            in '..\..\src\hdf5\Hdf5.Wrapper.pas',
  OSF.Export.HDF5         in '..\..\src\OSF.Export.HDF5.pas',
  OSF.Meta.Cache          in '..\..\src\OSF.Meta.Cache.pas',
  OSF.Merger              in '..\..\src\OSF.Merger.pas';

var
  Dispatcher: TOsfToolDispatcher;
  Args: TArray<string>;
  I: Integer;

begin
  try
    SetLength(Args, ParamCount);
    for I := 1 to ParamCount do
      Args[I - 1] := ParamStr(I);

    Dispatcher := TOsfToolDispatcher.Create;
    try
      ExitCode := Dispatcher.Run(Args);
    finally
      Dispatcher.Free;
    end;
  except
    on E: Exception do
    begin
      Writeln(ErrOutput, 'osftool: unhandled ', E.ClassName, ': ', E.Message);
      ExitCode := 3;
    end;
  end;
end.
