#  process.ps1, main program that configure's each resources

param(
    [string]$ConfigPath = ""
)

# PAths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$modRoot   = Split-Path -Parent $scriptDir

if (-not $ConfigPath) {
    $ConfigPath = Join-Path $modRoot "config\settings.json"
}

# Pre Defined
$cfg = @{
    ProcessName                  = "farcry4"
    Priority                     = "High"
    UseSmartAffinity             = $true
    WaitSecondsAfterLaunch       = 30
    EnableMemoryCleanup          = $false
    MemoryCleanupIntervalSeconds = 120
    EnableTimerResolution        = $true
    ChangePowerPlan              = $false
    LogToFile                    = $true
}

# Load config
if (Test-Path $ConfigPath) {
    try {
        $json = Get-Content $ConfigPath -Raw | ConvertFrom-Json
        foreach ($key in @($cfg.Keys)) {
            if ($json.PSObject.Properties.Name -contains $key) {
                $cfg[$key] = $json.$key
            }
        }
    } catch {
        Write-Warning "Could not read settings.json -- using defaults."
    }
}


$logPath = Join-Path $modRoot "optimizer.log"

function Log {
    param([string]$msg)
    $line = "[$(Get-Date -Format 'HH:mm:ss')] $msg"
    Write-Host $line
    if ($cfg.LogToFile) {
        Add-Content -Path $logPath -Value $line -ErrorAction SilentlyContinue
    }
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public class WinTimer {
    [DllImport("winmm.dll")] public static extern int timeBeginPeriod(uint p);
    [DllImport("winmm.dll")] public static extern int timeEndPeriod(uint p);
}

public class WSTrim {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool SetProcessWorkingSetSize(IntPtr hProcess, IntPtr min, IntPtr max);
}
"@ -ErrorAction SilentlyContinue

# Priority
$priorityMap = @{
    "Idle"        = [System.Diagnostics.ProcessPriorityClass]::Idle
    "BelowNormal" = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
    "Normal"      = [System.Diagnostics.ProcessPriorityClass]::Normal
    "AboveNormal" = [System.Diagnostics.ProcessPriorityClass]::AboveNormal
    "High"        = [System.Diagnostics.ProcessPriorityClass]::High
    "RealTime"    = [System.Diagnostics.ProcessPriorityClass]::RealTime
}


function Get-CpuVendor {
    param($cpu)

    $name = "$($cpu.Manufacturer)"

    if ($name -match "AMD") {
        return "AMD"
    } elseif ($name -match "Intel") {
        return "Intel"
    }

    # Intel core profile By Default
    return "Intel"
}

function Get-AffinityMask {
    param($cpu)

    $logical = [int]$cpu.NumberOfLogicalProcessors
    $vendor  = Get-CpuVendor -cpu $cpu

    # Core indexes:
    #   Intel cores: 0, 2, 4, 6
    #   AMD cores: 2, 4, 6, 8
    if ($vendor -eq "AMD") {
        $coreIndexes = @(2, 4, 6, 8)
    } else {
        $coreIndexes = @(0, 2, 4, 6)
    }

    $mask = [long]0
    foreach ($idx in $coreIndexes) {
        if ($idx -lt $logical) {
            $mask = $mask -bor (1L -shl $idx)
        }
    }

    if ($mask -le 0) {
        $all  = (1L -shl $logical) - 1
        $mask = $all -band (-bnot 1L)
    }

    return @{
        Mask        = [IntPtr][long]$mask
        Vendor      = $vendor
        CoreIndexes = $coreIndexes
    }
}

function Invoke-MemoryTrim {
    param($processes)
    foreach ($p in $processes) {
        try {
            [WSTrim]::SetProcessWorkingSetSize($p.Handle, [IntPtr](-1), [IntPtr](-1)) | Out-Null
        } catch { }
    }
    Log "Memory working-set trimmed."
}


function Get-ActivePowerPlan {
    try {
        $line = powercfg /getactivescheme 2>$null
        if ($line -match 'GUID:\s+(\S+)') { return $Matches[1] }
    } catch { }
    return $null
}

$HIGH_PERF_GUID = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"


Log "=== FarCry4-FpsFixMod Started ==="
Log "Config: $ConfigPath"

$timerSet = $false
if ($cfg.EnableTimerResolution) {
    try {
        $result = [WinTimer]::timeBeginPeriod(1)
        if ($result -eq 0) {
            $timerSet = $true
            Log "Timer resolution set to 1ms (was 15.6ms default). Reduces hitches."
        } else {
            Log "Could not set timer resolution (code $result)."
        }
    } catch {
        Log "Timer resolution skipped: $_"
    }
}


$savedPowerPlan = $null
if ($cfg.ChangePowerPlan) {
    try {
        $savedPowerPlan = Get-ActivePowerPlan
        powercfg /setactive $HIGH_PERF_GUID 2>$null
        Log "Power plan switched to High Performance. Was: $savedPowerPlan"
    } catch {
        Log "Could not change power plan (may need admin): $_"
    }
}


Log "Waiting for $($cfg.ProcessName) to launch..."

$timeout = 300
$elapsed = 0

while (-not (Get-Process $cfg.ProcessName -ErrorAction SilentlyContinue)) {
    Start-Sleep 2
    $elapsed += 2
    if ($elapsed -ge $timeout) {
        Log "Timed out waiting for game. Exiting."
        if ($timerSet) { [WinTimer]::timeEndPeriod(1) | Out-Null }
        if ($savedPowerPlan) { powercfg /setactive $savedPowerPlan 2>$null }
        exit 1
    }
}

Log "Game detected. Waiting $($cfg.WaitSecondsAfterLaunch)s for full initialization..."
Start-Sleep $cfg.WaitSecondsAfterLaunch

$proc = Get-Process $cfg.ProcessName -ErrorAction SilentlyContinue

if (-not $proc) {
    Log "Process ended before optimization could apply."
    if ($timerSet) { [WinTimer]::timeEndPeriod(1) | Out-Null }
    if ($savedPowerPlan) { powercfg /setactive $savedPowerPlan 2>$null }
    exit 1
}


$origPriority = $proc[0].PriorityClass
$origAffinity = $proc[0].ProcessorAffinity
Log "Saved original Priority=$origPriority  Affinity=$origAffinity"


$targetPriority = $priorityMap[$cfg.Priority]
if (-not $targetPriority) {
    $targetPriority = [System.Diagnostics.ProcessPriorityClass]::High
}

try {
    foreach ($p in $proc) { $p.PriorityClass = $targetPriority }
    Log "Priority set to: $($cfg.Priority)"
} catch {
    Log "Could not set priority: $_"
}


if ($cfg.UseSmartAffinity) {
    try {
        $cpu    = Get-CimInstance Win32_Processor
        $result = Get-AffinityMask -cpu $cpu
        $mask   = $result.Mask

        foreach ($p in $proc) { $p.ProcessorAffinity = $mask }

        $cores      = [int]$cpu.NumberOfCores
        $logical    = [int]$cpu.NumberOfLogicalProcessors
        $coreList   = ($result.CoreIndexes -join ", ")
        Log "CPU detected: $($result.Vendor) | $cores physical / $logical logical"
        Log "Affinity set. Mask=$mask | Using $($result.Vendor) core profile: cores $coreList"
    } catch {
        Log "Could not set affinity: $_"
    }
}

Log "All optimizations applied. Monitoring until game exits..."
Log "---"


$cleanupTimer = 0

while ($true) {
    Start-Sleep 5
    $cleanupTimer += 5

    $running = Get-Process $cfg.ProcessName -ErrorAction SilentlyContinue
    if (-not $running) { break }

    if ($cfg.EnableMemoryCleanup -and ($cleanupTimer -ge $cfg.MemoryCleanupIntervalSeconds)) {
        $cleanupTimer = 0
        Invoke-MemoryTrim -processes $running
    }
}


Log "Game exited. Restoring settings..."


if ($timerSet) {
    try {
        [WinTimer]::timeEndPeriod(1) | Out-Null
        Log "Timer resolution restored to Windows default."
    } catch { }
}


if ($savedPowerPlan) {
    try {
        powercfg /setactive $savedPowerPlan 2>$null
        Log "Power plan restored to: $savedPowerPlan"
    } catch { }
}


try {
    $lingering = Get-Process $cfg.ProcessName -ErrorAction SilentlyContinue
    if ($lingering) {
        foreach ($p in $lingering) {
            $p.PriorityClass     = $origPriority
            $p.ProcessorAffinity = $origAffinity
        }
        Log "Process settings restored."
    }
} catch { }

Log "Process Ended."
