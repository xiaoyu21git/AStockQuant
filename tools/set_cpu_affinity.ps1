# CPU 亲和性设置脚本 (Windows PowerShell)
# 用途：将 AStockQuantEngine 进程绑定到指定 CPU 核心，预留核心给系统/UI
# 使用方式：
#   .\set_cpu_affinity.ps1 -ProcessName "AStockQuantEngine" -ReservedCores 2

param(
    [string]$ProcessName = "AStockQuantEngine",
    [int]$ReservedCores = 2
)

try {
    $process = Get-Process -Name $ProcessName -ErrorAction Stop
    $totalCores = (Get-CimInstance Win32_Processor).NumberOfLogicalProcessors
    
    if ($totalCores -le $ReservedCores) {
        Write-Warning "Total cores ($totalCores) <= reserved cores ($ReservedCores), skipping affinity setting"
        exit 0
    }
    
    # 计算亲和性掩码：使用前 (totalCores - ReservedCores) 个核心
    $affinityMask = [math]::Pow(2, $totalCores - $ReservedCores) - 1
    
    # 设置所有进程实例的亲和性和优先级
    foreach ($p in $process) {
        try {
            $p.ProcessorAffinity = [int]$affinityMask
            $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
            Write-Host "Set $($p.Name) (PID $($p.Id)) affinity to mask 0x$([Convert]::ToString([int]$affinityMask, 16))" -ForegroundColor Green
        } catch {
            Write-Warning "Failed to set affinity for PID $($p.Id): $_"
        }
    }
    
    Write-Host "Reserved $ReservedCores cores for system (core $($totalCores - $ReservedCores)..$($totalCores - 1))" -ForegroundColor Yellow
} catch {
    Write-Error "Process '$ProcessName' not found. Ensure the application is running."
    exit 1
}