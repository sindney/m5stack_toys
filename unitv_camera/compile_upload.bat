@echo off
chcp 65001 >nul
echo ========================================
echo   UnitV Camera Viewer - Build ^& Upload
echo   (M5Stack Core side)
echo ========================================
echo.

REM ============================================
REM  Configuration - Modify these as needed
REM ============================================
REM Arduino CLI path (default: arduino-cli in PATH)
set ARDUINO_CLI=arduino-cli

REM Board FQBN (M5Stack Core)
set FQBN=m5stack:esp32:m5stack_core

REM Serial port for M5Stack upload
set PORT=COM8

REM Sketch directory
set SKETCH=%~dp0

REM ============================================

echo [1/2] Compiling...
%ARDUINO_CLI% compile --fqbn %FQBN% "%SKETCH%"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Compilation failed!
    pause
    exit /b 1
)

echo.
echo [2/2] Uploading to %PORT%...
%ARDUINO_CLI% upload --fqbn %FQBN% --port %PORT% "%SKETCH%"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Upload failed! Check COM port.
    echo Try changing PORT variable at the top of this script.
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Success! M5Stack is ready.
echo ========================================
echo.
echo M5Stack screen status:
echo   "Waiting for UnitV" = Waiting for UnitV connection
echo   Showing image       = Receiving normally
echo.
pause
