<#
.SYNOPSIS
    Build script for AI Subscriptions Monitor.

.DESCRIPTION
    Compiles the project using MSVC (cl.exe) with full static linking.
    Supports Debug/Release configurations, clean builds, and automatic
    ICO generation from app_icon.png via Python/Pillow.

.PARAMETER Config
    Build configuration: Release (default) or Debug.

.PARAMETER Clean
    Remove the build directory before building.

.PARAMETER IconOnly
    Only regenerate app_icon.ico from app_icon.png, then exit.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Config Debug
    .\build.ps1 -Clean
    .\build.ps1 -Clean -Config Release
    .\build.ps1 -IconOnly
#>

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [switch]$Clean,
    [switch]$IconOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ------------------------------------------------------------------
# Constants
# ------------------------------------------------------------------

$ProjectDir  = $PSScriptRoot
$BuildDir    = Join-Path $ProjectDir "build"
$SrcDir      = Join-Path $ProjectDir "src"
$ExeName     = "AISubscriptionsMonitor.exe"
$IconPng     = Join-Path $ProjectDir "app_icon.png"
$IconIco     = Join-Path $ProjectDir "app_icon.ico"
$IconLightPng = Join-Path $ProjectDir "app_icon_light.png"
$IconLightIco = Join-Path $ProjectDir "app_icon_light.ico"

$Sources = @(
    "main.cpp"
    "subscription.cpp"
    "http_client.cpp"
    "renderer.cpp"
)

$LinkLibs = @(
    "winhttp.lib"
    "gdi32.lib"
    "user32.lib"
    "kernel32.lib"
    "comctl32.lib"
    "advapi32.lib"
    "dwmapi.lib"
    "shell32.lib"
    "ole32.lib"
)

# ------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------

function Write-Step([string]$Message) {
    Write-Host "`n:: $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "   $Message" -ForegroundColor Green
}

function Write-Err([string]$Message) {
    Write-Host "   $Message" -ForegroundColor Red
}

function Find-VsWhere {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) { return $vswhere }
    return $null
}

function Find-VcVars64 {
    # Try vswhere first (works for any VS edition/path)
    $vswhere = Find-VsWhere
    if ($vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($installPath) {
            $vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $vcvars) { return $vcvars }
        }
    }
    # Fallback: hardcoded common paths
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

# Import VS environment variables into current PowerShell session
function Import-VsEnv([string]$VcVarsPath) {
    $tempFile = [System.IO.Path]::GetTempFileName()
    # Run vcvars64.bat, then dump the environment to a temp file
    & cmd.exe /c "`"$VcVarsPath`" >nul 2>&1 && set > `"$tempFile`""
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize Visual Studio environment"
    }
    Get-Content $tempFile | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
        }
    }
    Remove-Item $tempFile -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------------
# ICO Generation
# ------------------------------------------------------------------

function Update-SingleIcon {
    param(
        [string]$PngPath,
        [string]$IcoPath,
        [string]$Label
    )

    if (-not (Test-Path $PngPath)) {
        Write-Err "$Label PNG not found ($PngPath), skipping ICO generation"
        return $false
    }

    # Skip if .ico is newer than .png
    if ((Test-Path $IcoPath) -and
        (Get-Item $IcoPath).LastWriteTime -ge (Get-Item $PngPath).LastWriteTime) {
        Write-Ok "$Label ICO is up to date"
        return $true
    }

    Write-Step "Generating $Label ICO from PNG"

    # Check Python + Pillow
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) {
        Write-Err "Python not found -- cannot generate .ico (install Python + Pillow)"
        return $false
    }

    $script = @"
from PIL import Image
img = Image.open(r'$($PngPath -replace "'","''")')
img.save(r'$($IcoPath -replace "'","''")', format='ICO',
         sizes=[(16,16),(32,32),(48,48),(256,256)])
"@

    python -c $script 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Err "$Label ICO generation failed (is Pillow installed?  pip install Pillow)"
        return $false
    }

    Write-Ok "$Label ICO generated (16/32/48/256 px)"
    return $true
}

