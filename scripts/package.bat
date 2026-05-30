@echo off
REM NetShare - Package Release Build
REM Uses windeployqt to bundle Qt dependencies

set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release
set PACKAGE_DIR=%PROJECT_ROOT%\dist\NetShare
set QT_DIR=C:\Qt\6.8.3\msvc2022_64

echo === NetShare Package Script ===
echo.

REM Check build exists
if not exist "%BUILD_DIR%\src\Release\NetShare.exe" (
    echo ERROR: NetShare.exe not found in build directory
    echo Please build the project first
    exit /b 1
)

REM Clean previous package
if exist "%PACKAGE_DIR%" (
    echo Cleaning previous package...
    rmdir /s /q "%PACKAGE_DIR%"
)

REM Create package directory
mkdir "%PACKAGE_DIR%"

REM Copy main executable
echo Copying NetShare.exe...
copy "%BUILD_DIR%\src\Release\NetShare.exe" "%PACKAGE_DIR%\" >nul

REM Run windeployqt
echo Running windeployqt...
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-virtualkeyboard --no-svg --qmldir "%PROJECT_ROOT%\src\gui\qml" --dir "%PACKAGE_DIR%" "%PACKAGE_DIR%\NetShare.exe"

REM ===== Slim down package (medium plan) =====
echo Removing unused components...

REM 1. Remove unused Quick Controls styles (only Basic is needed)
rmdir /s /q "%PACKAGE_DIR%\qml\QtQuick\Controls\FluentWinUI3"
rmdir /s /q "%PACKAGE_DIR%\qml\QtQuick\Controls\Material"
rmdir /s /q "%PACKAGE_DIR%\qml\QtQuick\Controls\Imagine"
rmdir /s /q "%PACKAGE_DIR%\qml\QtQuick\Controls\Universal"
rmdir /s /q "%PACKAGE_DIR%\qml\QtQuick\Controls\Fusion"
rmdir /s /q "%PACKAGE_DIR%\qml\QtQuick\Controls\Windows"
rmdir /s /q "%PACKAGE_DIR%\qml\QtQuick\NativeStyle"

REM 2. Remove QML debugging tools (not needed in Release)
rmdir /s /q "%PACKAGE_DIR%\qmltooling"

REM 3. Remove unused SQL drivers (only QSQLITE is needed)
del /q "%PACKAGE_DIR%\sqldrivers\qsqlmimer.dll"
del /q "%PACKAGE_DIR%\sqldrivers\qsqlodbc.dll"
del /q "%PACKAGE_DIR%\sqldrivers\qsqlpsql.dll"

REM 4. Remove unused image format plugins (only qjpeg is needed)
del /q "%PACKAGE_DIR%\imageformats\qwebp.dll"
del /q "%PACKAGE_DIR%\imageformats\qtiff.dll"
del /q "%PACKAGE_DIR%\imageformats\qicns.dll"
del /q "%PACKAGE_DIR%\imageformats\qgif.dll"
del /q "%PACKAGE_DIR%\imageformats\qico.dll"
del /q "%PACKAGE_DIR%\imageformats\qsvg.dll"
del /q "%PACKAGE_DIR%\imageformats\qtga.dll"
del /q "%PACKAGE_DIR%\imageformats\qwbmp.dll"

REM 5. Remove Qt6VirtualKeyboard (desktop app doesn't need it)
del /q "%PACKAGE_DIR%\Qt6VirtualKeyboard.dll"

echo Done removing unused components.

REM Copy web assets
echo Copying web assets...
if not exist "%PACKAGE_DIR%\web" mkdir "%PACKAGE_DIR%\web"
copy "%PROJECT_ROOT%\web\*" "%PACKAGE_DIR%\web\" >nul 2>&1

REM Copy database init script if exists
if exist "%PROJECT_ROOT%\scripts\init_db.sql" (
    copy "%PROJECT_ROOT%\scripts\init_db.sql" "%PACKAGE_DIR%\" >nul
)

echo.
echo === Package created at: %PACKAGE_DIR% ===
echo.
echo Files:
dir /b "%PACKAGE_DIR%"
echo.
echo Total size:
powershell -command "(Get-ChildItem '%PACKAGE_DIR%' -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB" 2>nul

echo.
echo Done!
