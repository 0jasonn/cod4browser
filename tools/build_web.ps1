#requires -Version 5.1

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$toolchainDefinitionPath = Join-Path $PSScriptRoot 'web_toolchain.json'
$webToolchain = Get-Content -Raw -LiteralPath $toolchainDefinitionPath | ConvertFrom-Json
$emsdkRoot = Join-Path $repositoryRoot '.tools\emsdk'
$emscriptenConfig = Join-Path $emsdkRoot '.emscripten'
$cmakeExecutable = Join-Path $emsdkRoot "cmake\$($webToolchain.cmake)_64bit\bin\cmake.exe"
$ninjaExecutable = Join-Path $emsdkRoot "ninja\$($webToolchain.ninja)_64bit\ninja.exe"
$emscriptenToolchain = Join-Path $emsdkRoot 'upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake'
$buildDirectory = Join-Path $repositoryRoot 'build\web'

foreach ($requiredPath in @(
    $emscriptenConfig,
    $cmakeExecutable,
    $ninjaExecutable,
    $emscriptenToolchain
)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Missing web toolchain file: $requiredPath. Run tools/bootstrap_web_toolchain.ps1 first."
    }
}

$env:EM_CONFIG = $emscriptenConfig
$env:EMSDK = $emsdkRoot

& $cmakeExecutable `
    -S $repositoryRoot `
    -B $buildDirectory `
    -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$emscriptenToolchain" `
    "-DCMAKE_MAKE_PROGRAM=$ninjaExecutable" `
    '-DKISAK_PLATFORM=web' `
    "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to configure the web target.'
}

& $cmakeExecutable --build $buildDirectory --target KisakCOD-web -- -j1
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to build the web target.'
}

$siteDirectory = Join-Path $buildDirectory 'site'
foreach ($requiredOutput in @(
    'index.html',
    'launcher.mjs',
    'asset_store.mjs',
    'filesystem_bridge.mjs',
    'styles.css',
    'kisakcod.mjs',
    'kisakcod.wasm'
)) {
    $outputPath = Join-Path $siteDirectory $requiredOutput
    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        throw "The web build completed without required output: $outputPath"
    }
}

Write-Host "Browser build ready at $buildDirectory\site\index.html"
