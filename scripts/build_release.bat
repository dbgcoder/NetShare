@echo off
REM NetShare - Clean configure and build (Release)
set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

if exist "%BUILD_DIR%\CMakeCache.txt" (
    rmdir /s /q "%BUILD_DIR%"
)

cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
if %ERRORLEVEL% NEQ 0 (
    echo CMake configure failed
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 (
    echo Build failed
    exit /b 1
)
echo Build succeeded
