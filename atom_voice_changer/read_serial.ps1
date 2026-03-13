# Read serial output for diagnostics
$portName = "COM11"
$baudRate = 115200
$timeout = 15  # seconds

try {
    Start-Sleep -Seconds 2  # Wait for device to boot
    $port = New-Object System.IO.Ports.SerialPort($portName, $baudRate)
    $port.ReadTimeout = 2000
    $port.DtrEnable = $false
    $port.RtsEnable = $false
    $port.Open()
    Write-Host "=== Serial Monitor (${timeout}s) ==="
    
    $end = (Get-Date).AddSeconds($timeout)
    while ((Get-Date) -lt $end) {
        try {
            $line = $port.ReadLine()
            Write-Host $line
        } catch [System.TimeoutException] {
            # continue
        }
    }
    $port.Close()
    Write-Host "=== Done ==="
} catch {
    Write-Host "Error: $_"
}
