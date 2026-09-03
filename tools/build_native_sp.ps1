#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/native-sp',
    [string]$VisualStudioDirectory = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$pin = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'native_toolchain.json') | ConvertFrom-Json
$cmake = Join-Path $VisualStudioDirectory 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$compiler = Join-Path $VisualStudioDirectory "VC/Tools/MSVC/$($pin.msvcTools)/bin/Hostx64/x86/cl.exe"
foreach ($required in @($cmake, $compiler)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing native toolchain: $required"
    }
}

# Only the public Microsoft SDK is downloaded. Runtime game DLLs are never staged.
$packageName = "$($pin.d3dxPackage.ToLowerInvariant()).$($pin.d3dxVersion)"
$package = Join-Path $repositoryRoot ".tools/$packageName.nupkg"
$sdk = Join-Path $repositoryRoot ".tools/$packageName"
New-Item -ItemType Directory -Force -Path (Split-Path $package) | Out-Null
if (-not (Test-Path -LiteralPath $package -PathType Leaf)) {
    Invoke-WebRequest -UseBasicParsing -Uri "https://www.nuget.org/api/v2/package/$($pin.d3dxPackage)/$($pin.d3dxVersion)" -OutFile $package
}
if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash -ne $pin.d3dxSha256) {
    throw "D3DX package hash mismatch: $package"
}
if (-not (Test-Path -LiteralPath (Join-Path $sdk 'build/native/include/d3dx9.h'))) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($package, $sdk)
}

$build = [IO.Path]::GetFullPath((Join-Path $repositoryRoot $BuildDirectory))
& $cmake -S $repositoryRoot -B $build -G $pin.generator -A $pin.platform `
    -T "version=$($pin.msvcTools)" "-DCMAKE_SYSTEM_VERSION=$($pin.windowsSdk)" `
    -DKISAK_PLATFORM=win32 -DKISAK_OPENAL=ON -DKISAK_COPY_RUNTIME_DLLS=OFF `
    -DCMAKE_BUILD_TYPE=Release "-DDXSDK_DIR=$sdk/build/native" `
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$build/bin"
if ($LASTEXITCODE -ne 0) { throw 'Native SP configure failed.' }
& $cmake --build $build --config Release --target KisakCOD-sp --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native SP build failed.' }
Write-Host "Native SP executable: $build/bin/Release/KisakCOD-sp.exe"
