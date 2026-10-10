# Task 5.3 coverage run: every scenario of out/packs/scenarios.csv booted in PCSX2 with one ELF
# (engine/build.sh cover out/slice/hd2.bin <any Galaxy/n> -> out/slice-packs-cover.elf), the scenario chosen through
# out/boot.txt (host:boot.txt). Each run ends at its "cover:" line (frame 900), a pack error, or the timeout.
# Writes out/coverage.csv: galaxy, scenario, result (ok / fail / timeout), slow frames, worst frame us, runtime KB,
# last log line. usage: tools/coverage.ps1 [-Only Galaxy] [-Timeout 45] [-Resume] (keep the rows of an earlier run)
param([string]$Only = "", [int]$Timeout = 45, [switch]$Resume)
$out = "D:\smg-ps2\out"
$log = "$env:USERPROFILE\Documents\PCSX2\logs\emulog.txt"
$rows = Import-Csv "$out\packs\scenarios.csv" | Where-Object { $_.fits -eq "1" -and (-not $Only -or $_.galaxy -eq $Only) }
$res = @()
if ($Resume -and (Test-Path "$out\coverage.csv")) { $res = @(Import-Csv "$out\coverage.csv") }
$done = @{}; foreach ($x in $res) { $done["$($x.galaxy)/$($x.scenario)"] = 1 }
foreach ($r in $rows) {
    $scen = "$($r.galaxy)/$($r.scenario)"
    if ($done[$scen]) { continue }
    Set-Content -Path "$out\boot.txt" -Value $scen -NoNewline -Encoding ascii
    & "$PSScriptRoot\run_quiet.ps1" -Elf "$out\slice-packs-cover.elf" -Wait 5 | Out-Null
    $result = "timeout"; $line = ""
    for ($t = 0; $t -lt $Timeout; $t++) {
        Start-Sleep 1
        $txt = Get-Content $log -ErrorAction SilentlyContinue
        $c = $txt | Select-String -Pattern "^\[.*\] cover: " | Select-Object -Last 1
        if ($c) { $result = "ok"; $line = $c.Line; break }
        $e = $txt | Select-String -Pattern "packs: (missing|cannot|read error|zone \d+ failed|no PACKS)|slice: bad blob" | Select-Object -Last 1
        if ($e) { $result = "fail"; $line = $e.Line; break }
    }
    if (-not $line) { $line = ($txt | Select-Object -Last 1) }
    $m = [regex]::Match($line, "slow (\d+), worst (\d+) us, runtime (\d+) KB")
    if ($line -match "misses [1-9]") { $result = "miss" }  # Mario stands still: any miss is a bug (no ground...)
    $res += [pscustomobject]@{ galaxy = $r.galaxy; scenario = $r.scenario; result = $result;
        slow = $m.Groups[1].Value; worst_us = $m.Groups[2].Value; runtime_kb = $m.Groups[3].Value; line = $line.Trim() }
    "$scen $result $($m.Groups[2].Value)"
    $res | Export-Csv -Path "$out\coverage.csv" -NoTypeInformation
}
Get-Process | Where-Object { $_.Name -like 'pcsx2*' } | Stop-Process -Force
Remove-Item "$out\boot.txt" -ErrorAction SilentlyContinue
