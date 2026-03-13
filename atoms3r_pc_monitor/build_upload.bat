@echo off
echo Compiling AtomS3R PC Monitor...
C:\Users\xinhou\arduino-cli\arduino-cli.exe compile --fqbn m5stack:esp32:m5stack_atoms3r E:\git\m5stack_toys\atoms3r_pc_monitor
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful!
    echo.
    echo Uploading to COM10...
    C:\Users\xinhou\arduino-cli\arduino-cli.exe upload -p COM10 --fqbn m5stack:esp32:m5stack_atoms3r E:\git\m5stack_toys\atoms3r_pc_monitor
    if %ERRORLEVEL% EQU 0 (
        echo Upload successful!
    ) else (
        echo Upload failed! Check COM port.
    )
) else (
    echo Compilation failed!
)
pause
