program OsfTool;

{$APPTYPE CONSOLE}
{$R *.res}

uses
  {$IFDEF MSWINDOWS}
  Winapi.Windows,
  {$ENDIF}
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
  OSF.Version             in '..\..\src\OSF.Version.pas',
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
  OSF.Progress            in '..\..\src\OSF.Progress.pas',
  OSF.Progress.Console    in '..\..\src\OSF.Progress.Console.pas',
  OSF.Progress.Quiet      in '..\..\src\OSF.Progress.Quiet.pas',
  OSF.Progress.Verbose    in '..\..\src\OSF.Progress.Verbose.pas',
  OSF.Progress.Json       in '..\..\src\OSF.Progress.Json.pas',
  OSF.Progress.Fallback   in '..\..\src\OSF.Progress.Fallback.pas',
  OSF.Progress.Live       in '..\..\src\OSF.Progress.Live.pas',
  OSF.Progress.LogFile    in '..\..\src\OSF.Progress.LogFile.pas',
  OSF.Merger              in '..\..\src\OSF.Merger.pas';

{$IFDEF MSWINDOWS}
// Switches the console and the RTL text files to UTF-8 so non-ASCII output
// (units like 'degC', the live progress bar's block glyphs) renders
// correctly regardless of the machine's legacy code page.
procedure EnableUtf8Console;
begin
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  SetTextCodePage(Output, CP_UTF8);
  SetTextCodePage(ErrOutput, CP_UTF8);
end;
{$ENDIF}

var
  Dispatcher: TOsfToolDispatcher;
  Args: TArray<string>;
  I: Integer;

begin
  {$IFDEF MSWINDOWS}
  EnableUtf8Console;
  {$ENDIF}
  try
    SetLength(Args, ParamCount);
    for I := 1 to ParamCount do
      Args[I - 1] := ParamStr(I);

    Dispatcher := TOsfToolDispatcher.Create;
    try
      ExitCode := Dispatcher.Run(Args);
      Writeln;
      // Readln; //only for IDE Debugging
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
