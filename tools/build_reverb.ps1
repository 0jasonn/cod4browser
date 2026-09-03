#requires -Version 5.1
[CmdletBinding()]
param([ValidateRange(1, 16)][int]$Jobs = 2)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$pin = Get-Content -Raw (Join-Path $PSScriptRoot 'web_toolchain.json') | ConvertFrom-Json
$emsdk = Join-Path $repositoryRoot '.tools/emsdk'
$cmake = Join-Path $emsdk "cmake/$($pin.cmake)_64bit/bin/cmake.exe"
$ninja = Join-Path $emsdk "ninja/$($pin.ninja)_64bit/ninja.exe"
$toolchain = Join-Path $emsdk 'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'
$build = Join-Path $repositoryRoot 'build/reverb-wasm'
$env:EM_CONFIG = Join-Path $emsdk '.emscripten'
foreach ($required in @($cmake, $ninja, $toolchain, $env:EM_CONFIG)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing pinned web toolchain: $required. Run tools/bootstrap_web_toolchain.ps1."
    }
}
& $cmake -S (Join-Path $repositoryRoot 'scripts/web/reverb') -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Failed to configure the OpenAL reverb component.' }
& $cmake --build $build --target reverb_dsp --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Failed to build the OpenAL reverb component.' }
