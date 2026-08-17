param(
    [Parameter(Mandatory=$true)]
    [string]$PackageDir
)

$dirs = @(
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

$dlls = @(
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

foreach ($d in $dirs) {
    $f = Join-Path $PackageDir $d
    if (Test-Path -LiteralPath $f) {
        [System.IO.Directory]::Delete($f, $true)
        Write-Host "  Removed dir: $d"
    }
}

foreach ($d in $dlls) {
    $f = Join-Path $PackageDir $d
    if (Test-Path -LiteralPath $f) {
        Remove-Item -LiteralPath $f -Force
        Write-Host "  Removed dll: $d"
    }
}

$imgDir = Join-Path $PackageDir 'imageformats'
if ((Test-Path -LiteralPath $imgDir) -and ((Get-ChildItem -LiteralPath $imgDir -File).Count -eq 0)) {
    Remove-Item -LiteralPath $imgDir -Force
    Write-Host "  Removed empty dir: imageformats"
}
