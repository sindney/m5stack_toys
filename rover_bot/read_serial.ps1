# XiaoChong Rover Bot — Serial Monitor & Configuration Tool
# Usage: powershell -ExecutionPolicy Bypass -File read_serial.ps1 [-Port COM3] [-Configure]

param(
    [string]$Port = "",
    [switch]$Configure
)

# Auto-detect COM port
if ($Port -eq "") {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports.Count -eq 0) {
        Write-Host "No COM ports found! Connect M5StickC Plus first." -ForegroundColor Red
        exit 1
    }
    if ($ports.Count -eq 1) {
        $Port = $ports[0]
    } else {
        Write-Host "Available COM ports:" -ForegroundColor Cyan
        for ($i = 0; $i -lt $ports.Count; $i++) {
            Write-Host "  [$i] $($ports[$i])"
        }
        $sel = Read-Host "Select port"
        $Port = $ports[[int]$sel]
    }
}

Write-Host "=== XiaoChong Serial Monitor ===" -ForegroundColor Cyan
Write-Host "Port: $Port @ 115200" -ForegroundColor Yellow
Write-Host "Press Ctrl+C to exit" -ForegroundColor DarkGray
Write-Host ""

try {
    $serial = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, One
    $serial.ReadTimeout = 1000
    $serial.Open()

    if ($Configure) {
        Write-Host "=== WiFi Configuration ===" -ForegroundColor Green
        $ssid = Read-Host "WiFi SSID"
        $pass = Read-Host "WiFi Password"
        $cmd = "WIFI:${ssid}:${pass}"
        $serial.WriteLine($cmd)
        Write-Host "Sent: $cmd" -ForegroundColor Yellow
        Start-Sleep -Seconds 2
    }

    while ($true) {
        try {
            $line = $serial.ReadLine()
            # Color coding by prefix
            if ($line -match "^\[ERR") {
                Write-Host $line -ForegroundColor Red
            } elseif ($line -match "^\[WARN" -or $line -match "^\[BAT\]") {
                Write-Host $line -ForegroundColor Yellow
            } elseif ($line -match "^\[ASR\]" -or $line -match "^\[CMD\]") {
                Write-Host $line -ForegroundColor Green
            } elseif ($line -match "^\[NAV\]") {
                Write-Host $line -ForegroundColor Magenta
            } elseif ($line -match "^\[SCAN\]") {
                Write-Host $line -ForegroundColor Cyan
            } elseif ($line -match "^\[MODE\]") {
                Write-Host $line -ForegroundColor White -BackgroundColor DarkBlue
            } else {
                Write-Host $line
            }
        } catch [TimeoutException] {
            # Normal timeout, continue
        }

        # Check for user input to send commands
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq "W") {
                Write-Host "> Sending: forward" -ForegroundColor Yellow
                # Could send test commands here
            }
        }
    }
}
catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
}
finally {
    if ($serial -and $serial.IsOpen) {
        $serial.Close()
    }
    Write-Host "`nSerial port closed." -ForegroundColor DarkGray
}
