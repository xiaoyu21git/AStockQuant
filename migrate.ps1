$destDir = "G:\C++\AStockQuantEngine\bin\Debug"
$localDir = "C:\Users\wang\AppData\Local\astockquantapp-exe"
$roamingDir = "C:\Users\wang\AppData\Roaming\astockquantapp-exe"

md "$destDir\config\risk" -Force | Out-Null
md "$destDir\files" -Force | Out-Null
md "$destDir\cache\datasets" -Force | Out-Null
md "$destDir\temp" -Force | Out-Null

if (Test-Path "$localDir\datasets") {
    $size = (Get-ChildItem "$localDir\datasets" -Recurse | Measure-Object -Property Length -Sum).Sum
    Move-Item "$localDir\datasets" "$destDir\cache\" -Force
    Write-Output "MIGRATED_DATASETS: $size"
}

if (Test-Path "$localDir\factor_backtest_result.json") {
    $size = (Get-Item "$localDir\factor_backtest_result.json").Length
    Move-Item "$localDir\factor_backtest_result.json" "$destDir\files\" -Force
    Write-Output "MIGRATED_RESULT: $size"
}

if (Test-Path "$localDir\risk\risk_configuration.json") {
    $size = (Get-Item "$localDir\risk\risk_configuration.json").Length
    Move-Item "$localDir\risk\risk_configuration.json" "$destDir\config\risk\" -Force
    Write-Output "MIGRATED_RISK_LOCAL: $size"
}

if (Test-Path "$roamingDir\risk\risk_configuration.json") {
    $size = (Get-Item "$roamingDir\risk\risk_configuration.json").Length
    Move-Item "$roamingDir\risk\risk_configuration.json" "$destDir\config\risk\" -Force
    Write-Output "MIGRATED_RISK_ROAMING: $size"
}

Write-Output "--- RESIDUALS ---"
Get-ChildItem -Path $localDir, $roamingDir -Recurse -ErrorAction SilentlyContinue | Where-Object { !$_.PSIsContainer -and $_.Length -gt 1MB } | Select-Object FullName, Length
