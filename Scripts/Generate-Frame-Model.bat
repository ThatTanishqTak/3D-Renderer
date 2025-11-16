@echo off
rem ============================================================================
rem Generate-Frame-Model.bat
rem ----------------------------------------------------------------------------
rem Purpose: Create or refresh the diagnostic ONNX frame generator model without
rem          invoking the broader CMake project generation pipeline. Use this
rem          when you only need to produce the AI asset.
rem ============================================================================

set SCRIPT_DIR=%~dp0
set MODEL_PATH=%SCRIPT_DIR%..\bin\Assets\AI\frame_generator.onnx

rem Ensure the output directory exists so the Python script can write the model.
if not exist "%SCRIPT_DIR%..\bin\Assets\AI" (
    echo Creating AI asset directory at %SCRIPT_DIR%..\bin\Assets\AI.
    mkdir "%SCRIPT_DIR%..\bin\Assets\AI"
)

rem Invoke the existing Python helper to generate the sample model.
python "%SCRIPT_DIR%generate_ai_sample_model.py" --output "%MODEL_PATH%"

if errorlevel 1 (
    echo Model generation failed. Please review the Python output above for details.
    exit /b 1
)

echo Model generation completed successfully at %MODEL_PATH%.
pause