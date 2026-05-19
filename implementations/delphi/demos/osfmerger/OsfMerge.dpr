program OsfMerge;

{$APPTYPE CONSOLE}

// Console front-end for OSF.Merger. Walks a root directory, picks the
// OSF / OSFZ files that overlap a UTC interval, merges the requested
// channels, and writes the result as a single OSF file.
//
// Exit codes:
//   0  success
//   1  invalid arguments
//   2  no input files found in the interval
//   3  I/O error while writing the output

uses
  System.SysUtils,
  System.DateUtils,
  System.Classes,
  System.StrUtils,
  OSF.Types        in '..\..\src\OSF.Types.pas',
  OSF.Channel      in '..\..\src\OSF.Channel.pas',
  OSF.Log          in '..\..\src\OSF.Log.pas',
  OSF.Filer        in '..\..\src\OSF.Filer.pas',
  OSF.Data.Channels in '..\..\src\OSF.Data.Channels.pas',
  OSF.Data.Manager in '..\..\src\OSF.Data.Manager.pas',
  OSF.Meta.Cache   in '..\..\src\OSF.Meta.Cache.pas',
  OSF.Merger       in '..\..\src\OSF.Merger.pas';

const
  C_EXIT_OK         = 0;
  C_EXIT_BAD_ARGS   = 1;
  C_EXIT_NO_FILES   = 2;
  C_EXIT_WRITE_FAIL = 3;

procedure PrintUsage;
begin
  Writeln('osfmerge <rootdir> <outputfile> <starttime> <endtime> [channel ...] [options]');
  Writeln('');
  Writeln('Arguments:');
  Writeln('  rootdir     Root directory to scan recursively for .osf / .osfz files');
  Writeln('  outputfile  Output file path (.osf)');
  Writeln('  starttime   Interval start, ISO 8601: 2024-01-15T10:00:00');
  Writeln('  endtime     Interval end,   ISO 8601: 2024-01-15T12:00:00');
  Writeln('  channel     One or more channel names (optional; omit for all channels)');
  Writeln('');
  Writeln('Options:');
  Writeln('  --osf4        Write output as OSF4 (default: OSF5)');
  Writeln('  --overwrite   Overwrite strategy for overlapping timestamps (default: skip)');
  Writeln('  --no-cache    Do not read or write .json sidecar cache files');
  Writeln('  --help        Show this help and exit');
  Writeln('');
  Writeln('Examples:');
  Writeln('  osfmerge D:\data result.osf 2024-01-15T10:00:00 2024-01-15T12:00:00');
  Writeln('  osfmerge D:\data result.osf 2024-01-15T10:00:00 2024-01-15T12:00:00 GPS.Speed Temperatur');
  Writeln('  osfmerge D:\data result.osf 2024-01-15T10:00:00 2024-01-15T12:00:00 --osf4 --overwrite');
end;

function ParseIso8601(const S: string; out DT: TDateTime): Boolean;
var
  Y, M, D, H, N, Sec: Word;
begin
  Result := False;
  DT := 0;
  if Length(S) < 19 then
    Exit;
  try
    Y := StrToInt(Copy(S, 1, 4));
    M := StrToInt(Copy(S, 6, 2));
    D := StrToInt(Copy(S, 9, 2));
    H := StrToInt(Copy(S, 12, 2));
    N := StrToInt(Copy(S, 15, 2));
    Sec := StrToInt(Copy(S, 18, 2));
    DT := EncodeDateTime(Y, M, D, H, N, Sec, 0);
    Result := True;
  except
    Result := False;
  end;
end;

function IsOption(const S: string): Boolean;
begin
  Result := StartsText('--', S);
end;

type
  TParsedArgs = record
    Help: Boolean;
    RootDir: string;
    OutputFile: string;
    StartUtc: TDateTime;
    EndUtc: TDateTime;
    Channels: TArray<string>;
    UseOSF4: Boolean;
    Overwrite: Boolean;
    NoCache: Boolean;
    Valid: Boolean;
    Error: string;
  end;

function ParseArgs: TParsedArgs;
var
  Positional: TArray<string>;
  I, P: Integer;
  Param: string;
