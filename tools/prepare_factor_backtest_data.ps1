param(
    [string]$PythonExe = "python",
    [string]$MysqlExe = "mysql",
    [string]$DbHost = "127.0.0.1",
    [int]$DbPort = 3306,
    [string]$DbName = "astock_quant",
    [string]$DbUser = "root",
    [string]$DbPassword = "123456a",
    [string]$RequirementsPath = "astock_engine/requirements.txt",
    [string]$BaseSchemaPath = "astock_init.sql",
    [string]$EnhancedSchemaPath = "astock_init_enhanced.sql",
    [string]$DailyReportPath = "build/factor_backtest_data_prep/daily_update_report.json",
    [string]$SummaryPath = "build/factor_backtest_data_prep/run_summary.json",
    [string]$LogDirectory = "build/factor_backtest_data_prep/logs",
    [switch]$SkipSchemaInit,
    [switch]$SkipEnhancedSchema,
    [switch]$SkipInstallPythonDeps,
    [switch]$SkipFullImport,
    [switch]$SkipExtendedImport,
    [switch]$SkipCheckDbStatus,
    [switch]$SkipDailyPipeline,
    [switch]$SkipVerifyDaily,
    [switch]$SkipValidateFields,
    [switch]$ContinueOnStepFailure,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Path is required"
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\$Path"))
}

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

