@echo off
setlocal
cd /d "%~dp0.."
set "usf_root=%~1"
if not defined usf_root set "usf_root=%CD%\..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MD /O2 /DNDEBUG /DUNICODE /D_UNICODE /I Ext /I Ext\foobar2000 /I Ext\psflib /I Ext\wtl.10.1.0\lib\native\include /I "%usf_root%\src" tests\component_smoke.cpp /FoObj\smoke\component_smoke.obj /FeObj\smoke\component_smoke.exe /link x64\Release\foobar2000_component_client.lib x64\Release\foobar2000_sdk.lib x64\Release\pfc.lib Obj\smoke\shared.lib user32.lib shlwapi.lib ole32.lib shell32.lib winmm.lib
