param()

$ErrorActionPreference = "Continue"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release"
$PackageDir = Join-Path $ProjectRoot "dist\NetShare"
$QtDir = "C:\Qt\6.8.3\msvc2022_64"

Write-Host "=== NetShare Package Script ===" -ForegroundColor Cyan
Write-Host ""

$exePath = Join-Path $BuildDir "src\Release\NetShare.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "ERROR: NetShare.exe not found at $exePath" -ForegroundColor Red
    Write-Host "Please build the project first"
    exit 1
}

if (Test-Path $PackageDir) {
    Write-Host "Cleaning previous package..."
    [System.IO.Directory]::Delete($PackageDir, $true)
}

New-Item -ItemType Directory -Path $PackageDir | Out-Null

Write-Host "Copying NetShare.exe..."
Copy-Item $exePath $PackageDir

Write-Host "Running windeployqt..."
$windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
$qmlDir = Join-Path $ProjectRoot "src\gui\qml"
& $windeployqt --release --no-translations --no-opengl-sw --no-virtualkeyboard --no-svg --qmldir $qmlDir --dir $PackageDir (Join-Path $PackageDir "NetShare.exe")

Write-Host ""
Write-Host "Removing unused components..." -ForegroundColor Yellow

$removeDirs = @(
    'qml\QtQuick\Controls\FluentWinUI3',
    'qml\QtQuick\Controls\Material',
    'qml\QtQuick\Controls\Imagine',
    'qml\QtQuick\Controls\Universal',
    'qml\QtQuick\Controls\Fusion',
    'qml\QtQuick\Controls\Windows',
    'qml\QtQuick\NativeStyle',
    'qml\QtQuick\VirtualKeyboard',
    'qmltooling'
)

$removeDlls = @(
    'sqldrivers\qsqlmimer.dll',
    'sqldrivers\qsqlodbc.dll',
    'sqldrivers\qsqlpsql.dll',
    'imageformats\qwebp.dll',
    'imageformats\qtiff.dll',
    'imageformats\qicns.dll',
    'imageformats\qgif.dll',
    'imageformats\qico.dll',
    'imageformats\qsvg.dll',
    'imageformats\qtga.dll',
    'imageformats\qwbmp.dll',
    'Qt6VirtualKeyboard.dll'
)

foreach ($d in $removeDirs) {
    $f = Join-Path $PackageDir $d
    if (Test-Path -LiteralPath $f) {
        [System.IO.Directory]::Delete($f, $true)
        Write-Host "  Removed dir: $d" -ForegroundColor Green
    }
}

foreach ($d in $removeDlls) {
    $f = Join-Path $PackageDir $d
    if (Test-Path -LiteralPath $f) {
        [System.IO.File]::Delete($f)
        Write-Host "  Removed dll: $d" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Copying web assets..."
$webDir = Join-Path $PackageDir "web"
if (-not (Test-Path $webDir)) { New-Item -ItemType Directory -Path $webDir | Out-Null }
Copy-Item (Join-Path $ProjectRoot "web\*") $webDir

Write-Host "Copying i18n files..."
$i18nDir = Join-Path $PackageDir "i18n"
if (-not (Test-Path $i18nDir)) { New-Item -ItemType Directory -Path $i18nDir | Out-Null }
$qmFiles = Get-ChildItem -Path $BuildDir -Filter "netshare_*.qm" -Recurse
if ($qmFiles.Count -gt 0) {
    foreach ($qm in $qmFiles) {
        Copy-Item $qm.FullName $i18nDir
        Write-Host "  Copied: $($qm.Name)" -ForegroundColor Green
    }
} else {
    Write-Host "  WARNING: No .qm files found in build directory" -ForegroundColor Yellow
}

$totalSize = (Get-ChildItem $PackageDir -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ""
Write-Host "=== Package created at: $PackageDir ===" -ForegroundColor Cyan
Write-Host "Total size: $([math]::Round($totalSize/1MB,2)) MB"
Write-Host ""
Write-Host "Done!"
