# Render glyphs.json codepoints in Segoe MDL2 Assets to a grid PNG for
# visual verification. Usage: powershell -File verify-glyphs.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$json = Get-Content (Join-Path $root 'ui\glyphs.json') -Raw -Encoding UTF8 | ConvertFrom-Json

$cell = 110
$ids = $json.PSObject.Properties.Name
$cols = 6
$rows = [Math]::Ceiling($ids.Count / $cols)
$bmp = New-Object System.Drawing.Bitmap ($cols * $cell), ($rows * $cell + 20)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::White)
$fontGlyph = New-Object System.Drawing.Font 'Segoe MDL2 Assets', 26
$fontLabel = New-Object System.Drawing.Font 'Segoe UI', 9
$black = [System.Drawing.Brushes]::Black
$gray = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(140,0,0,0))

$i = 0
foreach ($id in $ids) {
  $cpHex = $json.$id.cp
  $ch = [char][Convert]::ToInt32($cpHex, 16)
  $col = $i % $cols; $row = [Math]::Floor($i / $cols)
  $x = $col * $cell; $y = $row * $cell + 10
  $g.DrawString([string]$ch, $fontGlyph, $black, $x + 20, $y)
  $g.DrawString(("$id  U+$cpHex"), $fontLabel, $gray, $x + 4, $y + 62)
  $g.DrawRectangle([System.Drawing.Pens]::LightGray, $x, $y - 6, $cell - 2, $cell - 2)
  $i++
}
$g.Dispose()
$out = Join-Path $env:TEMP 'yipie-glyphs-check.png'
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "grid: $out"
