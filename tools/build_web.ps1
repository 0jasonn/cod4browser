#requires -Version 5.1

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$buildJobs = 2
if ($env:KISAK_BUILD_JOBS) {
    if (-not [int]::TryParse($env:KISAK_BUILD_JOBS, [ref]$buildJobs) -or
        $buildJobs -lt 1 -or $buildJobs -gt 16) {
        throw 'KISAK_BUILD_JOBS must be an integer from 1 through 16.'
    }
}

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

$totalTimer = [Diagnostics.Stopwatch]::StartNew()
$stepTimer = [Diagnostics.Stopwatch]::StartNew()
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
$stepTimer.Stop()
Write-Host ("KISAK_TIMING web_configure_seconds={0:N3}" -f $stepTimer.Elapsed.TotalSeconds)

$stepTimer.Restart()
& $cmakeExecutable --build $buildDirectory --target KisakCOD-web --parallel $buildJobs
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to build the web target.'
}
$stepTimer.Stop()
Write-Host ("KISAK_TIMING web_compile_seconds={0:N3} jobs={1}" -f `
    $stepTimer.Elapsed.TotalSeconds, $buildJobs)

$stepTimer.Restart()
& $cmakeExecutable --build $buildDirectory --target check-canonical-runtime-prefix --parallel $buildJobs
if ($LASTEXITCODE -ne 0) {
    throw 'The strict canonical runtime-prefix compile/link/runtime check failed.'
}
$stepTimer.Stop()
Write-Host ("KISAK_TIMING runtime_prefix_check_seconds={0:N3} jobs={1}" -f `
    $stepTimer.Elapsed.TotalSeconds, $buildJobs)

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
$totalTimer.Stop()
Write-Host ("KISAK_TIMING web_build_total_seconds={0:N3}" -f $totalTimer.Elapsed.TotalSeconds)
