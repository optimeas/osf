program OSFCompileCheck;

{$APPTYPE CONSOLE}

uses
  OSF.Types               in 'src\OSF.Types.pas',
  OSF.Version             in 'src\OSF.Version.pas',
  OSF.Channel             in 'src\OSF.Channel.pas',
  OSF.Log                 in 'src\OSF.Log.pas',
  OSF.Filer               in 'src\OSF.Filer.pas',
  OSF.Data.Channels       in 'src\OSF.Data.Channels.pas',
  OSF.Data.Manager        in 'src\OSF.Data.Manager.pas',
  OSF.Export              in 'src\OSF.Export.pas',
  OSF.Export.CSV          in 'src\OSF.Export.CSV.pas',
  OSF.Export.CSV.Unified  in 'src\OSF.Export.CSV.Unified.pas',
  OSF.Meta.Cache          in 'src\OSF.Meta.Cache.pas',
  OSF.Merger              in 'src\OSF.Merger.pas',
  Console.ProgressBar     in 'src\console\Console.ProgressBar.pas';

// OSF.Export.HDF5 plus the src\hdf5\ wrapper units are intentionally
// excluded from this smoke test: they depend on the Windows-only HDF5
// runtime DLL and their own unit cluster, which is exercised by the
// osftool project that links the wrapper units explicitly.

begin
end.
