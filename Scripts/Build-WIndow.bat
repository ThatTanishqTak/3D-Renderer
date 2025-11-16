@echo off
rem ============================================================================
rem Build-WIndow.bat
rem ----------------------------------------------------------------------------
rem Purpose: Configure the Visual Studio 2022 solution for the renderer project
rem          using MSVC with C++20 support. The script now focuses solely on
rem          project generation so asset/model creation can be handled by
rem          dedicated tooling.
rem ============================================================================

set SCRIPT_DIR=%~dp0
set SOURCE_DIR=%SCRIPT_DIR%..\
set BUILD_DIR=%SCRIPT_DIR%..\Build

rem Ensure the build directory exists before invoking CMake.
if not exist "%BUILD_DIR%" (
    echo Creating build directory at %BUILD_DIR%.
    mkdir "%BUILD_DIR%"
)

rem Generate the Visual Studio 2022 project files targeting the MSVC toolset
rem with C++20 language features enabled.
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_CXX_STANDARD=20 ^
    -DCMAKE_CXX_STANDARD_REQUIRED=ON

if errorlevel 1 (
    echo Project generation failed. Please review the output above for details.
    pase
    exit /b 1
)

echo Project generation completed successfully.
pause