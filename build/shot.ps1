Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
$p = Get-Process SoloWorshiper -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$h = $p.MainWindowHandle
[void][W]::ShowWindow($h, 9)
[void][W]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 800
$r = New-Object W+RECT
[void][W]::GetClientRect($h, [ref]$r)
$o = New-Object W+POINT
[void][W]::ClientToScreen($h, [ref]$o)
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap($w, $hh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($o.X, $o.Y, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$out = $args[0]
if (-not $out) { $out = Join-Path (Get-Location) "shot.png" }
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "$out ${w}x${hh}"
