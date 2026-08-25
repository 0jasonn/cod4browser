#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build\web',
    [int64]$MaximumWasmBytes = 3340060,
    [int64]$MaximumJavaScriptBytes = 320727,
    [int64]$MaximumSiteBytes = 3671421,
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
    'browser_capabilities.mjs',
    'capability_probe_worker.mjs',
    'engine_worker.mjs',
    'index.html',
    'input_controller_core.mjs',
    'kisakcod.mjs',
    'kisakcod.wasm',
    'launcher.mjs',
    'product_checkpoint_controller.mjs',
    'product_engine_worker_host.mjs',
    'product_input_controller.mjs',
    'product_protocol.mjs',
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
$webCMakePath = Join-Path $repositoryRoot 'scripts\web\CMakeLists.txt'
$webCMake = Get-Content -Raw -LiteralPath $webCMakePath
$forbiddenObjects = @()
foreach ($listName in @(
    'KISAK_WEB_DIAGNOSTIC_SOURCES',
    'KISAK_WEB_GATE2_DIAGNOSTIC_SOURCES'
)) {
    $list = [regex]::Match(
        $webCMake,
        "(?ms)set\($listName\s+(?<body>.*?)\)")
    if (-not $list.Success) {
        throw "Canonical diagnostic source list is missing: $listName"
    }
    $forbiddenObjects += [regex]::Matches(
        $list.Groups['body'].Value,
        '"\$\{SRC_DIR\}/[^"/]+/(?<name>[^"/]+\.cpp)"') |
        ForEach-Object { $_.Groups['name'].Value }
}
$forbiddenObjects = @($forbiddenObjects | Sort-Object -Unique)
if ($forbiddenObjects.Count -eq 0) {
    throw 'Canonical diagnostic source lists did not contain any translation units.'
}
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

$expectedApplicationExports = @(
    '_KisakWeb_CompleteFsRead',
    '_KisakWeb_CompleteFsStat',
    '_KisakWeb_MountCanonicalRuntime',
    '_KisakWeb_ProbeFastfileHeader',
    '_KisakWeb_ProbeIwd',
    '_KisakWeb_ProbeLocalization',
    '_KisakWeb_QueueKeyEvent',
    '_KisakWeb_QueueMouseMove',
    '_KisakWeb_SubmitCanonicalCommand'
)
$generatedModule = Get-Content -Raw -LiteralPath (Join-Path $siteDirectory 'kisakcod.mjs')
$applicationExports = @(
    [regex]::Matches(
        $generatedModule,
        'Module\["(?<name>_KisakWeb_[A-Za-z0-9_]+)"\]') |
        ForEach-Object { $_.Groups['name'].Value } |
        Sort-Object -Unique
)
$exportDifference = @(Compare-Object $expectedApplicationExports $applicationExports)
if ($exportDifference.Count -ne 0) {
    $missing = @($exportDifference |
        Where-Object SideIndicator -eq '<=' | ForEach-Object InputObject)
    $unknown = @($exportDifference |
        Where-Object SideIndicator -eq '=>' | ForEach-Object InputObject)
    throw "Production application exports differ from the exact allowlist; missing=[$($missing -join ', ')] unknown=[$($unknown -join ', ')]"
}

$wasmBytes = (Get-Item -LiteralPath $wasmPath).Length
$javaScriptBytes = ($javascriptFiles | Measure-Object -Property Length -Sum).Sum
$siteBytes = (Get-ChildItem -LiteralPath $siteDirectory -File |
    Measure-Object -Property Length -Sum).Sum

function Write-BudgetMetric([string]$Name, [int64]$Current, [int64]$Budget) {
    $difference = $Current - $Budget
    $percentDifference = if ($Budget -eq 0) { 0.0 } else {
        100.0 * $difference / $Budget
    }
    Write-Host ("KISAK_PRODUCT_SIZE name={0} current_bytes={1} budget_bytes={2} difference_bytes={3} percentage_difference={4:N2}%" -f
        $Name, $Current, $Budget, $difference, $percentDifference)
}
Write-BudgetMetric 'wasm' $wasmBytes $MaximumWasmBytes
Write-BudgetMetric 'javascript' $javaScriptBytes $MaximumJavaScriptBytes
Write-BudgetMetric 'site' $siteBytes $MaximumSiteBytes
if ($wasmBytes -gt $MaximumWasmBytes) {
    throw "Production Wasm is $wasmBytes bytes; budget is $MaximumWasmBytes."
}
if ($javaScriptBytes -gt $MaximumJavaScriptBytes) {
    throw "Production JavaScript is $javaScriptBytes bytes; budget is $MaximumJavaScriptBytes."
}
if ($siteBytes -gt $MaximumSiteBytes) {
    throw "Production site is $siteBytes bytes; budget is $MaximumSiteBytes."
}

Write-Host "KISAK_PRODUCT_BOUNDARY wasm_bytes=$wasmBytes javascript_bytes=$javaScriptBytes site_bytes=$siteBytes wasm_exports=$wasmExportCount application_exports=$($applicationExports.Count) diagnostic_sources=$($forbiddenObjects.Count) files=$($allowedFiles.Count)"
