# configure.ps1
param(
    [switch]$d,
    [switch]$r
)
$buildType = "Debug"
if ($r) { $buildType = "Release" }

$root = Split-Path $PSScriptRoot -Parent
Push-Location $root

# TODO: no msvc, use mingw or gcc or clang

# Use the same profile for host and build so compiler/settings always match.
# (Don't name this $profile - that's a PowerShell automatic variable.)
$conanProfile = "./profiles/windows-x64-$buildType.txt"

function Fail([string]$msg) {
    Write-Host $msg -ForegroundColor Red
    Pop-Location
    exit 1
}

function CreateConanPackage {
    param (
        [string]$path,
        [string]$version
    )

    Write-Host "==> Creating Conan package: $path" -ForegroundColor Cyan

    # No separate `conan install` of the recipe: `conan create` resolves deps itself.
    # -pr:h / -pr:b use the project profile so package IDs match the root install.
    # -s:b build_type=Release keeps build-context tools (cmake, ninja, ...) at Release
    # so they come from the cache instead of being rebuilt as Debug.
    conan create "./scripts/conan-ext/$path/" `
      --version="$version" `
      --build=missing `
      -pr:h="$conanProfile" `
      -pr:b="$conanProfile" `
      -s:b build_type=Release

    if ($LASTEXITCODE -ne 0) { Fail "conan create failed for $path" }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere not found - is Visual Studio installed?" }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Fail "No VS install with MSVC toolchain found" }

# --- find a real, complete MSVC toolset (has cl.exe) - no hardcoded version ---
$msvcToolset = Get-ChildItem "$vsPath\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue |
Where-Object { Test-Path (Join-Path $_.FullName "bin\Hostx64\x64\cl.exe") } |
Sort-Object Name -Descending |
Select-Object -First 1

if (-not $msvcToolset) { Fail "No MSVC toolset with cl.exe found under $vsPath\VC\Tools\MSVC" }
$msvcRoot = $msvcToolset.FullName
Write-Host "==> MSVC toolset: $($msvcToolset.Name)" -ForegroundColor Cyan

# --- find newest Windows SDK that actually has headers+libs - no hardcoded version ---
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdkVersion = Get-ChildItem "$sdkRoot\Include" -Directory -ErrorAction SilentlyContinue |
Where-Object { Test-Path (Join-Path $_.FullName "um\windows.h") } |
Sort-Object Name -Descending |
Select-Object -First 1

if (-not $sdkVersion) { Fail "No usable Windows SDK found under $sdkRoot\Include" }
$sdkVer = $sdkVersion.Name
Write-Host "==> Windows SDK: $sdkVer" -ForegroundColor Cyan

# --- assemble env vars manually, bypassing vcvars.bat entirely ---
$binDir = Join-Path $msvcRoot "bin\Hostx64\x64"
$incDirs = @(
    (Join-Path $msvcRoot "include"),
    (Join-Path $sdkRoot "Include\$sdkVer\ucrt"),
    (Join-Path $sdkRoot "Include\$sdkVer\um"),
    (Join-Path $sdkRoot "Include\$sdkVer\shared"),
    (Join-Path $sdkRoot "Include\$sdkVer\winrt")
)
$libDirs = @(
    (Join-Path $msvcRoot "lib\x64"),
    (Join-Path $sdkRoot "Lib\$sdkVer\ucrt\x64"),
    (Join-Path $sdkRoot "Lib\$sdkVer\um\x64")
)

$sdkBinDir = Join-Path $sdkRoot "bin\$sdkVer\x64"
$env:PATH = "$binDir;$sdkBinDir;$env:PATH"
$env:INCLUDE = ($incDirs -join ";")
$env:LIB = ($libDirs -join ";")
$env:LIBPATH = $env:LIB

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) { Fail "cl.exe still not resolvable after manual env setup" }
Write-Host "Using compiler: $($cl.Source)" -ForegroundColor DarkGray

# Local packages first: the root conanfile requires them.
CreateConanPackage -path "openapv/all" -version "1.1.3.0"
CreateConanPackage -path "SDL/all"     -version "3.4.18"

Write-Host "==> Running conan install..." -ForegroundColor Cyan
conan install . --profile=$conanProfile --build=missing
if ($LASTEXITCODE -ne 0) { Fail "conan install failed" }

Write-Host "==> Running cmake configure..." -ForegroundColor Cyan
cmake --preset conan2-ninja
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }

Write-Host "==> Done. Run: ./scripts/build.ps1" -ForegroundColor Green
Pop-Location