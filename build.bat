@echo off
setlocal
pushd "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio 2022 with Desktop development with C++ is required.
  popd
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
  echo C++ build tools were not found.
  popd
  exit /b 1
)
"%VSROOT%\MSBuild\Current\Bin\MSBuild.exe" ChalkStudio.sln /m:1 /nodeReuse:false /p:TrackFileAccess=false /p:Configuration=Release /p:Platform=x64 /nologo /verbosity:minimal
if errorlevel 1 (
  popd
  exit /b 1
)
bin\Release\BoardTests.exe
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
