@echo off
echo Running build script...

set SCRIPT_DIR=%~dp0
set MODEL_PATH=%SCRIPT_DIR%..\Assets\AI\frame_generator.onnx

if exist "%MODEL_PATH%" (
    echo Using trained frame generator located at %MODEL_PATH%.
) else (
    echo Trained frame generator not found. Generating diagnostic identity model instead.
    python "%SCRIPT_DIR%generate_ai_sample_model.py" --output "%MODEL_PATH%"
)

cmake -B "%SCRIPT_DIR%..\Build" "%SCRIPT_DIR%..\"

echo Build generation completed. Press any key to exit.
pause >nul