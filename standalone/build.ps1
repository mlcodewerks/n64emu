param([switch]$Test, [string]$Msys2Root = 'C:\msys64')
$ErrorActionPreference = 'Stop'
$env:MSYS2_ROOT = $Msys2Root
$env:PATH = "$Msys2Root\ucrt64\bin;$Msys2Root\usr\bin;$env:PATH"
$env:CC = "$Msys2Root\ucrt64\bin\gcc.exe"
$env:CXX = "$Msys2Root\ucrt64\bin\g++.exe"
Push-Location $PSScriptRoot
try {
    if (!(Test-Path build/meson-private/coredata.dat)) {
        & meson setup build
        if ($LASTEXITCODE) { throw 'Configure failed' }
    }
    & meson compile -C build
    if ($LASTEXITCODE) { throw 'Build failed' }
    if ($Test) {
        & meson test -C build --no-rebuild --print-errorlogs --num-processes 1
        if ($LASTEXITCODE) { throw 'Tests failed' }
    }
} finally { Pop-Location }
