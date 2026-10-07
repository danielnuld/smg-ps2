# Record a test run for the project video: start PCSX2 quietly, toggle its own capture, wait, stop, and keep the mp4
# as videos\<date>_<name>_<commit>.mp4 (videos\ is not in git). usage: record.ps1 <name> [elf] [seconds]
param([Parameter(Mandatory)][string]$Name, [string]$Elf = "D:\smg-ps2\engine\slice-bench.elf", [int]$Seconds = 36)
$src = "$env:USERPROFILE\Documents\PCSX2\videos"
$t0 = Get-Date
& "$PSScriptRoot\run_quiet.ps1" $Elf 4 | Out-Null
& "$PSScriptRoot\capture.ps1"
Start-Sleep $Seconds
& "$PSScriptRoot\capture.ps1"
Start-Sleep 3
Get-Process | Where-Object { $_.Name -like 'pcsx2*' } | Stop-Process -Force
$mp4 = Get-ChildItem $src -Filter *.mp4 | Where-Object { $_.LastWriteTime -gt $t0 } | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $mp4) { throw "no capture written to $src" }
$commit = (git -C D:\smg-ps2 rev-parse --short HEAD)
$out = "D:\smg-ps2\videos\{0:yyyy-MM-dd_HHmm}_{1}_{2}.mp4" -f $t0, $Name, $commit
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
Move-Item -LiteralPath $mp4.FullName $out
$out
