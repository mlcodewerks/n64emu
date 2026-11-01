param(
    [Parameter(Mandatory = $true)][ValidateSet('Win32', 'x64')][string]$Architecture,
    [string]$BuildDirectory,
    [string]$LibraryManager
)
$ErrorActionPreference = 'Stop'
if (!$BuildDirectory) {
    $BuildDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) "build/usf/$Architecture"
}
$directory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$definition = Join-Path $directory 'n64emu_usf.def'
if (!(Test-Path -LiteralPath $definition) -or !(Test-Path -LiteralPath (Join-Path $directory 'n64emu_usf.dll'))) {
    throw "Build the $Architecture USF DLL and .def in $directory first."
}

if (!$LibraryManager) {
    $command = Get-Command lib.exe -ErrorAction SilentlyContinue
    if ($command) { $LibraryManager = $command.Source }
}
if (!$LibraryManager) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $LibraryManager = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Tools/MSVC/**/bin/Hostx64/x64/lib.exe' | Select-Object -First 1
    }
}
if (!$LibraryManager) {
    throw 'Install Visual Studio C++ build tools, run from a Developer PowerShell, or pass -LibraryManager with the path to lib.exe.'
}

$machine = if ($Architecture -eq 'Win32') { 'X86' } else { 'X64' }
$library = Join-Path $directory 'n64emu_usf.lib'
& $LibraryManager /nologo "/def:$definition" "/out:$library" /name:n64emu_usf.dll "/machine:$machine"
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed with exit code $LASTEXITCODE." }
Write-Output "Created $library for MSVC linking and delay loading."
