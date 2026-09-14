# Run the port with a scripted input file, photograph it, and print what the log
# says happened. One command per verification run.
#
#   powershell -ExecutionPolicy Bypass -File tools/test_run.ps1 -Script tools/scripts/flight.txt `
#       -OutDir shots/run -Count 12 -IntervalSeconds 4.5 [-StartDelaySeconds 3] [-Env "PW64_FRAME_STATS=1"]
#
# The port is started without a console, so it logs to pw64.log in the settings
# directory; that file is cleared first and filtered afterwards to the state
# transcript, frame-rate lines and anything that went wrong. Captures are of the
# desktop region the window occupies, so nothing should cover the game while
# this runs (see tools/capture_window.ps1).

param(
    [string]$Script = "",
    [string]$OutDir = "shots/run",
    [int]$Count = 10,
    [double]$IntervalSeconds = 5.0,
    [double]$StartDelaySeconds = 3.0,
    [string[]]$Env = @(),
    [string]$Exe = "build/Pilotwings64Recomp.exe",
    [string]$Rom = "game.z64"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

$log = Join-Path $env:LOCALAPPDATA "Pilotwings64Recomp\pw64.log"
Remove-Item $log -ErrorAction SilentlyContinue

if ($Script -ne "") {
    $env:PW64_INPUT_SCRIPT = (Resolve-Path $Script).Path
} else {
    Remove-Item Env:PW64_INPUT_SCRIPT -ErrorAction SilentlyContinue
}
foreach ($pair in $Env) {
    $name, $value = $pair -split "=", 2
    Set-Item -Path "Env:$name" -Value $value
}

$process = Start-Process -FilePath $Exe -ArgumentList $Rom -PassThru
Start-Sleep -Seconds $StartDelaySeconds
& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "capture_window.ps1") `
    -OutDir $OutDir -Count $Count -IntervalSeconds $IntervalSeconds
if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force
    Write-Output "stopped the port after the captures"
} else {
    Write-Output "the port exited by itself with code $($process.ExitCode)"
}
Start-Sleep -Seconds 1

if (Test-Path $log) {
    Get-Content $log | Select-String -Pattern "state:|frames per second|LOOKUP|CRASH|error|Error|dropped|assert" |
        Select-Object -First 60 | ForEach-Object { $_.Line }
}
