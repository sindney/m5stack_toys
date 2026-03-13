@echo off
chcp 65001 >nul
echo ========================================
echo   M5Stack 编译上传脚本
echo   使用 M5Unified 库 (ESP32 SDK 3.x)
echo ========================================
echo.

cd /d %~dp0

echo [1/2] 编译中... (需要 1-2 分钟)
C:\Users\xinhou\arduino-cli\arduino-cli.exe compile --fqbn m5stack:esp32:m5stack_core .

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ✗ 编译失败!
    pause
    exit /b 1
)

echo.
echo [2/2] 上传到 COM8...
C:\Users\xinhou\arduino-cli\arduino-cli.exe upload --fqbn m5stack:esp32:m5stack_core --port COM8 .

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ✗ 上传失败!
    pause
    exit /b 1
)

echo.
echo ========================================
echo   ✓ 成功! M5Stack 已准备就绪
echo ========================================
echo.
echo M5Stack 屏幕状态说明:
echo   "Waiting for UnitV" = 等待 UnitV 连接
echo   显示图像           = 接收正常
echo.
pause
