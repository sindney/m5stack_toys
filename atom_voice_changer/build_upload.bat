@echo off
chcp 65001 >nul
echo ============================================
echo   AtomS3R Voice Changer - Build ^& Upload
echo ============================================
echo.

REM ============================================
REM  Configuration - Modify these as needed
REM ============================================
REM Arduino CLI path (default: arduino-cli in PATH)
set ARDUINO_CLI=arduino-cli

REM Board FQBN (Fully Qualified Board Name)
set FQBN=m5stack:esp32:m5stack_atoms3r

REM Serial port for upload (modify for your system)
set PORT=COM11

REM Sketch directory (use script's directory by default)
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
        echo   Upload complete! Device will reboot.
        echo ============================================
    ) else (
        echo.
        echo Upload FAILED! Check COM port connection.
        echo Try changing PORT variable at the top of this script.
    )
) else (
    echo.
    echo Compilation FAILED! Check errors above.
)

echo.
pause
