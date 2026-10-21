# Release script for Scraps
# Usage:  powershell -File scripts\release.ps1 [-Version 0.2.0] [-SkipBuild]
# Steps:  bump version (CMakeLists + installer.iss) -> build -> installer -> portable zip -> (optional) sign
# Upload: gh release create vX.Y.Z --repo zeddddf/scraps dist/*

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

Write-Host "== Scraps release $Version ==" -ForegroundColor Cyan

# 1. Bump version in CMakeLists.txt and installer.iss
$cmake = "CMakeLists.txt"
(Get-Content $cmake -Raw) -replace 'project\(Scraps VERSION [0-9.]+\)', "project(Scraps VERSION $Version)" | Set-Content $cmake -NoNewline
$iss = "scripts\installer.iss"
(Get-Content $iss -Raw) -replace '#define MyAppVersion "[0-9.]+"', "#define MyAppVersion `"$Version`"" | Set-Content $iss -NoNewline
Write-Host "[1/5] version bumped to $Version"

# 2. Build
if (-not $SkipBuild) {
    & cmd /c build_release.bat
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
Write-Host "[2/5] build done"

# 3. Optional code signing (self-signed dev cert: see scripts/signing.md)
$setup = "dist\Scraps-$Version-setup.exe"
New-Item -ItemType Directory -Force dist | Out-Null

# 4. Inno Setup installer
$iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
& $iscc scripts/installer.iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
Write-Host "[3/5] installer: $setup"

# Sign after packaging (covers the exact bytes users download)
$signTool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue | Select-Object -Last 1
if ($signTool) {
    & $signTool.FullName sign /a /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $setup 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { Write-Host "      signed (Authenticode)" } else { Write-Host "      signing skipped (no usable certificate)" -ForegroundColor Yellow }
} else {
    Write-Host "      signtool not found, signing skipped" -ForegroundColor Yellow
}
Write-Host "[4/5] signing done"

# 5. Portable zip
$zip = "dist\Scraps-$Version-portable.zip"
Compress-Archive -Path build/Release/Scraps.exe, build/Release/scraps-cli.exe, build/Release/scraps_screenshot.exe, build/Release/assets, LICENSE, README.md -DestinationPath $zip -Force
Write-Host "[5/5] portable: $zip"

Write-Host ""
Write-Host "Next: gh release create v$Version --repo zeddddf/scraps dist/Scraps-$Version-setup.exe dist/Scraps-$Version-portable.zip"