function Update-Icon {
    $darkOk  = Update-SingleIcon -PngPath $IconPng      -IcoPath $IconIco      -Label "app_icon (dark-mode)"
    $lightOk = Update-SingleIcon -PngPath $IconLightPng  -IcoPath $IconLightIco  -Label "app_icon_light (light-mode)"
    return ($darkOk -and $lightOk)
}

# ------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------

function Invoke-Clean {
    Write-Step "Cleaning build directory"
    if (Test-Path $BuildDir) {
        Remove-Item $BuildDir -Recurse -Force
        Write-Ok "Removed: $BuildDir"
    } else {
        Write-Ok "Already clean"
    }
}

# ------------------------------------------------------------------
# Build
# ------------------------------------------------------------------

function Invoke-Build {
    # -- VS Environment --
    Write-Step "Setting up Visual Studio environment"
    $vcvars = Find-VcVars64
    if (-not $vcvars) {
        throw "Visual Studio 2022 with C++ workload not found"
    }
    Import-VsEnv $vcvars
    Write-Ok "Environment ready (cl.exe, rc.exe available)"

    # -- Directories --
    if (-not (Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    # -- ICO --
    Update-Icon | Out-Null

    # -- Resource Compilation --
    Write-Step "Compiling resources"
    $rcFile  = Join-Path $SrcDir "app.rc"
    $resFile = Join-Path $BuildDir "app.res"

    & rc.exe /nologo /fo $resFile $rcFile
    if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed" }
    Write-Ok "app.res compiled"

    # -- C++ Compilation & Link --
    Write-Step "Compiling C++ ($Config)"

    $outExe = Join-Path $BuildDir $ExeName
    $srcFiles = $Sources | ForEach-Object { Join-Path $SrcDir $_ }

    $clFlags = @(
        "/std:c++17"
        "/EHsc"
        "/utf-8"
        "/nologo"
        "/DUNICODE"
        "/D_UNICODE"
        "/I`"$SrcDir`""
        "/Fe:`"$outExe`""
    )

    if ($Config -eq "Debug") {
        $clFlags += "/Od", "/Zi", "/MDd", "/DDEBUG", "/D_DEBUG"
    } else {
        $clFlags += "/O2", "/MT", "/DNDEBUG"
    }

    $linkFlags = @(
        "/link"
        "`"$resFile`""
        "/SUBSYSTEM:WINDOWS"
    )

    if ($Config -eq "Debug") {
        $linkFlags += "/DEBUG"
    }

    $allArgs = $clFlags + $srcFiles + $linkFlags + $LinkLibs
    $argLine = $allArgs -join " "

    # Invoke cl.exe via cmd to handle the complex argument line
    & cmd.exe /c "cl.exe $argLine"
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed" }

    # -- Cleanup intermediate files from project root --
    Get-ChildItem -Path $ProjectDir -Filter "*.obj" -File | Remove-Item -Force

    # -- Summary --
    $exeSize = (Get-Item $outExe).Length
    $sizeKB  = [math]::Round($exeSize / 1024)

    Write-Host ""
    Write-Host "  ========================================" -ForegroundColor Green
    Write-Host "  Build successful! ($Config)" -ForegroundColor Green
    Write-Host "  Executable: $outExe" -ForegroundColor Green
    Write-Host "  Size:       $sizeKB KB" -ForegroundColor Green
    Write-Host "  ========================================" -ForegroundColor Green

    return $outExe
}

# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------

try {
    Push-Location $ProjectDir

    if ($IconOnly) {
        Update-Icon | Out-Null
        Pop-Location
        exit 0
    }

    if ($Clean) {
        Invoke-Clean
    }

    Invoke-Build

    Pop-Location
    exit 0
}
catch {
    Write-Host ""
    Write-Err "BUILD FAILED: $_"
    Pop-Location
    exit 1
}
