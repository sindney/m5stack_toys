@echo off
echo Installing PC Monitor dependencies...
echo.
pip install bleak psutil nvidia-ml-py
echo.
echo Installing optional AMD GPU support...
pip install pyadl
echo.
echo Done! Run pc_monitor.py to start.
pause
