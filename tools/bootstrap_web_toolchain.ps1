#requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$toolchainDefinitionPath = Join-Path $PSScriptRoot 'web_toolchain.json'
$webToolchain = Get-Content -Raw -LiteralPath $toolchainDefinitionPath | ConvertFrom-Json
$localToolsRoot = Join-Path $repositoryRoot '.tools'
$emsdkRoot = Join-Path $localToolsRoot 'emsdk'
$emsdkCommand = Join-Path $emsdkRoot 'emsdk.bat'

if (-not (Test-Path -LiteralPath $localToolsRoot)) {
    New-Item -ItemType Directory -Path $localToolsRoot | Out-Null
}

if (-not (Test-Path -LiteralPath $emsdkRoot)) {
    Write-Host "Cloning emsdk $($webToolchain.emscripten)..."
    & git clone --branch $webToolchain.emscripten --depth 1 `
        https://github.com/emscripten-core/emsdk.git $emsdkRoot
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to clone emsdk.'
    }
}

if (-not (Test-Path -LiteralPath $emsdkCommand -PathType Leaf)) {
    throw "The emsdk checkout is incomplete: $emsdkCommand is missing."
}

$actualEmsdkCommit = (& git -C $emsdkRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualEmsdkCommit -ne $webToolchain.emsdkCommit) {
    throw "Expected emsdk commit $($webToolchain.emsdkCommit), found $actualEmsdkCommit."
}

$emscriptenTarget = $webToolchain.emscripten
$cmakeTarget = "cmake-$($webToolchain.cmake)-64bit"
$ninjaTarget = "ninja-$($webToolchain.ninja)-64bit"
$env:EMSDK_NOTTY = '1'

Write-Host "Installing web toolchain into $localToolsRoot..."
& $emsdkCommand install $emscriptenTarget $cmakeTarget $ninjaTarget
if ($LASTEXITCODE -ne 0) {
    throw 'emsdk failed to install the pinned web toolchain.'
}

& $emsdkCommand activate $emscriptenTarget $cmakeTarget $ninjaTarget
if ($LASTEXITCODE -ne 0) {
    throw 'emsdk failed to activate the pinned web toolchain.'
}

Write-Host 'Web toolchain ready. Run tools/build_web.ps1 next.'
