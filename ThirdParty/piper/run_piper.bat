@echo off
setlocal

cd /d "%~dp0"

:: Check that all arguments are present
if "%~1"=="" (
    echo Usage: %~nx0 "text to speak" "path\to\model.onnx" "path\to\output.wav"
    exit /b 1
)
if "%~2"=="" (
    echo [ERROR] Missing model path.
    echo Usage: %~nx0 "text to speak" "path\to\model.onnx" "path\to\output.wav"
    exit /b 1
)
if "%~3"=="" (
    echo [ERROR] Missing output path.
    echo Usage: %~nx0 "text to speak" "path\to\model.onnx" "path\to\output.wav"
    exit /b 1
)

:: Debug: Show full command
echo [DEBUG] Command: echo %1 ^| piper.exe -m "%~2" -f "%~3" 

:: Run Piper with piped text
cmd.exe /C "echo "%~1" | piper.exe -m "%~2" -f "%~3""

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Piper failed with exit code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
) else (
    echo [SUCCESS] Piper completed. Output saved to: %~3
)

endlocal