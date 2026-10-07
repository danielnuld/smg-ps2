# usage: keys.ps1 "K+J:200" "W:1500" "wait:2000" "K:100" ...
#   KEY[+KEY...]:ms  hold those keys together for ms;  wait:ms  pause
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Steps)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class K {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
}
"@
# key -> (vk, scan, extended)
$map = @{
  'W'=@(0x57,0x11,0); 'A'=@(0x41,0x1E,0); 'S'=@(0x53,0x1F,0); 'D'=@(0x44,0x20,0)
  'K'=@(0x4B,0x25,0); 'J'=@(0x4A,0x24,0); 'H'=@(0x48,0x23,0); 'Q'=@(0x51,0x10,0); 'E'=@(0x45,0x12,0)
  '1'=@(0x31,0x02,0); '2'=@(0x32,0x03,0); 'LCTRL'=@(0xA2,0x1D,0); 'LSHIFT'=@(0xA0,0x2A,0); 'ENTER'=@(0x0D,0x1C,0)
  'UP'=@(0x26,0x48,1); 'DOWN'=@(0x28,0x50,1); 'LEFT'=@(0x25,0x4B,1); 'RIGHT'=@(0x27,0x4D,1)
}
$p = Get-Process Dolphin | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
(New-Object -ComObject WScript.Shell).AppActivate($p.Id) | Out-Null
Start-Sleep -Milliseconds 150
[K]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 200
foreach ($s in $Steps) {
  $name, $ms = $s.Split(':'); if ($name -ne 'mouse') { $ms = [int]$ms }
  if ($name -eq 'wait') { Start-Sleep -Milliseconds $ms; continue }
  if ($name -eq 'mouse') {   # mouse:X,Y  window-relative (same coordinates as snap.ps1 images)
    [K]::SetProcessDPIAware() | Out-Null
    $r = New-Object K+RECT; [K]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $x, $y = $s.Split(':')[1].Split(','); [K]::SetCursorPos($r.L + [int]$x, $r.T + [int]$y) | Out-Null
    Start-Sleep -Milliseconds 300; continue
  }
  $keys = $name.Split('+')
  foreach ($k in $keys) { $m = $map[$k]; [K]::keybd_event($m[0], $m[1], [uint32](0x8 -bor ($m[2] * 1)), [UIntPtr]::Zero) }
  Start-Sleep -Milliseconds $ms
  foreach ($k in $keys) { $m = $map[$k]; [K]::keybd_event($m[0], $m[1], [uint32](0x8 -bor 0x2 -bor ($m[2] * 1)), [UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 80
}
"ok"
