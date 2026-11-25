# Drives the running app with synthetic clicks (client coordinates) for testing.
#   powershell -File drive.ps1 -Actions "click:1185,154;wait:600;shot:out.png"
param([string]$Actions)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class D {
  [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

$p = Get-Process SoloWorshiper -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$h = $p.MainWindowHandle
[void][D]::ShowWindow($h, 9)
$shell = New-Object -ComObject WScript.Shell
[void]$shell.AppActivate($p.Id)
Start-Sleep -Milliseconds 500
if ([D]::GetForegroundWindow() -ne $h) {
  Write-Output "warn: app did not take the foreground; screenshots may show other windows"
} else {
  Write-Output 'foreground: ok'
}

function Shot($path) {
  $r = New-Object D+RECT
  [void][D]::GetClientRect($h, [ref]$r)
  $o = New-Object D+POINT
  [void][D]::ClientToScreen($h, [ref]$o)
  $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap($w, $hh)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($o.X, $o.Y, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Output "shot: $path ${w}x${hh}"
}

foreach ($a in $Actions.Split(';')) {
  if (-not $a) { continue }
  $k, $v = $a.Split(':', 2)
  switch ($k) {
    'click' {
      $x, $y = $v.Split(',')
      $lp = [IntPtr]([int]$x -bor ([int]$y -shl 16))
      [void][D]::PostMessage($h, 0x0201, [IntPtr]1, $lp)   # WM_LBUTTONDOWN
      Start-Sleep -Milliseconds 40
      [void][D]::PostMessage($h, 0x0202, [IntPtr]0, $lp)   # WM_LBUTTONUP
      Write-Output "click: $x,$y"
    }
    'wait' { Start-Sleep -Milliseconds ([int]$v) }
    'activate' { [void][D]::SetForegroundWindow($h); Write-Output 'activate' }
    'shot' { Shot $v }
  }
}
