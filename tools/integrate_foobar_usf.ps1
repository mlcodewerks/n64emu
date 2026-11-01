param(
    [Parameter(Mandatory = $true)][string]$ComponentRoot,
    [string]$n64emuRoot
)
$ErrorActionPreference = 'Stop'
if (!$n64emuRoot) { $n64emuRoot = Split-Path -Parent $PSScriptRoot }
$componentDirectory = (Resolve-Path -LiteralPath $ComponentRoot).Path
$engineDirectory = (Resolve-Path -LiteralPath $n64emuRoot).Path
$projectPath = Join-Path $componentDirectory 'foo_input_usf.vcxproj'
$versionPath = Join-Path $componentDirectory 'library.h'
$project = [IO.File]::ReadAllText($projectPath)
$version = [IO.File]::ReadAllText($versionPath)
$importAnchor = '<Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />'
$import = '<Import Project="$(MSBuildProjectDirectory)\n64emu_usf.props" />'
$reference = '<ProjectReference Include="Ext\lazyusf2\lazyusf2.vcxproj"'
if (!$project.Contains($import)) {
    if (!$project.Contains($importAnchor) -or !$project.Contains($reference)) {
        throw 'The component project does not have the expected C++ import and lazyusf2 reference.'
    }
    $project = $project.Replace($importAnchor, $importAnchor + "`r`n  " + $import)
    $project = $project.Replace($reference, $reference + ' Condition="''$(Usen64emuUSF)'' != ''true''"')
}
if (!$version.Contains('N64EMU_USF_COMPONENT')) {
    $versionAnchor = '#define LAZYUSF2_VERSION "ea74bbb"'
    if (!$version.Contains($versionAnchor)) { throw 'Unexpected lazyusf2 version declaration.' }
    $version = $version.Replace($versionAnchor, "#ifdef N64EMU_USF_COMPONENT`r`n" +
        '#define LAZYUSF2_VERSION "n64emu USF interface"' + "`r`n#else`r`n" + $versionAnchor + "`r`n#endif")
}
$props = [IO.File]::ReadAllText((Join-Path $engineDirectory 'src/usf/foobar_usf.props'))
$props = $props.Replace('@N64EMU_ROOT@', [Security.SecurityElement]::Escape($engineDirectory))
[xml]$null = $project
[xml]$null = $props
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($projectPath, $project, $encoding)
[IO.File]::WriteAllText($versionPath, $version, $encoding)
[IO.File]::WriteAllText((Join-Path $componentDirectory 'n64emu_usf.props'), $props, $encoding)
Copy-Item -LiteralPath (Join-Path $engineDirectory 'src/usf/foobar_usf_delay.cpp') -Destination (Join-Path $componentDirectory 'n64emu_usf_delay.cpp') -Force
Write-Output 'n64emu USF enabled for Win32/x64. Set Usen64emuUSF=false to use lazyusf2.'
