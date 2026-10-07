# Start PCSX2 with an ELF without stealing focus: launch minimized, then show it off-screen without activating it,
# so PrintWindow (snapbg.ps1) can capture it while the user keeps working. usage: run_quiet.ps1 [elf] [seconds]
param([string]$Elf = "D:\smg-ps2\engine\slice.elf", [int]$Wait = 16)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class QW {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hgt, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct WP { public int len, flags, show; public POINT min, max; public RECT normal; }
  [DllImport("user32.dll")] public static extern bool SetWindowPlacement(IntPtr h, ref WP wp);
}
"@
Get-Process | Where-Object { $_.Name -like 'pcsx2*' } | Stop-Process -Force; Start-Sleep 1
Start-Process -FilePath "C:\Program Files\PCSX2\pcsx2-qt.exe" -ArgumentList @('-elf', $Elf) -WindowStyle Minimized
Start-Sleep 4
$p = Get-Process pcsx2-qt | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$wp = New-Object QW+WP
$wp.len = [Runtime.InteropServices.Marshal]::SizeOf($wp); $wp.show = 4    # SW_SHOWNOACTIVATE: normal state, not active
$wp.normal.L = -2400; $wp.normal.T = 0; $wp.normal.R = -2400 + 1331; $wp.normal.B = 880   # off-screen
[QW]::SetWindowPlacement($p.MainWindowHandle, [ref]$wp) | Out-Null
Start-Sleep ($Wait - 4)
"running"
