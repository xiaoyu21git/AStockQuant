param(
    [string]$ProcessName = "astockquantapp-exe",
    [int]$StartupTimeoutSeconds = 15,
    [switch]$RequireWindow
)

$ErrorActionPreference = "Stop"

$deadline = (Get-Date).AddSeconds([Math]::Max(1, $StartupTimeoutSeconds))
$process = $null

while ((Get-Date) -lt $deadline) {
    $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($process) {
        $process.Refresh()
        if (-not $RequireWindow -or $process.MainWindowHandle -ne 0 -or -not [string]::IsNullOrWhiteSpace($process.MainWindowTitle)) {
            break
        }
    }

    Start-Sleep -Milliseconds 200
}

if (-not $process) {
    Write-Output "[check_app_running] status=not_running"
    Write-Output "[check_app_running] processName=$ProcessName"
    exit 1
}

$process.Refresh()

if ($RequireWindow -and $process.MainWindowHandle -eq 0 -and [string]::IsNullOrWhiteSpace($process.MainWindowTitle)) {
    Write-Output "[check_app_running] status=running_without_window"
    Write-Output "[check_app_running] processId=$($process.Id)"
    Write-Output "[check_app_running] processName=$ProcessName"
    exit 2
}

Write-Output "[check_app_running] status=running"
Write-Output "[check_app_running] processId=$($process.Id)"
Write-Output "[check_app_running] mainWindowTitle=$($process.MainWindowTitle)"
Write-Output "[check_app_running] processStopped=false"
exit 0