@echo off
setlocal
set ARDUINO_CLI=D:\SDK\arduino-cli\arduino-cli.exe
set FQBN=m5stack:esp32:m5stack_stickc_plus
set SKETCH_DIR=%~dp0
set PORT=COM3

if "%1"=="upload" goto :upload
if "%1"=="monitor" goto :monitor

:compile
echo [BUILD] Compiling rover firmware...
%ARDUINO_CLI% compile --fqbn %FQBN% "%SKETCH_DIR%"
if errorlevel 1 (
    echo [BUILD] Compile FAILED
    exit /b 1
)
echo [BUILD] Compile OK
if "%1"=="" goto :eof

:upload
echo [BUILD] Uploading to %PORT%...
if "%2" NEQ "" set PORT=%2
%ARDUINO_CLI% compile --fqbn %FQBN% "%SKETCH_DIR%"
if errorlevel 1 (
    echo [BUILD] Compile FAILED
    exit /b 1
)
%ARDUINO_CLI% upload -p %PORT% --fqbn %FQBN% "%SKETCH_DIR%"
if errorlevel 1 (
    echo [BUILD] Upload FAILED
    exit /b 1
)
echo [BUILD] Upload OK
goto :eof

:monitor
echo [BUILD] Opening serial monitor on %PORT%...
if "%2" NEQ "" set PORT=%2
%ARDUINO_CLI% monitor -p %PORT% -c baudrate=115200
goto :eof
