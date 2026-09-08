# Generate assets/app.ico for YiPie: dark annulus + purple east sector.
# Uses GDI+ Bitmap -> Icon.Save (writes a proper multi-frame-capable .ico via
# the frame passed; a single 32x32 frame is sufficient and correct for the
# tray, and Windows scales it down to 16x16 when needed).
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

function Draw-Ring([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $pad = [Math]::Max(1, [int]($size * 0.06))
    $rect = New-Object System.Drawing.Rectangle $pad, $pad, ($size - 2 * $pad), ($size - 2 * $pad)
    $outerR = ($size - 2 * $pad) / 2.0
    $innerR = $outerR * 0.55
    $hole = New-Object System.Drawing.RectangleF ($rect.X + ($outerR - $innerR)), ($rect.Y + ($outerR - $innerR)), (2 * $innerR), (2 * $innerR)
    # 1) dark annulus via alternate-fill donut
    $band = New-Object System.Drawing.Drawing2D.GraphicsPath
    $band.AddArc($rect.X, $rect.Y, $rect.Width, $rect.Height, 0, 360)
    $band.AddArc($hole.X, $hole.Y, $hole.Width, $hole.Height, 0, 360)
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 34, 34, 42))
    $g.FillPath($brush, $band)
    # 2) purple east sector (full pie from center; hole re-punched below)
    $hl = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 108, 77, 255))
    $g.FillPie($hl, [float]$rect.X, [float]$rect.Y, [float]$rect.Width, [float]$rect.Height, -22.5, 45.0)
    # 3) punch transparent center: SourceCopy replaces pixels incl. alpha
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $punch = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    $g.FillEllipse($punch, $hole.X, $hole.Y, $hole.Width, $hole.Height)
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
    $brush.Dispose(); $hl.Dispose(); $punch.Dispose(); $band.Dispose()
    $g.Dispose()
    return $bmp
}

$out = Join-Path $PSScriptRoot '..\assets\app.ico'
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null

# Multi-size icon: build 16/32/48 bitmaps, compose a real multi-frame ICO
# (ICONDIR + per-image BITMAPINFOHEADER 32bpp bottom-up + AND mask).
$sizes = 16, 32, 48
$frames = foreach ($s in $sizes) {
    $bmp = Draw-Ring $s
    $rect = New-Object System.Drawing.Rectangle 0, 0, $s, $s
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = [Math]::Abs($data.Stride)
    $buf = New-Object byte[] ($stride * $s)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $bmp.UnlockBits($data)
    $bmp.Dispose()
    # flip to bottom-up (ICO wants bottom-up XOR rows)
    $dib = New-Object byte[] ($stride * $s)
    for ($y = 0; $y -lt $s; $y++) {
        [Array]::Copy($buf, $y * $stride, $dib, ($s - 1 - $y) * $stride, $stride)
    }
    @{ size = $s; dib = $dib; stride = $stride }
}

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$frames.Count)
$imgSize = foreach ($f in $frames) {
    $maskRowBytes = ((($f.size + 31) -shr 5) * 4)
    $sizeBytes = 40 + $f.dib.Length + ($maskRowBytes * $f.size)
    $f | Add-Member -NotePropertyName maskRowBytes -NotePropertyValue $maskRowBytes -Force
    $sizeBytes
}
$pos = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $sizeBytes = 40 + $f.dib.Length + ($f.maskRowBytes * $f.size)
    $bw.Write([byte]$f.size); $bw.Write([byte]$f.size)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$sizeBytes); $bw.Write([uint32]$pos)
    $pos += $sizeBytes
}
foreach ($f in $frames) {
    $bw.Write([int32]40)                       # BITMAPINFOHEADER size
    $bw.Write([int32]$f.size)                  # width
    $bw.Write([int32](2 * $f.size))            # height = XOR + AND
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]0)                       # BI_RGB
    $bw.Write([uint32]$f.dib.Length)
    $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    $bw.Write($f.dib)
    $bw.Write((New-Object byte[] ($f.maskRowBytes * $f.size)))
}
$bw.Flush()
[IO.File]::WriteAllBytes($out, $ms.ToArray())
$bw.Dispose()

# sanity: parse back
$ic = New-Object System.Drawing.Icon $out
Write-Host "reparse: $($ic.Width)x$($ic.Height)"
$ic.Dispose()
# sanity 2: every size renders to a bitmap without throwing
foreach ($s in 16, 32, 48) {
    $one = New-Object System.Drawing.Icon $out, $s, $s
    Write-Host "frame $s OK"
    $one.Dispose()
}
Write-Host "wrote $out $((Get-Item $out).Length) bytes"
