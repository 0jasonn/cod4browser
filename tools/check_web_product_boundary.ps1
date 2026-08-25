#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build\web',
    [int64]$MaximumWasmBytes = 3730562,
    [int64]$MaximumJavaScriptBytes = 553678,
    [int64]$MaximumSiteBytes = 4294371,
    [int]$MaximumWasmExports = 24
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resolvedBuild = Join-Path $repositoryRoot $BuildDirectory
$siteDirectory = Join-Path $resolvedBuild 'site'
$mapPath = Join-Path $resolvedBuild 'kisakcod-production.map'

$allowedFiles = @(
    'asset_profile.mjs',
    'asset_store.mjs',
    'engine_protocol.mjs',
    'engine_worker.mjs',
    'engine_worker_host.mjs',
    'index.html',
    'kisakcod.mjs',
    'kisakcod.wasm',
    'launcher.mjs',
    'product_input_controller.mjs',
    'styles.css',
    'web_audio_driver.mjs',
    'worker_sync_filesystem.mjs'
)

foreach ($name in $allowedFiles) {
    $path = Join-Path $siteDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Production artifact is missing $name."
    }
}

$unexpected = Get-ChildItem -LiteralPath $siteDirectory -File |
    Where-Object { $_.Name -notin $allowedFiles }
if ($unexpected) {
    throw "Production artifact contains unexpected files: $($unexpected.Name -join ', ')"
}

$forbiddenJavaScript = @(
    '\bcallProbe\b',
    '\btestControl\b',
    'test-control',
    '_KisakWeb_Test',
    '_KisakWeb_StartArchive',
    '_KisakWeb_StartQcommon',
    '_KisakWeb_StartRetail',
    'filesystem_bridge',
    'renderer-comparison',
    'retail-census',
    'gate2'
)
$javascriptFiles = Get-ChildItem -LiteralPath $siteDirectory -Filter '*.mjs' -File
foreach ($file in $javascriptFiles) {
    $text = Get-Content -Raw -LiteralPath $file.FullName
    foreach ($pattern in $forbiddenJavaScript) {
        if ($text -match $pattern) {
            throw "Production JavaScript $($file.Name) contains forbidden pattern: $pattern"
        }
    }
}

if (-not (Test-Path -LiteralPath $mapPath -PathType Leaf)) {
    throw "Production linker map is missing: $mapPath"
}
$map = Get-Content -Raw -LiteralPath $mapPath
$forbiddenObjects = @(
    'web_archive_job.cpp',
    'web_engine_asset.cpp',
    'web_engine_surface.cpp',
    'web_fastfile_source.cpp',
    'web_fastfile_world.cpp',
    'web_fastfile_zone_registry.cpp',
    'web_fastfile_zone_stream.cpp',
    'web_qcommon_preinit.cpp',
    'web_qcommon_runtime.cpp',
    'web_renderer_comparison.cpp',
    'web_retail_',
    'web_sound_alias_catalog.cpp'
)
foreach ($name in $forbiddenObjects) {
    if ($map.Contains($name)) {
        throw "Production linker map contains diagnostic object: $name"
    }
}

$wasmPath = Join-Path $siteDirectory 'kisakcod.wasm'
$wasmDis = Join-Path $repositoryRoot '.tools\emsdk\upstream\bin\wasm-dis.exe'
if (-not (Test-Path -LiteralPath $wasmDis -PathType Leaf)) {
    throw "Pinned wasm-dis is missing: $wasmDis"
}
$wasmTextPath = Join-Path $resolvedBuild 'kisakcod-export-scan.wat'
try {
    & $wasmDis $wasmPath -o $wasmTextPath
    if ($LASTEXITCODE -ne 0) { throw "wasm-dis failed with exit code $LASTEXITCODE." }
    $wasmText = Get-Content -Raw -LiteralPath $wasmTextPath
    $wasmExportCount = [regex]::Matches($wasmText, '(?m)^\s*\(export ').Count
    if ($wasmExportCount -gt $MaximumWasmExports) {
        throw "Production Wasm has $wasmExportCount exports; budget is $MaximumWasmExports."
    }
} finally {
    Remove-Item -LiteralPath $wasmTextPath -Force -ErrorAction SilentlyContinue
}

$wasmBytes = (Get-Item -LiteralPath $wasmPath).Length
$javaScriptBytes = ($javascriptFiles | Measure-Object -Property Length -Sum).Sum
$siteBytes = (Get-ChildItem -LiteralPath $siteDirectory -File |
    Measure-Object -Property Length -Sum).Sum
if ($wasmBytes -gt $MaximumWasmBytes) {
    throw "Production Wasm is $wasmBytes bytes; budget is $MaximumWasmBytes."
}
if ($javaScriptBytes -gt $MaximumJavaScriptBytes) {
    throw "Production JavaScript is $javaScriptBytes bytes; budget is $MaximumJavaScriptBytes."
}
if ($siteBytes -gt $MaximumSiteBytes) {
    throw "Production site is $siteBytes bytes; budget is $MaximumSiteBytes."
}

Write-Host "KISAK_PRODUCT_BOUNDARY wasm_bytes=$wasmBytes javascript_bytes=$javaScriptBytes site_bytes=$siteBytes wasm_exports=$wasmExportCount files=$($allowedFiles.Count)"
