param(
    [string]$N64emuRoot = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory = $true)][string]$Fb2kSDKRoot,
    [Parameter(Mandatory = $true)][string]$Fb2kLibraryDir,
    [Parameter(Mandatory = $true)][string]$WTLIncludeDir,
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
& (Join-Path $N64emuRoot 'src\foo_input_usf\build-x64.ps1') -N64emuRoot $N64emuRoot -Fb2kSDKRoot $Fb2kSDKRoot -Fb2kLibraryDir $Fb2kLibraryDir -WTLIncludeDir $WTLIncludeDir -Rebuild:$Rebuild
