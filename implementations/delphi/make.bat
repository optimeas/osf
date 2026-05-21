@echo off
rem ============================================================
rem  make.bat - full release build of osftool + demos + setup
rem
rem  Steps:  clean -> locate Delphi -> check HDF5 runtime ->
rem          build osftool -> compile-check demos -> build setup
rem          -> collect executables into bin\
rem
rem  Each Delphi project is built with dcc64 driven by a
rem  temporary dcc64.cfg (written into the project directory and
rem  deleted afterwards). This sidesteps the msbuild DCC task,
rem  whose command line overflows the 32000-char limit when the
rem  machine carries a long global library search path.
rem
rem  Exit codes:
rem    0  success
rem    1  prerequisite missing (Delphi, HDF5 runtime, ISCC)
rem    2  build error (dcc64 returned non-zero)
rem    3  installer error (ISCC returned non-zero)
rem ============================================================
setlocal enableextensions enabledelayedexpansion
pushd "%~dp0"

set "PF86=%ProgramFiles(x86)%"
set "PF=%ProgramFiles%"

rem --- [1/7] Clean ------------------------------------------------
echo --- [1/7] Cleaning build artifacts ---
call "%~dp0clean.bat"

rem --- [2/7] Locate Delphi ---------------------------------------
echo --- [2/7] Locating Delphi (rsvars.bat) ---
set "RSVARS="
for %%v in (23.0 22.0 37.0 38.0) do (
  if not defined RSVARS if exist "!PF86!\Embarcadero\Studio\%%v\bin\rsvars.bat" (
    set "RSVARS=!PF86!\Embarcadero\Studio\%%v\bin\rsvars.bat"
  )
)
if not defined RSVARS (
  echo ERROR: no RAD Studio rsvars.bat found ^(checked Studio 23.0, 22.0, 37.0, 38.0^).
  echo        Install RAD Studio, or set the BDS environment variable manually.
  popd & endlocal & exit /b 1
)
echo Using: !RSVARS!
call "!RSVARS!"
if not exist "!BDS!\bin\dcc64.exe" (
  echo ERROR: dcc64.exe not found under "!BDS!\bin" - the Delphi setup looks broken.
  popd & endlocal & exit /b 1
)
set "DCC64=!BDS!\bin\dcc64.exe"
set "RTL=!BDS!\lib\Win64\release"

rem --- [3/7] Check HDF5 runtime ----------------------------------
echo --- [3/7] Checking HDF5 runtime ---
if not exist "%~dp0..\..\dataformats\hdf5\lib\win64\hdf5.dll" (
  echo ERROR: hdf5.dll not found under dataformats\hdf5\lib\win64\.
  echo        Run dataformats\hdf5\lib\install-hdf5.ps1 to fetch the HDF5 runtime.
  popd & endlocal & exit /b 1
)

rem --- [4/7] Build osftool ----------------------------------------
echo --- [4/7] Building osftool (Release / Win64) ---
call :build_project "%~dp0tools\osftool" "OsfTool.dpr" ""
if errorlevel 1 (
  echo ERROR: osftool build failed.
  popd & endlocal & exit /b 2
)
if not exist "%~dp0tools\osftool\Win64\Release\OsfTool.exe" (
  echo ERROR: osftool build reported success but OsfTool.exe is missing.
  popd & endlocal & exit /b 2
)

