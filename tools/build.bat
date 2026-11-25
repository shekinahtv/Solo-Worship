@echo off
setlocal
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS%" (
  echo Could not find vcvars64.bat at "%VS%"
  exit /b 1
)
call "%VS%" >nul
if errorlevel 1 exit /b 1
set "ROOT=%~dp0.."
cmake -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build "%ROOT%\build" --parallel
if errorlevel 1 exit /b 1
echo BUILD OK
