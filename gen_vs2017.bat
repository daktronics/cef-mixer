@echo off

rem set CEF_ROOT=c:\cef\cef_binary_3.3325.1742.g5caccda_windows64

set BASE_DIR=%~dp0
rem echo %BASE_DIR%

mkdir "%BASE_DIR%\build"

cd "%BASE_DIR%\build"

rem use "Visual Studio 15" as the generator for x86 builds
rem cmake -G "Visual Studio 15" "%BASE_DIR%"
cmake -G "Visual Studio 15 Win64" "%BASE_DIR%"

cd %BASE_DIR%