function Resolve-ExecutablePath {
    param([string]$CommandName)

    if (Test-Path $CommandName) {
        return [System.IO.Path]::GetFullPath($CommandName)
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if (-not $command) {
        if ($CommandName -ieq "mysql") {
            $fallbackPaths = @(
                "C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe",
                "C:\Program Files\MySQL\MySQL Workbench 8.0\mysql.exe"
            )
            foreach ($fallbackPath in $fallbackPaths) {
                if (Test-Path $fallbackPath) {
                    return [System.IO.Path]::GetFullPath($fallbackPath)
                }
            }
        }

        throw "Command not found: $CommandName"
    }

    return $command.Source
}

function Quote-DisplayToken {
    param([string]$Text)

    if ($null -eq $Text) {
        return '""'
    }

    if ($Text -match '\s') {
        return '"' + $Text.Replace('"', '\"') + '"'
    }

    return $Text
}

function Format-CommandDisplay {
    param(
        [string]$Executable,
        [string[]]$Arguments
    )

    $parts = @((Quote-DisplayToken $Executable))
    foreach ($argument in ($Arguments | Where-Object { $null -ne $_ })) {
        $parts += Quote-DisplayToken ([string]$argument)
    }
    return ($parts -join ' ')
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    $startTime = Get-Date
    Write-Host "==> $Name"
    $status = "success"
    $message = ""

    try {
        & $Action
        Write-Host "[ok] $Name" -ForegroundColor Green
    } catch {
        $status = "failed"
        $message = $_.Exception.Message
        Write-Host "[failed] $Name :: $message" -ForegroundColor Red
        if (-not $ContinueOnStepFailure) {
            $script:StepResults += [pscustomobject]@{
                name = $Name
                status = $status
                message = $message
                startedAt = $startTime.ToString("s")
                endedAt = (Get-Date).ToString("s")
            }
            throw
        }
    }

    $script:StepResults += [pscustomobject]@{
        name = $Name
        status = $status
        message = $message
        startedAt = $startTime.ToString("s")
        endedAt = (Get-Date).ToString("s")
    }
}

function Invoke-ExternalCommand {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$Description
    )

    $display = Format-CommandDisplay -Executable $Executable -Arguments $Arguments
    if ($DryRun) {
        Write-Host "[dry-run] $display"
        return
    }

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Invoke-PythonScript {
    param(
        [string]$ScriptPath,
        [string[]]$Arguments,
        [string]$Description
    )

    $resolvedScriptPath = Resolve-RepoPath $ScriptPath
    if (-not (Test-Path $resolvedScriptPath)) {
        throw "Python script not found: $resolvedScriptPath"
    }

    $commandArguments = @($resolvedScriptPath)
    if ($Arguments) {
        $commandArguments += $Arguments
    }

    Invoke-ExternalCommand -Executable $script:ResolvedPythonExe -Arguments $commandArguments -Description $Description
}

function Invoke-MysqlQuery {
    param(
        [string]$Sql,
        [string]$Description
    )

    $arguments = @(
        "-h", $DbHost,
        "-P", [string]$DbPort,
        "-u", $DbUser,
        "-p$DbPassword",
        "-e", $Sql
    )

    Invoke-ExternalCommand -Executable $script:ResolvedMysqlExe -Arguments $arguments -Description $Description
}

function Invoke-MysqlFile {
    param(
        [string]$SqlFilePath,
        [string]$Description
    )

    $resolvedSqlFilePath = Resolve-RepoPath $SqlFilePath
    if (-not (Test-Path $resolvedSqlFilePath)) {
        throw "SQL file not found: $resolvedSqlFilePath"
    }

    $arguments = @(
        "-h", $DbHost,
        "-P", [string]$DbPort,
        "-u", $DbUser,
        "-p$DbPassword",
        $DbName
    )

    $display = Format-CommandDisplay -Executable $script:ResolvedMysqlExe -Arguments ($arguments + @("<", $resolvedSqlFilePath))
    if ($DryRun) {
        Write-Host "[dry-run] $display"
        return
    }

    $sqlText = Get-Content -Path $resolvedSqlFilePath -Raw -Encoding UTF8
    $sqlText | & $script:ResolvedMysqlExe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Test-GmTokenConfigured {
    if (-not [string]::IsNullOrWhiteSpace($env:GM_TOKEN)) {
        return $true
    }
    if (-not [string]::IsNullOrWhiteSpace($env:ASTOCK_GM_TOKEN)) {
        return $true
    }

    $candidatePaths = @(
        (Resolve-RepoPath "config/trading_connection.json"),
        (Resolve-RepoPath "bin/Debug/config/trading_connection.json"),
        (Resolve-RepoPath "build/tests/config/trading_connection.json")
    )

    foreach ($candidatePath in $candidatePaths) {
        if (-not (Test-Path $candidatePath)) {
            continue
        }

        try {
            $payload = Get-Content -Path $candidatePath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($payload -and -not [string]::IsNullOrWhiteSpace([string]$payload.token)) {
                return $true
            }
        } catch {
        }
    }

    return $false
}

$script:StepResults = @()
$repoRoot = Resolve-RepoPath "."
$resolvedLogDirectory = Resolve-RepoPath $LogDirectory
$resolvedDailyReportPath = Resolve-RepoPath $DailyReportPath
$resolvedSummaryPath = Resolve-RepoPath $SummaryPath

Ensure-Directory $resolvedLogDirectory
Ensure-Directory (Split-Path -Parent $resolvedDailyReportPath)
Ensure-Directory (Split-Path -Parent $resolvedSummaryPath)

$transcriptPath = Join-Path $resolvedLogDirectory ("prepare_factor_backtest_data_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".log")
Start-Transcript -Path $transcriptPath | Out-Null

try {
    if (-not $DryRun) {
        $script:ResolvedPythonExe = Resolve-ExecutablePath $PythonExe
        $script:ResolvedMysqlExe = Resolve-ExecutablePath $MysqlExe
    } else {
        $script:ResolvedPythonExe = $PythonExe
        $script:ResolvedMysqlExe = $MysqlExe
    }

    $defaultDbConfig = ($DbHost -eq "127.0.0.1") -and ($DbPort -eq 3306) -and ($DbUser -eq "root") -and ($DbPassword -eq "123456a") -and ($DbName -eq "astock_quant")
    if (-not $defaultDbConfig -and (-not $SkipFullImport -or -not $SkipCheckDbStatus -or -not $SkipDailyPipeline -or -not $SkipVerifyDaily -or -not $SkipValidateFields)) {
        Write-Warning "Python data scripts in this repo still use in-file MYSQL_CONFIG values. The Db* parameters only affect mysql CLI initialization unless you align those scripts first."
    }

    Write-Host "Repository root: $repoRoot"
    Write-Host "Transcript: $transcriptPath"
    Write-Host "Summary: $resolvedSummaryPath"

    if (-not $SkipSchemaInit) {
        Invoke-Step -Name "Create database" -Action {
            Invoke-MysqlQuery -Sql "CREATE DATABASE IF NOT EXISTS $DbName CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;" -Description "Create database"
        }

        Invoke-Step -Name "Import base schema" -Action {
            Invoke-MysqlFile -SqlFilePath $BaseSchemaPath -Description "Import base schema"
        }

        if (-not $SkipEnhancedSchema) {
            Invoke-Step -Name "Import enhanced schema" -Action {
                Invoke-MysqlFile -SqlFilePath $EnhancedSchemaPath -Description "Import enhanced schema"
            }
        }
    }

    if (-not $SkipInstallPythonDeps) {
        Invoke-Step -Name "Install Python requirements" -Action {
            $resolvedRequirementsPath = Resolve-RepoPath $RequirementsPath
            if (-not (Test-Path $resolvedRequirementsPath)) {
                throw "Requirements file not found: $resolvedRequirementsPath"
            }

            Invoke-ExternalCommand -Executable $script:ResolvedPythonExe -Arguments @("-m", "pip", "install", "-r", $resolvedRequirementsPath) -Description "Install Python requirements"
        }
    }

    if (-not $SkipFullImport) {
        Invoke-Step -Name "Run AkShare market supplement" -Action {
            Invoke-PythonScript -ScriptPath "tools/import_from_akshare.py" -Arguments @() -Description "AkShare market supplement"
        }
    }

    if (-not $SkipExtendedImport) {
        Invoke-Step -Name "Import extended factor data" -Action {
            Invoke-PythonScript -ScriptPath "tools/import_extended_market_data.py" -Arguments @(
                "--data-type", "all",
                "--start-date", "2020-01-01",
                "--end-date", (Get-Date -Format "yyyy-MM-dd")
            ) -Description "Import extended factor data"
        }
    }

    if (-not $SkipCheckDbStatus) {
        Invoke-Step -Name "Check database status" -Action {
            Invoke-PythonScript -ScriptPath "tools/check_db_status.py" -Arguments @() -Description "Check database status"
        }
    }

    if (-not $SkipDailyPipeline) {
        Invoke-Step -Name "Run daily update pipeline" -Action {
            Invoke-PythonScript -ScriptPath "tools/run_daily_update_pipeline.py" -Arguments @(
                "--daily-close-profile",
                "--with-financial",
                "--include-history-gaps",
                "--report-file", $resolvedDailyReportPath
            ) -Description "Run daily update pipeline"
        }
    }

    if (-not $SkipVerifyDaily) {
        Invoke-Step -Name "Verify daily_bar alignment" -Action {
            Invoke-PythonScript -ScriptPath "tools/verify_daily_update.py" -Arguments @("--sample-limit", "20") -Description "Verify daily update"
        }
    }

    if (-not $SkipValidateFields) {
        Invoke-Step -Name "Validate backtest fields" -Action {
            Invoke-PythonScript -ScriptPath "tools/validate_backtest_data_fields.py" -Arguments @(
                "--daily-window", "180",
                "--financial-window", "3650"
            ) -Description "Validate backtest data fields"
        }
    }
} catch {
    $script:RunExceptionMessage = $_.Exception.Message
} finally {
    $failedSteps = @($script:StepResults | Where-Object { $_.status -eq "failed" })
    $overallStatus = if ($script:RunExceptionMessage) {
        "failed"
    } elseif ($failedSteps.Count -gt 0) {
        "partial_failed"
    } else {
        "success"
    }

    $summary = [ordered]@{
        status = $overallStatus
        dryRun = [bool]$DryRun
        transcriptPath = $transcriptPath
        dailyReportPath = $resolvedDailyReportPath
        summaryGeneratedAt = (Get-Date).ToString("s")
        errorMessage = $script:RunExceptionMessage
        steps = @($script:StepResults)
        note = "daily_bar and financial_indicator have official import paths; news tables still require separate preparation."
    }

    $summary | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedSummaryPath -Encoding UTF8

    Write-Host "Run summary written to $resolvedSummaryPath"
    Stop-Transcript | Out-Null

    if ($overallStatus -ne "success") {
        exit 1
    }
}