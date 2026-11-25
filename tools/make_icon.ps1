# ---------------------------------------------------------------------------
# Builds assets\app.ico from assets\app-source.jpg.
# Writes a multi-size uncompressed 32-bit ICO (16..256 px), which every
# Windows version from XP up accepts in the shell, the taskbar and the
# installer. Run from the repository root:
#   powershell -ExecutionPolicy Bypass -File tools\make_icon.ps1
# ---------------------------------------------------------------------------
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$src = Join-Path $root "assets\app-source.jpg"
$dst = Join-Path $root "assets\app.ico"
if (-not (Test-Path $src)) { Write-Error "Missing $src"; exit 1 }

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$source = [System.Drawing.Image]::FromFile($src)
$images = @()

foreach ($s in $sizes) {
  $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.DrawImage($source, 0, 0, $s, $s)
  $g.Dispose()

  $rect = New-Object System.Drawing.Rectangle(0, 0, $s, $s)
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $data.Stride
  $raw = New-Object byte[] ($stride * $s)
  [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $raw, 0, $raw.Length)
  $bmp.UnlockBits($data)

  # Bottom-up BGRA rows, as the DIB in an ICO expects.
  $pixels = New-Object System.IO.MemoryStream
  for ($y = $s - 1; $y -ge 0; $y--) { $pixels.Write($raw, $y * $stride, $s * 4) }

  # AND mask: 1 bit per pixel, rows padded to 4 bytes, all zero (alpha is used).
  $maskRow = [Math]::Floor(($s + 31) / 32) * 4
  $mask = New-Object byte[] ($maskRow * $s)

  $info = New-Object System.IO.MemoryStream
  $w = New-Object System.IO.BinaryWriter($info)
  $w.Write([int]40)          # biSize
  $w.Write([int]$s)          # biWidth
  $w.Write([int]($s * 2))    # biHeight (XOR + AND)
  $w.Write([int16]1)         # biPlanes
  $w.Write([int16]32)        # biBitCount
  $w.Write([int]0)           # biCompression = BI_RGB
  $w.Write([int]($s * $s * 4))
  $w.Write([int]0); $w.Write([int]0); $w.Write([int]0); $w.Write([int]0)

  $payload = New-Object System.IO.MemoryStream
  $info.Position = 0; $info.CopyTo($payload)
  $pixels.Position = 0; $pixels.CopyTo($payload)
  $payload.Write($mask, 0, $mask.Length)
  $images += ,$payload.ToArray()
  $w.Dispose(); $info.Dispose(); $pixels.Dispose(); $payload.Dispose(); $bmp.Dispose()
}
$source.Dispose()

$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($out)
$bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]
  $dim = if ($s -ge 256) { 0 } else { $s }
  $bw.Write([byte]$dim); $bw.Write([byte]$dim)
  $bw.Write([byte]0); $bw.Write([byte]0)
  $bw.Write([int16]1); $bw.Write([int16]32)
  $bw.Write([int]$images[$i].Length)
  $bw.Write([int]$offset)
  $offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write($img) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($dst, $out.ToArray())
$bw.Dispose(); $out.Dispose()
Write-Output "Wrote $dst ($($sizes -join ', ') px)"
