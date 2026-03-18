# Read serial output for diagnostics
# Usage: .\read_serial.ps1 [-Port COM11] [-BaudRate 115200] [-Timeout 15]

param(
    [string]$Port = "COM11",
    [int]$BaudRate = 115200,
    [int]$Timeout = 15
)

try {
    Start-Sleep -Seconds 2  # Wait for device to boot
    $serialPort = New-Object System.IO.Ports.SerialPort($Port, $BaudRate)
    $serialPort.ReadTimeout = 2000
    $serialPort.DtrEnable = $false
    $serialPort.RtsEnable = $false
    $serialPort.Open()
    Write-Host "=== Serial Monitor ($Port @ $BaudRate baud, ${Timeout}s) ==="
    
    $end = (Get-Date).AddSeconds($Timeout)
    while ((Get-Date) -lt $end) {
        try {
            $line = $serialPort.ReadLine()
            Write-Host $line
        } catch [System.TimeoutException] {
            # continue
        }
    }
    $serialPort.Close()
    Write-Host "=== Done ==="
} catch {
    Write-Host "Error: $_"
    Write-Host "Tip: Check if the port '$Port' is correct and not in use."
}
