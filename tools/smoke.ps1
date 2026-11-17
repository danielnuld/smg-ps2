# Smoke test before an ISO: a few test modes that play the game's main line, each built, run in PCSX2 in the background
# and passed when its line shows in the log (or failed at its time limit, or at a crash line). ~15 min; exit status 1
# if any failed. The grand and opening tests write PCSX2's memory card.
# usage: tools/smoke.ps1 [-Only grand,route]
param([string[]]$Only = @())
$tests = @(
  # name, build mode, scenario, XF, the line that passes it, time limit (s)
  @{ n = 'opening'; mode = 'opening'; scn = 'FileSelect/1';        xf = '';                 pass = 'opening test f \d+: nerve 9'; limit = 300 },  # a new file, the prologue up to PrologueDirector's GameStart
  @{ n = 'grand';   mode = 'grand';   scn = 'HeavensDoorGalaxy/1'; xf = '-DASTRO_DOOR=1';   pass = 'astro change stage cube';     limit = 240 },  # the grand star's return, its demos, the save, through the Terrace's door
  @{ n = 'route';   mode = 'route';   scn = 'EggStarGalaxy/1';     xf = '';                 pass = 'route: star got';             limit = 200 }   # Good Egg 1 played to its star
)
$crash = 'TLB miss|exception|panic|bad blob|no memory|cannot read'
$log = "$env:USERPROFILE\Documents\PCSX2\logs\emulog.txt"
$failed = 0
foreach ($t in $tests) {
  if ($Only.Count -and $Only -notcontains $t.n) { continue }
  $b = wsl.exe -e bash -c "XF='$($t.xf)' bash /mnt/d/smg-ps2/engine/build.sh $($t.mode) out/slice/hd2.bin $($t.scn) 2>&1" | Select-String '\berror\b|undefined reference'
  if ($b) { "FAIL $($t.n): build"; $b | Select-Object -First 3 | ForEach-Object { "  $($_.Line)" }; $failed++; continue }
  Push-Location D:\smg-ps2\out
  & $PSScriptRoot\run_quiet.ps1 -Elf "D:\smg-ps2\out\slice-packs-$($t.mode).elf" -Wait 6 | Out-Null
  Pop-Location
  $sw = [Diagnostics.Stopwatch]::StartNew(); $hit = $null; $bad = $null
  while ($sw.Elapsed.TotalSeconds -lt $t.limit -and -not $hit -and -not $bad) {
    Start-Sleep 3
    $l = Get-Content $log -ErrorAction SilentlyContinue
    $hit = $l | Select-String $t.pass | Select-Object -First 1
    $bad = $l | Select-String $crash | Select-Object -First 1
  }
  if ($hit -and -not $bad) { "PASS $($t.n) ($([int]$sw.Elapsed.TotalSeconds) s): $($hit.Line)" }
  else { "FAIL $($t.n): $(if ($bad) { $bad.Line } else { "no '$($t.pass)' in $($t.limit) s; last: $($l | Select-Object -Last 1)" })"; $failed++ }
}
Get-Process | Where-Object { $_.Name -like 'pcsx2*' } | Stop-Process -Force
if ($failed) { "smoke: $failed failed"; exit 1 }
"smoke: all passed"