begin
  Result := Default(TParsedArgs);
  Result.UseOSF4 := False;
  Result.Overwrite := False;
  Result.NoCache := False;

  SetLength(Positional, 0);
  for I := 1 to ParamCount do
  begin
    Param := ParamStr(I);
    if SameText(Param, '--help') or SameText(Param, '-h') or SameText(Param, '-?') then
      Result.Help := True
    else if SameText(Param, '--osf4') then
      Result.UseOSF4 := True
    else if SameText(Param, '--overwrite') then
      Result.Overwrite := True
    else if SameText(Param, '--no-cache') then
      Result.NoCache := True
    else if IsOption(Param) then
    begin
      Result.Error := 'Unknown option: ' + Param;
      Exit;
    end
    else
    begin
      SetLength(Positional, Length(Positional) + 1);
      Positional[High(Positional)] := Param;
    end;
  end;

  if Result.Help then
    Exit;

  if Length(Positional) < 4 then
  begin
    Result.Error := 'Expected at least 4 positional arguments (rootdir outputfile starttime endtime)';
    Exit;
  end;

  Result.RootDir := Positional[0];
  Result.OutputFile := Positional[1];
  if not ParseIso8601(Positional[2], Result.StartUtc) then
  begin
    Result.Error := 'Invalid starttime: ' + Positional[2];
    Exit;
  end;
  if not ParseIso8601(Positional[3], Result.EndUtc) then
  begin
    Result.Error := 'Invalid endtime: ' + Positional[3];
    Exit;
  end;

  // Remaining positionals (if any) are channel names.
  SetLength(Result.Channels, Length(Positional) - 4);
  P := 0;
  for I := 4 to High(Positional) do
  begin
    Result.Channels[P] := Positional[I];
    Inc(P);
  end;

  Result.Valid := True;
end;

procedure WriteConsoleLog(Level: TOSFLogLevel; const Msg: string);
const
  C_LEVEL: array[TOSFLogLevel] of string = ('DEBUG', 'INFO   ', 'WARNING', 'ERROR  ');
begin
  Writeln(Format('[%s] %s', [C_LEVEL[Level], Msg]));
end;

function RunMerge(const Args: TParsedArgs): Integer;
var
  Merger: TOSFMerger;
  Entries: TArray<TOSFFileEntry>;
  OutSize: Int64;
begin
  Result := C_EXIT_OK;
  Merger := TOSFMerger.Create;
  try
    Merger.OnLog := WriteConsoleLog;
    Merger.RootDirectory := Args.RootDir;
    Merger.SetInterval(Args.StartUtc, Args.EndUtc);
    Merger.ChannelFilter := Args.Channels;
    Merger.UseCache := not Args.NoCache;
    if Args.UseOSF4 then
      Merger.OutputVersion := osvOSF4
    else
      Merger.OutputVersion := osvOSF5;
    if Args.Overwrite then
      Merger.OverlapStrategy := osOverwrite
    else
      Merger.OverlapStrategy := osSkip;

    Writeln('Scanning ', Args.RootDir, ' ...');
    Entries := Merger.Scan;
    Writeln(Format('Found %d files in interval.', [Length(Entries)]));
    if Length(Entries) = 0 then
      Exit(C_EXIT_NO_FILES);

    Writeln(Format('Merging %d files ...', [Length(Entries)]));
    try
      Merger.SaveToFile(Args.OutputFile);
    except
      on E: Exception do
      begin
        Writeln('Write failed: ', E.ClassName, ': ', E.Message);
        Exit(C_EXIT_WRITE_FAIL);
      end;
    end;

    OutSize := 0;
    if FileExists(Args.OutputFile) then
      with TFileStream.Create(Args.OutputFile, fmOpenRead or fmShareDenyNone) do
        try
          OutSize := Size;
        finally
          Free;
        end;
    Writeln(Format('Written: %s (%d bytes)', [Args.OutputFile, OutSize]));
  finally
    Merger.Free;
  end;
end;

var
  Args: TParsedArgs;

begin
  try
    Args := ParseArgs;
    if Args.Help then
    begin
      PrintUsage;
      ExitCode := C_EXIT_OK;
      Exit;
    end;
    if not Args.Valid then
    begin
      Writeln('osfmerge: ', Args.Error);
      Writeln('Run with --help for usage.');
      ExitCode := C_EXIT_BAD_ARGS;
      Exit;
    end;
    ExitCode := RunMerge(Args);
  except
    on E: Exception do
    begin
      Writeln('Unhandled error: ', E.ClassName, ': ', E.Message);
      ExitCode := C_EXIT_WRITE_FAIL;
    end;
  end;
end.
