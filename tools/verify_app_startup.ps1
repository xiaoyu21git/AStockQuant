param(
    [string]$ExePath = "bin/Debug/astockquantapp-exe.exe",
    [int]$StartupTimeoutSeconds = 15,
    [string]$LogDirectory = "bin/Debug/logs/startup-check",
    [switch]$KeepRunning
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\$Path"))
}

function Read-LogTail {
    param(
        [string]$Path,
        [int]$LineCount = 20
    )

    if (-not (Test-Path $Path)) {
        return @()
    }

    return Get-Content -Path $Path -Tail $LineCount
}

$resolvedExePath = Resolve-RepoPath $ExePath
if (-not (Test-Path $resolvedExePath)) {
    throw "Executable not found: $resolvedExePath"
}

$resolvedLogDirectory = Resolve-RepoPath $LogDirectory
New-Item -ItemType Directory -Force -Path $resolvedLogDirectory | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stdoutLog = Join-Path $resolvedLogDirectory "startup_stdout_$timestamp.log"
$stderrLog = Join-Path $resolvedLogDirectory "startup_stderr_$timestamp.log"
$workingDirectory = Split-Path -Parent $resolvedExePath

Write-Output "[verify_app_startup] exe=$resolvedExePath"
Write-Output "[verify_app_startup] workdir=$workingDirectory"
Write-Output "[verify_app_startup] stdout=$stdoutLog"
Write-Output "[verify_app_startup] stderr=$stderrLog"

$process = Start-Process -FilePath $resolvedExePath `
    -WorkingDirectory $workingDirectory `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError $stderrLog `
    -PassThru

$deadline = (Get-Date).AddSeconds([Math]::Max(1, $StartupTimeoutSeconds))
$windowReady = $false

while ((Get-Date) -lt $deadline) {
    $process.Refresh()

    if ($process.HasExited) {
        break
    }

    if ($process.MainWindowHandle -ne 0 -or -not [string]::IsNullOrWhiteSpace($process.MainWindowTitle)) {
        $windowReady = $true
        break
    }

    Start-Sleep -Milliseconds 200
}

$process.Refresh()

if ($process.HasExited) {
    $stdoutTail = Read-LogTail -Path $stdoutLog
    $stderrTail = Read-LogTail -Path $stderrLog

    Write-Output "[verify_app_startup] status=exited_early"
    Write-Output "[verify_app_startup] exitCode=$($process.ExitCode)"
    if ($stdoutTail.Count -gt 0) {
        Write-Output "[verify_app_startup] stdout_tail="
        $stdoutTail | ForEach-Object { Write-Output $_ }
    }
    if ($stderrTail.Count -gt 0) {
        Write-Output "[verify_app_startup] stderr_tail="
        $stderrTail | ForEach-Object { Write-Output $_ }
    }

    exit 1
}

if (-not $windowReady) {
    Write-Output "[verify_app_startup] status=running_without_window"
    Write-Output "[verify_app_startup] processId=$($process.Id)"
    Write-Output "[verify_app_startup] mainWindowTitle=$($process.MainWindowTitle)"

    if (-not $KeepRunning) {
        Stop-Process -Id $process.Id -Force
        Write-Output "[verify_app_startup] processStopped=true"
    }

    exit 2
}

Write-Output "[verify_app_startup] status=window_ready"
Write-Output "[verify_app_startup] processId=$($process.Id)"
Write-Output "[verify_app_startup] mainWindowTitle=$($process.MainWindowTitle)"

if ($KeepRunning) {
    Write-Output "[verify_app_startup] processKeptRunning=true"
    exit 0
}

Stop-Process -Id $process.Id -Force
Write-Output "[verify_app_startup] processStopped=true"
exit 0