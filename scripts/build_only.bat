@echo off
REM NetShare - Build only (no reconfigure)
set PROJECT_ROOT=%~dp0..
set BUILD_DIR=%PROJECT_ROOT%\build\Desktop_Qt_6_8_3_MSVC2022_64bit-Release

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
echo Starting MSBuild...
msbuild "%BUILD_DIR%\NetShare.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
echo ERRORLEVEL=%ERRORLEVEL%
