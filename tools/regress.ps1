# Build and run test modes one after another; print each run's last lines matching its pattern.
# usage: regress.ps1 "route:EggStarGalaxy/1:200:stars" "dino:EggStarGalaxy/1:120:star" ...   (mode:scenario:seconds:pattern)
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Runs)
$env:MSYS_NO_PATHCONV = 1
foreach ($r in $Runs) {
  $mode, $scn, $sec, $pat = $r.Split(':')
  Set-Location D:\smg-ps2
  wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh $mode out/slice/hd2.bin $scn | Select-String "error"
  Push-Location D:\smg-ps2\out
  & D:\smg-ps2\tools\run_quiet.ps1 -Elf "D:\smg-ps2\out\slice-packs-$mode.elf" -Wait ([int]$sec) | Out-Null
  Pop-Location
  $l = Get-Content "$env:USERPROFILE\Documents\PCSX2\logs\emulog.txt"
  "== $r"
  $l | Select-String $pat | Select-Object -Last 3 | ForEach-Object { $_.Line }
  $l | Select-String "TLB|exception|panic" | Select-Object -First 2 | ForEach-Object { $_.Line }
}
Get-Process | Where-Object { $_.Name -like 'pcsx2*' } | Stop-Process -Force
