$port = New-Object System.IO.Ports.SerialPort COM6,115200
$port.Open()
$logFile = "uart_log.txt"
Write-Host "Listening... power cycle the board (logging to $logFile)"
while($true) {
    if($port.BytesToRead -gt 0) {
        $data = $port.ReadExisting()
        Write-Host -NoNewline $data
        Add-Content -Path $logFile -Value $data -NoNewline
    }
    if([Console]::KeyAvailable) {
        $key = [Console]::ReadKey($true)
        if($key.Key -eq 'Enter') {
            $port.Write("`r`n")
        } else {
            $port.Write($key.KeyChar.ToString())
        }
    }
    Start-Sleep -Milliseconds 5
}