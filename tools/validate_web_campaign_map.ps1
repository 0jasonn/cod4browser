[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RetailRoot,
    [string]$Map = 'blackout',
    [ValidateSet('chromium', 'chrome', 'msedge')]
    [string]$Browser = 'chrome',
    [switch]$Mission,
    [ValidateSet('progression', 'full')]
    [string]$MissionStage = 'full',
    [ValidateSet('none', 'author', 'replay')]
    [string]$RouteMode = 'none',
    [string]$RoutePath,
    [string]$RouteOutput,
    [switch]$Headless,
    [ValidateRange(1024, 65535)]
    [int]$Port = 8031
)

$ErrorActionPreference = 'Stop'
if ($Map -notmatch '^[a-z0-9_]+$' -or
    $Map.StartsWith('mp_') -or $Map.EndsWith('_mp')) {
    throw 'Map must name one single-player zone using lowercase letters, numbers, or underscores.'
}
if ($RouteMode -ne 'none' -and -not $Mission) {
    throw 'Route authoring and replay require -Mission.'
}
if ($RouteMode -eq 'author' -and $Headless) {
    throw 'Manual route authoring requires a headed browser.'
}
if ($RoutePath -and $RouteMode -ne 'replay') {
    throw '-RoutePath is only used with -RouteMode replay.'
}
$resolvedRoutePath = $null
if ($RoutePath) {
    $resolvedRoutePath = (Resolve-Path -LiteralPath $RoutePath).Path
} elseif ($RouteMode -eq 'replay') {
    throw '-RoutePath is required for route replay.'
}
$resolvedRouteOutput = $null
if ($RouteMode -eq 'author') {
    if (-not $RouteOutput) { throw '-RouteOutput is required for route authoring.' }
    $routeOutputParent = Split-Path -Parent $RouteOutput
    if (-not $routeOutputParent) { $routeOutputParent = '.' }
    $resolvedRouteOutput = Join-Path (Resolve-Path -LiteralPath $routeOutputParent).Path `
        (Split-Path -Leaf $RouteOutput)
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
    'KISAK_RETAIL_MISSION_MAP',
    'KISAK_RETAIL_MISSION_STAGE',
    'KISAK_RETAIL_ROUTE_MODE',
    'KISAK_RETAIL_ROUTE_PATH',
    'KISAK_RETAIL_ROUTE_OUTPUT',
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
    if ($Mission) {
        Remove-Item -LiteralPath 'Env:KISAK_RETAIL_PHASE3_MAP' -ErrorAction SilentlyContinue
        $env:KISAK_RETAIL_MISSION_MAP = $Map
        $env:KISAK_RETAIL_MISSION_STAGE = $MissionStage
    } else {
        $env:KISAK_RETAIL_PHASE3_MAP = $Map
        Remove-Item -LiteralPath 'Env:KISAK_RETAIL_MISSION_MAP' -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath 'Env:KISAK_RETAIL_MISSION_STAGE' -ErrorAction SilentlyContinue
    }
    if ($RouteMode -eq 'none') {
        Remove-Item -LiteralPath 'Env:KISAK_RETAIL_ROUTE_MODE' -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath 'Env:KISAK_RETAIL_ROUTE_PATH' -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath 'Env:KISAK_RETAIL_ROUTE_OUTPUT' -ErrorAction SilentlyContinue
    } else {
        $env:KISAK_RETAIL_ROUTE_MODE = $RouteMode
        if ($RouteMode -eq 'replay') {
            $env:KISAK_RETAIL_ROUTE_PATH = $resolvedRoutePath
            Remove-Item -LiteralPath 'Env:KISAK_RETAIL_ROUTE_OUTPUT' -ErrorAction SilentlyContinue
        } else {
            $env:KISAK_RETAIL_ROUTE_OUTPUT = $resolvedRouteOutput
            Remove-Item -LiteralPath 'Env:KISAK_RETAIL_ROUTE_PATH' -ErrorAction SilentlyContinue
        }
    }
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
        'tests/browser/local_retail_validation.spec.mjs', '--grep',
        $(if ($RouteMode -eq 'author') { '@retail-route-author' }
            elseif ($Mission) { '@retail-mission' } else { '@retail-phase3' })
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
    throw $(if ($Mission) {
        'The local mission validation failed.'
    } else {
        'The local campaign-map validation failed.'
    })
}
