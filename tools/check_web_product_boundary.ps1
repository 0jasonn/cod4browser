#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build\web',
    [string]$BaselinePath = 'tools\web_product_size_baseline.json'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resolvedBuild = Join-Path $repositoryRoot $BuildDirectory
$siteDirectory = Join-Path $resolvedBuild 'site'
$mapPath = Join-Path $resolvedBuild 'kisakcod-production.map'
$resolvedBaseline = if ([IO.Path]::IsPathRooted($BaselinePath)) {
    $BaselinePath
} else {
    Join-Path $repositoryRoot $BaselinePath
}
if (-not (Test-Path -LiteralPath $resolvedBaseline -PathType Leaf)) {
    throw "Production size baseline is missing: $resolvedBaseline"
}
$baseline = Get-Content -Raw -LiteralPath $resolvedBaseline | ConvertFrom-Json
$headroomPercent = [double]$baseline.headroomPercent
$artifactBudgets = @{}
foreach ($name in @('wasm', 'javascript', 'site')) {
    $entry = $baseline.artifacts.$name
    $baselineBytes = [int64]$entry.baselineBytes
    $budgetBytes = [int64]$entry.budgetBytes
    $expectedBudget = [int64][math]::Ceiling(
        $baselineBytes * (1.0 + $headroomPercent / 100.0))
    if ($baselineBytes -le 0 -or $budgetBytes -ne $expectedBudget) {
        throw "Invalid $name size baseline or headroom budget in $resolvedBaseline."
    }
    $artifactBudgets[$name] = @{
        Baseline = $baselineBytes
        Budget = $budgetBytes
    }
}
$maximumWasmExports = [int]$baseline.rawWasmExportCap

$allowedFiles = @(
    'asset_profile.mjs',
    'asset_store.mjs',
    'browser_capabilities.mjs',
    'browser_quit.mjs',
    'capability_probe_worker.mjs',
    'engine_worker.mjs',
    'index.html',
    'input_controller_core.mjs',
    'kisakcod.mjs',
    'kisakcod.wasm',
    'launcher.mjs',
    'licenses.txt',
    'product_checkpoint_controller.mjs',
    'product_engine_worker_host.mjs',
    'product_mount_controller.mjs',
    'product_protocol.mjs',
    'reverb_dsp.mjs',
    'styles.css',
    'web_audio_driver.mjs',
    'web_reverb_worklet.mjs',
    'worker_sync_filesystem.mjs',
    'worker_transport.mjs'
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
    'frame-profile',
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

$expectedApplicationExports = @($baseline.applicationExportAllowlist)
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

function Write-BudgetMetric(
    [string]$Name,
    [int64]$Current,
    [int64]$Baseline,
    [int64]$Budget
) {
    $difference = $Current - $Baseline
    $percentDifference = if ($Baseline -eq 0) { 0.0 } else {
        100.0 * $difference / $Baseline
    }
    Write-Host ("KISAK_PRODUCT_SIZE name={0} current_bytes={1} baseline_bytes={2} budget_bytes={3} difference_bytes={4} percentage_difference={5:N2}%" -f
        $Name, $Current, $Baseline, $Budget, $difference, $percentDifference)
}
Write-Host ("KISAK_PRODUCT_BASELINE approved_commit={0} headroom_percent={1} reason={2}" -f
    $baseline.approvedCommit, $headroomPercent, $baseline.reason)
Write-BudgetMetric 'wasm' $wasmBytes `
    $artifactBudgets.wasm.Baseline $artifactBudgets.wasm.Budget
Write-BudgetMetric 'javascript' $javaScriptBytes `
    $artifactBudgets.javascript.Baseline $artifactBudgets.javascript.Budget
Write-BudgetMetric 'site' $siteBytes `
    $artifactBudgets.site.Baseline $artifactBudgets.site.Budget
if ($wasmBytes -gt $artifactBudgets.wasm.Budget) {
    throw "Production Wasm is $wasmBytes bytes; budget is $($artifactBudgets.wasm.Budget)."
}
if ($javaScriptBytes -gt $artifactBudgets.javascript.Budget) {
    throw "Production JavaScript is $javaScriptBytes bytes; budget is $($artifactBudgets.javascript.Budget)."
}
if ($siteBytes -gt $artifactBudgets.site.Budget) {
    throw "Production site is $siteBytes bytes; budget is $($artifactBudgets.site.Budget)."
}

Write-Host "KISAK_PRODUCT_BOUNDARY wasm_bytes=$wasmBytes javascript_bytes=$javaScriptBytes site_bytes=$siteBytes wasm_exports=$wasmExportCount application_exports=$($applicationExports.Count) files=$($allowedFiles.Count)"
