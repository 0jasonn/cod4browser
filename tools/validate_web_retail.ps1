[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RetailRoot,
    [ValidateRange(1024, 65535)]
    [int]$Port = 8030
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $RetailRoot).Path
$required = @(
    'localization.txt',
    'zone\english\code_post_gfx.ff',
    'zone\english\ui.ff',
    'zone\english\common.ff',
    'zone\english\killhouse.ff',
    'zone\english\cargoship.ff'
)
foreach ($relativePath in $required) {
    $candidate = Join-Path $resolvedRoot $relativePath
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "The validation installation is missing $relativePath"
    }
}

& "$PSScriptRoot\build_web.ps1" -Configuration Release -Diagnostics
if ($LASTEXITCODE -ne 0) { throw 'The diagnostic web build failed.' }

$env:KISAK_COD4_RETAIL_ROOT = $resolvedRoot
$env:KISAK_WEB_SITE = 'build/web-diagnostics/site-diagnostics'
$env:KISAK_WEB_TEST_PORT = "$Port"
$env:KISAK_PLAYWRIGHT_WORKERS = '1'
& npm.cmd exec -- playwright test tests/browser/local_retail_validation.spec.mjs
if ($LASTEXITCODE -ne 0) { throw 'The local retail validation matrix failed.' }
