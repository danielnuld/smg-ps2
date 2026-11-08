# Capture a window without bringing it to the front (PrintWindow, PW_RENDERFULLCONTENT), so the user's desktop
# is never touched or captured: every visible top-level window of the process (a minimized one put off-screen without
# activation first), the largest taken (MainWindowHandle gave a 199 x 34 title bar). usage: snapbg.ps1 [png] [process]
# [-N shots] [-Ms between]: with N > 1 the files are <png>-0.png, <png>-1.png...
param([string]$Out = "$PSScriptRoot\snap.png", [string]$Proc = "pcsx2-qt", [int]$N = 1, [int]$Ms = 500)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class PW2 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct WP { public int len, flags, show; public POINT min, max; public RECT normal; }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPlacement(IntPtr h, ref WP wp);
  public static List<IntPtr> Of(uint pid) {
    var l = new List<IntPtr>();
    EnumWindows((h, x) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) l.Add(h); return true; }, IntPtr.Zero);
    return l;
  }
}
"@
[PW2]::SetProcessDPIAware() | Out-Null
$p = Get-Process $Proc | Select-Object -First 1
$best = [IntPtr]::Zero; $area = 0
foreach ($h in [PW2]::Of([uint32]$p.Id)) {
  if ([PW2]::IsIconic($h)) {
    $wp = New-Object PW2+WP
    $wp.len = [Runtime.InteropServices.Marshal]::SizeOf($wp); $wp.show = 4
    $wp.normal.L = -2400; $wp.normal.T = 0; $wp.normal.R = -2400 + 1331; $wp.normal.B = 880
    [PW2]::SetWindowPlacement($h, [ref]$wp) | Out-Null
    Start-Sleep -Milliseconds 300
  }
  $r = New-Object PW2+RECT
  [PW2]::GetWindowRect($h, [ref]$r) | Out-Null
  $a = ($r.R - $r.L) * ($r.B - $r.T)
  if ($a -gt $area) { $area = $a; $best = $h }
}
for ($k = 0; $k -lt $N; $k++) {
$r = New-Object PW2+RECT
[PW2]::GetWindowRect($best, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[PW2]::PrintWindow($best, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$bmp.Save($(if ($N -gt 1) { $Out -replace '\.png$', "-$k.png" } else { $Out }), [System.Drawing.Imaging.ImageFormat]::Png)
"-> $Out $($r.R - $r.L)x$($r.B - $r.T)"
Start-Sleep -Milliseconds $Ms
}
