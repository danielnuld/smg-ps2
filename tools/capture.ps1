# Toggle PCSX2's own video capture (hotkey ToggleVideoCapture = F10, PCSX2.ini) without focusing the window:
# the key is posted to the main window only (posting it to every window toggles twice). Output: Documents\PCSX2\videos\*.mp4
Add-Type @"
using System; using System.Runtime.InteropServices;
public class PK {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$h = (Get-Process pcsx2-qt | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
$scan = 0x44   # F10
[PK]::PostMessage($h, 0x0104, [IntPtr]0x79, [IntPtr](1 -bor ($scan -shl 16))) | Out-Null                  # WM_SYSKEYDOWN
[PK]::PostMessage($h, 0x0105, [IntPtr]0x79, [IntPtr](1 -bor ($scan -shl 16) -bor 0xC0000000L)) | Out-Null  # WM_SYSKEYUP
