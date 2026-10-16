# Mario's place in the Wii at play.json frame G: a movie of the timeline up to G (Dolphin pauses at its end), then
# dolmem.py near (float triples within R of the port's place at G, the -Near argument "x y z"). usage:
# probe.ps1 <play.json> <G> "<x y z>" [seconds]
param([string]$Play, [int]$G, [string]$Near, [int]$Seconds = 150, [int]$R = 2500, [string]$Read = "", [string]$Units = "")
$out = "D:\smg-ps2\tools\dolphin\out\probe_$G"
python -c "import json,sys; p=json.load(open(sys.argv[1])); q=[e for e in p if e['g']<$G]+[{'g':$G}]; json.dump(q,open(sys.argv[2],'w'))" $Play "$out.json"
python D:\smg-ps2\tools\replay.py dtm "$out.json" "$out.dtm" 0
$j = Start-Job { param($m, $s) powershell -ExecutionPolicy Bypass -File D:\smg-ps2\tools\dolphin\play.ps1 $m $s -Out "$m.d" } -ArgumentList "$out.dtm", $Seconds
Start-Sleep ($Seconds - 8)
if ($Units) { python -I D:\smg-ps2\tools\dolphin\dolmem.py units $Units.Split(",") } elseif ($Read) { python -I D:\smg-ps2\tools\dolphin\dolmem.py read3 $Read.Split(",") } else { python -I D:\smg-ps2\tools\dolphin\dolmem.py near "$Near,$R" }
Receive-Job $j -Wait | Out-Null
