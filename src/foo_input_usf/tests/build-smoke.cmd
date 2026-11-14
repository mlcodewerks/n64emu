@echo off
setlocal
cd /d "%~dp0.."
if "%~4"=="" (
  echo Usage: build-smoke.cmd SDK-root SDK-libraries WTL-include SDK-runtime-directory
  exit /b 1
)
set "usf_root=%CD%\..\.."
set "smoke_out=%usf_root%\build\foo_input_usf\smoke"
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vs_root=%%i"
if not defined vs_root exit /b 1
call "%vs_root%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%smoke_out%" mkdir "%smoke_out%"
cl /nologo /std:c++20 /EHsc /MD /O2 /DNDEBUG /DUNICODE /D_UNICODE /I "%~1" /I "%~1\foobar2000" /I "%~3" /I "%usf_root%\src" tests\component_smoke.cpp /Fo"%smoke_out%\component_smoke.obj" /Fe"%smoke_out%\component_smoke.exe" /link /LIBPATH:"%~2" foobar2000_component_client.lib foobar2000_sdk.lib pfc.lib "%~4\shared.lib" user32.lib shlwapi.lib ole32.lib shell32.lib winmm.lib
if errorlevel 1 exit /b 1
copy /y "%~4\shared.dll" "%smoke_out%\shared.dll" >nul
