@echo off
echo Compiling M5Stocks...
C:\Users\xinhou\arduino-cli\arduino-cli.exe compile --export-binaries --fqbn m5stack:esp32:m5stack_core E:\git\m5stack_toys\m5stocks
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful!
    echo.
    echo Uploading to COM8...
    C:\Users\xinhou\arduino-cli\arduino-cli.exe upload -p COM8 --fqbn m5stack:esp32:m5stack_core E:\git\m5stack_toys\m5stocks
    if %ERRORLEVEL% EQU 0 (
        echo Upload successful!
    ) else (
        echo Upload failed! Check COM port.
    )
) else (
    echo Compilation failed!
)
pause
