#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$definition = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'cinematic_codec.json') | ConvertFrom-Json
$toolsDirectory = Join-Path $repositoryRoot '.tools'
$gitDirectory = Split-Path (Split-Path (Get-Command git.exe).Source)
$bash = Join-Path $gitDirectory 'bin/bash.exe'
if (-not (Test-Path -LiteralPath $bash)) { throw 'Git for Windows Bash is required for the FFmpeg build.' }
if (-not (Test-Path -LiteralPath (Join-Path $toolsDirectory 'emsdk/.emscripten'))) {
    throw 'Run tools/bootstrap_web_toolchain.ps1 first.'
}
foreach ($dependency in @($definition.ffmpeg, $definition.make)) {
    $archive = Join-Path $toolsDirectory ([Uri]$dependency.url).Segments[-1]
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -Uri $dependency.url -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $dependency.sha256) {
        throw "Dependency archive hash mismatch: $archive"
    }
    $destination = if ($dependency -eq $definition.make) { Join-Path $toolsDirectory 'msys-make' } else { $toolsDirectory }
    $sentinel = if ($dependency -eq $definition.make) { Join-Path $destination 'usr/bin/make.exe' } else { Join-Path $destination "ffmpeg-$($definition.ffmpeg.version)/configure" }
    if (-not (Test-Path -LiteralPath $sentinel)) {
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        & tar -xf $archive -C $destination
        if ($LASTEXITCODE -ne 0) { throw 'Failed to extract cinematic build dependency.' }
    }
}
& $bash (Join-Path $PSScriptRoot 'build_cinematic_codec.sh')
if ($LASTEXITCODE -ne 0) { throw 'Cinematic codec build failed.' }
