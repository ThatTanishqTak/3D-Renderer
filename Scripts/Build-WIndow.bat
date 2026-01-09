@echo off
rem ============================================================================
rem Build-WIndow.bat
rem ----------------------------------------------------------------------------
rem Purpose: Configure the Visual Studio 2022 solution for the renderer project
rem          using MSVC with C++20 support. The script now focuses solely on
rem          project generation so asset/model creation can be handled by
rem          dedicated tooling.
rem ============================================================================

setlocal

rem Use local variables with explicit quoting to avoid argument parsing issues.
set "ScriptDir=%~dp0"
set "SourceDir=%ScriptDir%.."
set "BuildDir=%ScriptDir%..\Build"

rem Ensure the build directory exists before invoking CMake.
if not exist "%BuildDir%" (
    echo Creating build directory at %BuildDir%.
    mkdir "%BuildDir%"
)

rem Generate the Visual Studio 2022 project files targeting the MSVC toolset
rem with C++20 language features enabled.
rem Run CMake as a single line to prevent caret or spacing issues on Windows.
cmake -S "%SourceDir%" -B "%BuildDir%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON

if errorlevel 1 (
    echo Project generation failed. Please review the output above for details.
    pause
    exit /b 1
)

echo Project generation completed successfully.
pause