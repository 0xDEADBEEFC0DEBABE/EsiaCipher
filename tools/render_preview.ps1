param([string]$Dir, [float]$Scale = 70, [float]$Width = 12)
# Draws strokes.json (y up, baseline 0) as thick round-capped lines on dark gray: a preview of the intro's cursive.
Add-Type -AssemblyName System.Drawing
$strokes = Get-Content (Join-Path $Dir "strokes.json") -Raw | ConvertFrom-Json
$minX = 1e9; $maxX = -1e9; $minY = 1e9; $maxY = -1e9
foreach ($s in $strokes) { foreach ($p in $s) { $minX = [Math]::Min($minX, $p[0]); $maxX = [Math]::Max($maxX, $p[0]); $minY = [Math]::Min($minY, $p[1]); $maxY = [Math]::Max($maxY, $p[1]) } }
$pad = 40
$w = [int](($maxX - $minX) * $Scale + 2 * $pad); $h = [int](($maxY - $minY) * $Scale + 2 * $pad)
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::FromArgb(255, 34, 36, 44))
# the baseline and the x-height, faint
$guide = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(60, 255, 255, 255)), 1
foreach ($yy in 0, 1, 1.75) { $py = $pad + ($maxY - $yy) * $Scale; $g.DrawLine($guide, 0, $py, $w, $py) }
$stroke = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 200, 225, 255)), $Width
$stroke.StartCap = [System.Drawing.Drawing2D.LineCap]::Round; $stroke.EndCap = [System.Drawing.Drawing2D.LineCap]::Round; $stroke.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
foreach ($s in $strokes) {
  $pts = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
  foreach ($p in $s) { $pts.Add((New-Object System.Drawing.PointF ([float]($pad + ($p[0] - $minX) * $Scale)), ([float]($pad + ($maxY - $p[1]) * $Scale)))) }
  if ($pts.Count -ge 2) { $g.DrawLines($stroke, $pts.ToArray()) } else { $g.FillEllipse([System.Drawing.Brushes]::White, $pts[0].X - $Width / 2, $pts[0].Y - $Width / 2, $Width, $Width) }
}
$bmp.Save((Join-Path $Dir "preview.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose(); "preview $w x $h"

