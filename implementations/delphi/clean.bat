@echo off
rem ============================================================
rem  clean.bat - remove transient Delphi build artifacts
rem
rem  Deletes, recursively from this script's directory:
rem    *.dcu *.exe *.local *.identcache *.dsk *.dsv *.tvsconfig
rem    *.stat *.~* *.map *.drc *.rsm *.tds
rem    __history\ and __recovery\ directories
rem    whole per-platform output directories: Win32, Win64,
rem      Win64x, OSX64, OSXARM64, Linux64, Android, Android64,
rem      iOSDevice64, iOSSimARM64
rem    top-level bin\ dist\ build\ directories
rem
rem  This removes every build product - compiled demo and test
rem  executables and the generated osftool setup installer
rem  included; make.bat rebuilds them.
rem
rem  Left untouched: source files (.pas .dpr .dproj .dpk .dfm
rem  .res .rc .iss), documentation (.md .txt) and the .git\
rem  directory. The HDF5 runtime under
rem  ..\..\dataformats\hdf5\lib\ is managed by install-hdf5.ps1,
rem  lies outside this tree and is never touched here.
rem ============================================================
setlocal enableextensions
pushd "%~dp0"

echo Cleaning Delphi build artifacts...

rem -- transient files by extension, anywhere below this folder
for %%e in (dcu exe local identcache dsk dsv tvsconfig stat map drc rsm tds) do (
  del /s /q "*.%%e" >nul 2>&1
)
del /s /q "*.~*" >nul 2>&1

rem -- Delphi history / recovery folders, anywhere
for /d /r %%d in (__history __recovery) do (
  if exist "%%d" rd /s /q "%%d" >nul 2>&1
)

rem -- whole per-platform output directories, anywhere
for %%p in (Win32 Win64 Win64x OSX64 OSXARM64 Linux64 Android Android64 iOSDevice64 iOSSimARM64) do (
  for /d /r %%d in (%%p) do (
    if exist "%%d" rd /s /q "%%d" >nul 2>&1
  )
)

rem -- top-level output folders, directly under this script only
for %%d in (bin dist build) do (
  if exist "%~dp0%%d" rd /s /q "%~dp0%%d" >nul 2>&1
)

echo Done.
popd
endlocal
exit /b 0
