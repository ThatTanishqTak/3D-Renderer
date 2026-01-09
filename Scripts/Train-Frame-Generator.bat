@echo off
rem ============================================================================
rem Train-Frame-Generator.bat
rem ----------------------------------------------------------------------------
rem Purpose: Train the frame generator model from a dataset and export the ONNX
rem          model plus a training checkpoint asset.
rem Usage:   Train-Frame-Generator.bat <dataset_path>
rem ============================================================================

rem Prefer an explicit dataset path argument, otherwise default to ../Dataset/frame_00*.png.
set DATASET_PATH=%~1
if "%DATASET_PATH%"=="" (
    set DATASET_PATH=%~dp0..\Dataset\frame_00*.png
    echo No dataset argument provided. Using default: %DATASET_PATH%.
    echo Usage: Train-Frame-Generator.bat ^<dataset_path^>
)

set SCRIPT_DIR=%~dp0
set MODEL_PATH=%SCRIPT_DIR%..\bin\Assets\AI\frame_generator.onnx
set CHECKPOINT_DIR=%SCRIPT_DIR%..\bin\Assets\AI\checkpoints

rem Ensure output directories exist before running the trainer.
if not exist "%SCRIPT_DIR%..\bin\Assets\AI" (
    echo Creating AI asset directory at %SCRIPT_DIR%..\bin\Assets\AI.
    mkdir "%SCRIPT_DIR%..\bin\Assets\AI"
)

if not exist "%CHECKPOINT_DIR%" (
    echo Creating checkpoint directory at %CHECKPOINT_DIR%.
    mkdir "%CHECKPOINT_DIR%"
)

rem Invoke the training script with explicit export and checkpoint paths.
python "%SCRIPT_DIR%train_frame_generator.py" "%DATASET_PATH%" --export-path "%MODEL_PATH%" --checkpoint-path "%CHECKPOINT_DIR%\frame_generator.pt"

if errorlevel 1 (
    echo Frame generator training failed. Please review the Python output above for details.
    pause
    exit /b 1
)

echo Frame generator training completed successfully at %MODEL_PATH%.