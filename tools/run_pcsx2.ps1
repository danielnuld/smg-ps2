# Run an ELF in PCSX2, wait, capture the window, print the EE log lines since boot.
# usage: run_pcsx2.ps1 [elf] [seconds] [png]
param([string]$Elf = "D:\smg-ps2\engine\slice.elf", [int]$Wait = 20, [string]$Png = "D:\smg-ps2\engine\snap.png")
Get-Process | Where-Object { $_.Name -like 'pcsx2*' } | Stop-Process -Force; Start-Sleep 1
Start-Process -FilePath "C:\Program Files\PCSX2\pcsx2-qt.exe" -ArgumentList @('-elf', $Elf)
Start-Sleep -Seconds $Wait
& "$PSScriptRoot\snap.ps1" $Png pcsx2-qt | Out-Null
$log = "$env:USERPROFILE\Documents\PCSX2\logs\emulog.txt"
$lines = Get-Content $log
$start = ($lines | Select-String -Pattern 'is executing' | Select-Object -Last 1).LineNumber
$lines[$start..($lines.Count - 1)] | Where-Object { $_ -notmatch 'TLB Miss' -or $_ -match 'TLB' } | Select-Object -First 40
