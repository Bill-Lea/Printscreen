# PrintScreen SKSE Plugin Build Script
# Usage: .\build.ps1 [-Config Release|Debug] [-Clean] [-Configure]

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Configure
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$VsInstallPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"

# Import VS Developer Shell
Import-Module "$VsInstallPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $VsInstallPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64"

Write-Host "=== PrintScreen Build Script ===" -ForegroundColor Cyan
Write-Host "Configuration: $Config" -ForegroundColor Yellow

# Clean build directory if requested
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    $Configure = $true
}

# Configure if needed or requested
if ($Configure -or -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "Configuring with CMake..." -ForegroundColor Yellow
    cmake -B $BuildDir -S $ProjectRoot --preset=release
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Configuration failed!" -ForegroundColor Red
        exit 1
    }
}

# Build
Write-Host "Building $Config..." -ForegroundColor Yellow
cmake --build $BuildDir --config $Config

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "=== Build Successful ===" -ForegroundColor Green
    $dll = Join-Path $BuildDir "bin\Printscreen.dll"
    if (Test-Path $dll) {
        $size = [math]::Round((Get-Item $dll).Length / 1KB, 1)
        Write-Host "Output: $dll ($size KB)" -ForegroundColor Green
    }
} else {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