rem --- [5/7] Compile-check the demo projects ----------------------
rem  osfviewer depends on TeeChart, which is present with Delphi 22 / 23
rem  but not (yet) with Delphi 37. It is therefore treated as optional:
rem  a build failure there is a warning, not a fatal error. Every other
rem  demo is mandatory and aborts the build on failure.
echo --- [5/7] Building demo projects (Release / Win64) ---
set /a DEMOCOUNT=0
if exist "%~dp0demos\" (
  for /d %%d in ("%~dp0demos\*") do (
    for %%f in ("%%~d\*.dpr") do (
      set /a DEMOCOUNT+=1
      set "DEMOEXTRA="
      set "DEMOOPT=0"
      if /i "%%~nxd"=="osfviewer" set "DEMOOPT=1"
      if /i "%%~nxd"=="osfviewer" set "DEMOEXTRA=V:\tools\kompD2010\TChart\Source"
      echo   building %%~nxf
      call :build_project "%%~d" "%%~nxf" "!DEMOEXTRA!"
      if errorlevel 1 (
        if "!DEMOOPT!"=="1" (
          echo   WARNING: %%~nxf failed to build - skipped [needs TeeChart, Delphi 22/23].
        ) else (
          echo ERROR: demo build failed: %%f
          popd
          endlocal
          exit /b 2
        )
      )
    )
  )
)
if !DEMOCOUNT! EQU 0 echo   No demo projects found.

rem --- [6/7] Build the installer ----------------------------------
echo --- [6/7] Building the installer (ISCC) ---
set "ISCC="
if exist "!PF86!\Inno Setup 6\ISCC.exe" set "ISCC=!PF86!\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "!PF!\Inno Setup 6\ISCC.exe" set "ISCC=!PF!\Inno Setup 6\ISCC.exe"
if not defined ISCC (
  echo ERROR: ISCC.exe ^(Inno Setup 6^) not found.
  echo        Install Inno Setup 6 from https://jrsoftware.org/isdl.php
  popd & endlocal & exit /b 1
)
"!ISCC!" "%~dp0setup\osftool.iss"
if errorlevel 1 (
  echo ERROR: installer build failed.
  popd & endlocal & exit /b 3
)

rem --- [7/7] Collect executables into bin\ -------------------------
echo --- [7/7] Collecting executables into bin\ ---
set "BINDIR=%~dp0bin"
if not exist "!BINDIR!" mkdir "!BINDIR!" >nul 2>&1
if exist "%~dp0tools\osftool\Win64\Release\OsfTool.exe" copy /y "%~dp0tools\osftool\Win64\Release\OsfTool.exe" "!BINDIR!\" >nul
if exist "%~dp0demos\" (
  for /d %%d in ("%~dp0demos\*") do (
    for %%f in ("%%~d\Win64\Release\*.exe") do copy /y "%%f" "!BINDIR!\" >nul
  )
)
echo.
echo Build complete.
echo   executables : %~dp0bin
echo   installer   : %~dp0setup\  ^(osftool-^<version^>-setup-x64.exe^)
popd
endlocal
exit /b 0

rem ============================================================
rem  :build_project  - compile one .dpr with dcc64 (Release/Win64)
rem    %~1  project directory
rem    %~2  .dpr file name
rem    %~3  optional extra unit search path (may be empty)
rem  Returns dcc64's exit code. A temporary dcc64.cfg carries all
rem  parameters so the command line stays short.
rem ============================================================
:build_project
setlocal enabledelayedexpansion
set "PRJDIR=%~1"
set "DPR=%~2"
set "EXTRAU=%~3"
set "OUT=!PRJDIR!\Win64\Release"
if not exist "!OUT!" mkdir "!OUT!" >nul 2>&1
(
  echo -B
  echo -$D-
  echo -$L-
  echo -$Y-
  echo -DRELEASE
  echo -NS"Winapi;System.Win;Data.Win;Datasnap.Win;Web.Win;Soap.Win;Xml.Win;Bde;System;Xml;Data;Datasnap;Web;Soap;Vcl;Vcl.Imaging;Vcl.Touch;Vcl.Samples;Vcl.Shell;VCLTee"
  echo -U"!RTL!;..\..\src;..\..\src\hdf5;!EXTRAU!"
  echo -E"!OUT!"
  echo -NU"!OUT!"
) > "!PRJDIR!\dcc64.cfg"
pushd "!PRJDIR!"
"!DCC64!" "!DPR!"
set "RC=!ERRORLEVEL!"
popd
del /q "!PRJDIR!\dcc64.cfg" >nul 2>&1
endlocal & exit /b %RC%
