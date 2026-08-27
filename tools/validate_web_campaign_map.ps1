[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RetailRoot,
    [string]$Map = 'blackout',
    [ValidateSet('chromium', 'chrome', 'msedge')]
    [string]$Browser = 'chrome',
    [switch]$Headless,
    [ValidateRange(1024, 65535)]
    [int]$Port = 8031
)

$ErrorActionPreference = 'Stop'
if ($Map -notmatch '^[a-z0-9_]+$' -or
    $Map.StartsWith('mp_') -or $Map.EndsWith('_mp')) {
    throw 'Map must name one single-player zone using lowercase letters, numbers, or underscores.'
}
$resolvedRoot = (Resolve-Path -LiteralPath $RetailRoot).Path
$required = @(
    'localization.txt',
    'zone\english\code_post_gfx.ff',
    'zone\english\ui.ff',
    'zone\english\common.ff',
    'zone\english\killhouse.ff',
    'zone\english\cargoship.ff',
    "zone\english\$Map.ff"
)
foreach ($relativePath in $required) {
    $candidate = Join-Path $resolvedRoot $relativePath
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "The campaign validation installation is missing $relativePath"
    }
}

& "$PSScriptRoot\build_web.ps1" -Configuration Release -Diagnostics
if ($LASTEXITCODE -ne 0) { throw 'The diagnostic web build failed.' }

$environmentNames = @(
    'KISAK_COD4_RETAIL_ROOT',
    'KISAK_RETAIL_PHASE3_MAP',
    'KISAK_BROWSER_CHANNEL',
    'KISAK_WEB_SITE',
    'KISAK_WEB_TEST_PORT',
    'KISAK_PLAYWRIGHT_WORKERS'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, 'Process')
}

$playwrightExitCode = 0
try {
    $env:KISAK_COD4_RETAIL_ROOT = $resolvedRoot
    $env:KISAK_RETAIL_PHASE3_MAP = $Map
    if ($Browser -eq 'chromium') {
        Remove-Item -LiteralPath 'Env:KISAK_BROWSER_CHANNEL' -ErrorAction SilentlyContinue
    } else {
        $env:KISAK_BROWSER_CHANNEL = $Browser
    }
    $env:KISAK_WEB_SITE = 'build/web-diagnostics/site-diagnostics'
    $env:KISAK_WEB_TEST_PORT = "$Port"
    $env:KISAK_PLAYWRIGHT_WORKERS = '1'
    $playwrightArguments = @(
        'exec', '--', 'playwright', 'test',
        'tests/browser/local_retail_validation.spec.mjs', '--grep', '@retail-phase3'
    )
    if (-not $Headless) { $playwrightArguments += '--headed' }
    & npm.cmd @playwrightArguments
    $playwrightExitCode = $LASTEXITCODE
} finally {
    foreach ($name in $environmentNames) {
        if ($null -eq $previousEnvironment[$name]) {
            Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
        } else {
            Set-Item -LiteralPath "Env:$name" -Value $previousEnvironment[$name]
        }
    }
}
if ($playwrightExitCode -ne 0) {
    throw 'The local campaign-map validation failed.'
}
