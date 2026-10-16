# Play a .dtm movie (tools/dolphin/dtm.py) in Dolphin from boot of the extracted disc, dumping video + audio, with
# the render window off-screen and a separate User folder (D:\dolphin-script\User: a copy of D:\dolphin\User).
# usage: play.ps1 <movie.dtm> <seconds> [-Out dir] [-Gecko]   -> <Out>\framedump*.avi, dspdump.wav
param([Parameter(Mandatory)][string]$Movie, [Parameter(Mandatory)][int]$Seconds, [string]$Out = "$PSScriptRoot\out")
$user = "D:\dolphin-script\User"
Get-Process | Where-Object { $_.Name -eq 'Dolphin' } | Stop-Process -Force; Start-Sleep 1
Remove-Item -Recurse -Force "$user\Dump\Frames\*", "$user\Dump\Audio\*" -ErrorAction SilentlyContinue
$cfg = @('Dolphin.Movie.DumpFrames=True', 'Dolphin.Movie.DumpFramesSilent=True', 'Dolphin.DSP.DumpAudio=True',
         'Dolphin.DSP.DumpAudioSilent=True', 'Dolphin.Movie.PauseMovie=True', 'Dolphin.Interface.ConfirmStop=False',
         'Dolphin.Interface.UsePanicHandlers=False', 'Dolphin.Interface.PauseOnFocusLost=False',
         'Dolphin.General.ShowFrameCount=True', 'Dolphin.Display.RenderWindowAutoSize=False',
         'Dolphin.Display.RenderWindowXPos=-2400', 'Dolphin.Display.RenderWindowYPos=0',
         'Dolphin.Display.RenderWindowWidth=640', 'Dolphin.Display.RenderWindowHeight=480',
         'Dolphin.Core.EnableCheats=True', 'Graphics.Settings.InternalResolution=1')
$args_ = @('-u', $user, '-b', '-m', $Movie, '-e', 'D:/smg-ps2/data/fs/sys/main.dol') + ($cfg | ForEach-Object { '-C', $_ })
$p = Start-Process -FilePath "D:\dolphin\Dolphin.exe" -ArgumentList $args_ -WindowStyle Minimized -PassThru
Start-Sleep $Seconds
taskkill /PID $p.Id | Out-Null
$p.WaitForExit(40000) | Out-Null
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep 2 }
New-Item -ItemType Directory -Force $Out | Out-Null
Get-ChildItem "$user\Dump\Frames", "$user\Dump\Audio" -File -ErrorAction SilentlyContinue | Move-Item -Destination $Out -Force
Get-ChildItem $Out | Select-Object Name, Length
