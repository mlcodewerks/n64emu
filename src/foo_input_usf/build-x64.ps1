param(
    [Alias('ParallaxRoot')]
    [string]$N64emuRoot = (Join-Path $PSScriptRoot '..\..'),
    [string]$Fb2kSDKRoot = (Join-Path $PSScriptRoot '..\..\deps\fb2k'),
    [string]$Fb2kLibraryDir,
    [string]$WTLIncludeDir,
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$N64emuRoot = [IO.Path]::GetFullPath($N64emuRoot)
if (!$Fb2kLibraryDir) { $Fb2kLibraryDir = Join-Path $Fb2kSDKRoot 'lib\x64\Release' }
if (!$WTLIncludeDir) { $WTLIncludeDir = Join-Path $Fb2kSDKRoot 'wtl\include' }
$Fb2kSDKRoot = [IO.Path]::GetFullPath($Fb2kSDKRoot)
$Fb2kLibraryDir = [IO.Path]::GetFullPath($Fb2kLibraryDir)
$WTLIncludeDir = [IO.Path]::GetFullPath($WTLIncludeDir)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio with the MSVC C++ build tools is required.' }
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
Push-Location $PSScriptRoot
try {
    & $msbuild foo_input_usf.slnx "/t:$target" /p:Configuration=Release /p:Platform=x64 "/p:N64emuUSFRoot=$N64emuRoot" "/p:Fb2kSDKRoot=$Fb2kSDKRoot" "/p:Fb2kLibraryDir=$Fb2kLibraryDir" "/p:WTLIncludeDir=$WTLIncludeDir" /m /nr:false /nologo /v:minimal
    if ($LASTEXITCODE) { throw "x64 build failed (exit $LASTEXITCODE)." }
    Write-Output 'Built build\foo_input_usf\Bin\x64\Release\foo_input_usf.dll and n64emu_usf.dll. Install both together.'
} finally { Pop-Location }
