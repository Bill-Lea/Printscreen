param(
    [string]$GamePath = "",
    [string]$PackageRoot = $PSScriptRoot,
    [switch]$InstallPapyrusSource,
    [switch]$InstallDocs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Info($msg)  { Write-Host "[INFO] $msg" }
function Write-Ok($msg)    { Write-Host "[ OK ] $msg" -ForegroundColor Green }
function Write-WarnMsg($msg){ Write-Host "[WARN] $msg" -ForegroundColor Yellow }
function Write-Fail($msg)  { Write-Host "[FAIL] $msg" -ForegroundColor Red }

function Resolve-GamePath {
    param([string]$PreferredPath)

    if ($PreferredPath -and (Test-Path (Join-Path $PreferredPath "SkyrimSE.exe"))) {
        return (Resolve-Path $PreferredPath).Path
    }

    $registryPaths = @(
        "HKLM:\SOFTWARE\Bethesda Softworks\Skyrim Special Edition",
        "HKLM:\SOFTWARE\WOW6432Node\Bethesda Softworks\Skyrim Special Edition"
    )

    foreach ($reg in $registryPaths) {
        try {
            $value = Get-ItemProperty -Path $reg -ErrorAction Stop
            if ($value.Installed Path) {
                $candidate = $value."Installed Path"
                if (Test-Path (Join-Path $candidate "SkyrimSE.exe")) {
                    return (Resolve-Path $candidate).Path
                }
            }
        } catch {
        }
    }

    $steamRoots = @(
        "$env:ProgramFiles(x86)\Steam\steamapps\common\Skyrim Special Edition",
        "$env:ProgramFiles\Steam\steamapps\common\Skyrim Special Edition"
    )

    foreach ($candidate in $steamRoots) {
        if (Test-Path (Join-Path $candidate "SkyrimSE.exe")) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "Could not locate Skyrim Special Edition. Re-run with -GamePath `"C:\Path\To\Skyrim Special Edition`"."
}

function Get-FileVersionString {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "Version target not found: $Path"
    }

    return (Get-Item $Path).VersionInfo.FileVersion
}

function Get-RuntimeVariant {
    param([string]$ExeVersion)

    # Adjust these mappings to your actual supported builds.
    # AE commonly starts at 1.6.x; older SE is 1.5.x.
    if ($ExeVersion -match '^1\.6\.') {
        return "AE"
    }
    elseif ($ExeVersion -match '^1\.5\.') {
        return "SE"
    }
    else {
        throw "Unsupported Skyrim runtime version detected: $ExeVersion"
    }
}

function Copy-FileSafe {
    param(
        [string]$Source,
        [string]$Destination
    )

    $destDir = Split-Path -Parent $Destination
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    Copy-Item -Path $Source -Destination $Destination -Force
}

try {
    Write-Info "Resolving Skyrim install path..."
    $resolvedGamePath = Resolve-GamePath -PreferredPath $GamePath
    Write-Ok "Game path: $resolvedGamePath"

    $skyrimExe = Join-Path $resolvedGamePath "SkyrimSE.exe"
    $runtimeVersion = Get-FileVersionString -Path $skyrimExe
    Write-Ok "Detected SkyrimSE.exe version: $runtimeVersion"

    $variant = Get-RuntimeVariant -ExeVersion $runtimeVersion
    Write-Ok "Selected runtime variant: $variant"

    $dllSource = Join-Path $PackageRoot "Variants\$variant\SKSE\Plugins\PrintscreenV3.dll"
    $pexSource = Join-Path $PackageRoot "Core\Release\Data\Scripts\PrintscreenV3.pex"
    $pscSource = Join-Path $PackageRoot "Optional\Source\Data\Scripts\Source\PrintscreenV3.psc"
    $readmeSource = Join-Path $PackageRoot "Optional\Docs\Readme.txt"
    $changelogSource = Join-Path $PackageRoot "Optional\Docs\Changelog.txt"

    if (-not (Test-Path $dllSource)) {
        throw "Missing DLL package file: $dllSource"
    }
    if (-not (Test-Path $pexSource)) {
        throw "Missing Papyrus compiled script: $pexSource"
    }

    $dllDest = Join-Path $resolvedGamePath "SKSE\Plugins\PrintscreenV3.dll"
    $pexDest = Join-Path $resolvedGamePath "Data\Scripts\PrintscreenV3.pex"
    $pscDest = Join-Path $resolvedGamePath "Data\Scripts\Source\PrintscreenV3.psc"
    $readmeDest = Join-Path $resolvedGamePath "Readme_PrintscreenV3.txt"
    $changelogDest = Join-Path $resolvedGamePath "Changelog_PrintscreenV3.txt"

    Write-Info "Installing core files..."
    Copy-FileSafe -Source $dllSource -Destination $dllDest
    Copy-FileSafe -Source $pexSource -Destination $pexDest
    Write-Ok "Installed DLL and compiled Papyrus script."

    if ($InstallPapyrusSource) {
        if (Test-Path $pscSource) {
            Copy-FileSafe -Source $pscSource -Destination $pscDest
            Write-Ok "Installed Papyrus source."
        } else {
            Write-WarnMsg "Papyrus source requested, but file not found: $pscSource"
        }
    }

    if ($InstallDocs) {
        if (Test-Path $readmeSource) {
            Copy-FileSafe -Source $readmeSource -Destination $readmeDest
        }
        if (Test-Path $changelogSource) {
            Copy-FileSafe -Source $changelogSource -Destination $changelogDest
        }
        Write-Ok "Installed documentation."
    }

    Write-Host ""
    Write-Ok "PrintscreenV3 installation complete."
    Write-Host "Runtime: $runtimeVersion"
    Write-Host "Variant: $variant"
    Write-Host "DLL: $dllDest"
    Write-Host "PEX: $pexDest"
}
catch {
    Write-Host ""
    Write-Fail $_.Exception.Message
    exit 1
}