@echo off
chcp 65001 >nul
echo ============================================
echo   AtomS3R PC Monitor - Build ^& Upload
echo ============================================
echo.

REM ============================================
REM  Configuration - Modify these as needed
REM ============================================
REM Arduino CLI path (default: arduino-cli in PATH)
set ARDUINO_CLI=arduino-cli

REM Board FQBN
set FQBN=m5stack:esp32:m5stack_atoms3r

REM Serial port for upload
set PORT=COM10

REM Sketch directory
set SKETCH=%~dp0

REM ============================================

echo [1/2] Compiling...
%ARDUINO_CLI% compile --fqbn %FQBN% "%SKETCH%"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful!
    echo.
    echo [2/2] Uploading to %PORT%...
    %ARDUINO_CLI% upload -p %PORT% --fqbn %FQBN% "%SKETCH%"
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ============================================
        echo   Upload successful!
        echo ============================================
    ) else (
        echo.
        echo Upload failed! Check COM port.
        echo Try changing PORT variable at the top of this script.
    )
) else (
    echo.
    echo Compilation failed!
)

pause
