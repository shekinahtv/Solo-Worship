@echo off
rem ---------------------------------------------------------------------------
rem Build the Windows installer. Run tools\build.bat first: the script needs
rem build\SoloWorshiper.exe and build\WebView2Loader.dll.
rem ---------------------------------------------------------------------------
setlocal

set ROOT=%~dp0..
cd /d "%ROOT%"

if not exist "build\SoloWorshiper.exe" (
  echo ERROR: build\SoloWorshiper.exe is missing. Run tools\build.bat first.
  exit /b 1
)
if not exist "build\WebView2Loader.dll" (
  echo ERROR: build\WebView2Loader.dll is missing. Run tools\build.bat first.
  exit /b 1
)

set ISCC=
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe

if "%ISCC%"=="" (
  echo ERROR: Inno Setup 6 was not found.
  echo Install it from https://jrsoftware.org/isdl.php and run this script again.
  exit /b 1
)

echo Using %ISCC%
"%ISCC%" installer\lvs.iss
if errorlevel 1 (
  echo INSTALLER FAILED
  exit /b 1
)

echo INSTALLER OK - see dist\
exit /b 0
