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
if not exist "%BUILD_DIR%\src\NetShare.exe" (
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
copy "%BUILD_DIR%\src\NetShare.exe" "%PACKAGE_DIR%\" >nul

REM Run windeployqt
echo Running windeployqt...
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --dir "%PACKAGE_DIR%" "%PACKAGE_DIR%\NetShare.exe"

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
