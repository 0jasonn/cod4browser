#requires -Version 5.1

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Diagnostics
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
$buildDirectory = Join-Path $repositoryRoot $(if ($Diagnostics) {
    'build\web-diagnostics'
} else {
    'build\web'
})
$target = if ($Diagnostics) { 'KisakCOD-web-diagnostics' } else { 'KisakCOD-web' }

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
    "-DKISAK_WEB_DIAGNOSTICS=$($Diagnostics.IsPresent.ToString().ToUpperInvariant())" `
    "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to configure the web target.'
}
$stepTimer.Stop()
Write-Host ("KISAK_TIMING web_configure_seconds={0:N3}" -f $stepTimer.Elapsed.TotalSeconds)

$stepTimer.Restart()
& $cmakeExecutable --build $buildDirectory --target $target --parallel $buildJobs
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

$siteDirectory = Join-Path $buildDirectory $(if ($Diagnostics) { 'site-diagnostics' } else { 'site' })
$requiredOutputs = @(
    'index.html',
    'launcher.mjs',
    'asset_store.mjs',
    'styles.css',
    'kisakcod.mjs',
    'kisakcod.wasm'
)
if ($Diagnostics) {
    $requiredOutputs += 'filesystem_bridge.mjs'
}
foreach ($requiredOutput in $requiredOutputs) {
    $outputPath = Join-Path $siteDirectory $requiredOutput
    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        throw "The web build completed without required output: $outputPath"
    }
}

Write-Host "Browser build ready at $siteDirectory\index.html"
$totalTimer.Stop()
Write-Host ("KISAK_TIMING web_build_total_seconds={0:N3}" -f $totalTimer.Elapsed.TotalSeconds)
