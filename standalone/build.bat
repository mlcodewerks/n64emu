@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "MSYS2_ROOT="
set "run_tests="

:parse_args
if "%~1"=="" goto build
if /i "%~1"=="-Test" (
    set "run_tests=1"
    shift /1
    goto parse_args
)
if /i "%~1"=="-Msys2Root" (
    if "%~2"=="" goto usage
    shift /1
)
if defined MSYS2_ROOT goto usage
set "MSYS2_ROOT=%~1"
if "%MSYS2_ROOT:~0,1%"=="-" goto usage
shift /1
goto parse_args

:build
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "PATH=%MSYS2_ROOT%\ucrt64\bin;%MSYS2_ROOT%\usr\bin;%PATH%"
set CC="%MSYS2_ROOT%\ucrt64\bin\gcc.exe"
set CXX="%MSYS2_ROOT%\ucrt64\bin\g++.exe"
pushd "%~dp0" || exit /b 1

if not exist "build\meson-private\coredata.dat" (
    call meson setup build || goto done
)
call meson compile -C build || goto done
if defined run_tests call meson test -C build --no-rebuild --print-errorlogs --num-processes 1

:done
set "build_status=%errorlevel%"
popd
exit /b %build_status%

:usage
echo Usage: %~nx0 [-Test] [[-Msys2Root] "path"] 1>&2
exit /b 1
