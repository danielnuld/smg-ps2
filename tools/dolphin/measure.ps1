param([string]$Stage, [int]$Scenario, [string]$Tag)
$s = $PSScriptRoot
Stop-Process -Name Dolphin -Force -ErrorAction SilentlyContinue; Start-Sleep 2
Start-Process -FilePath "D:\dolphin\Dolphin.exe" -ArgumentList @('-b','-e','D:\smg-ps2\data\smg.rvz'); Start-Sleep -Seconds 40
& "$s\keys.ps1" "K+J:600" "wait:7000" | Out-Null
& "$s\keys.ps1" "mouse:315,405" "wait:800" "K:200" "wait:3500" | Out-Null
$w = Start-Process -FilePath python -ArgumentList @("$s\dolmem.py","warp",$Stage,"$Scenario","45") -RedirectStandardOutput "$s\warp_$Tag.log" -NoNewWindow -PassThru
Start-Sleep 3
& "$s\keys.ps1" "mouse:615,507" "wait:600" "K:200" "wait:30000" | Out-Null
& "$s\snap.ps1" "$s\snap_$Tag.png" | Out-Null
python "$s\dolmem.py" dump "$s\dumps\$Tag" | Out-Null
Get-Content "$s\warp_$Tag.log"
