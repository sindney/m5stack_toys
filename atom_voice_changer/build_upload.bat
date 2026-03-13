@echo off
chcp 65001 >nul
echo ============================================
echo   AtomS3R Voice Changer - Build ^& Upload
echo ============================================
echo.

set ARDUINO_CLI=C:\Users\xinhou\arduino-cli\arduino-cli.exe
set FQBN=m5stack:esp32:m5stack_atoms3r
set SKETCH=E:\git\m5stack_toys\atom_voice_changer
set PORT=COM11

echo [1/2] Compiling...
%ARDUINO_CLI% compile --fqbn %FQBN% %SKETCH%

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful!
    echo.
    echo [2/2] Uploading to %PORT%...
    %ARDUINO_CLI% upload -p %PORT% --fqbn %FQBN% %SKETCH%
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ============================================
        echo   Upload complete! Device will reboot.
        echo ============================================
    ) else (
        echo.
        echo Upload FAILED! Check COM port connection.
    )
) else (
    echo.
    echo Compilation FAILED! Check errors above.
)

echo.
pause
