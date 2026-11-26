param([string]$Out, [int]$W, [int]$H, [string]$Motif, [string]$C1, [string]$C2, [string]$C3)
# Original abstract artwork for the VitaDeck demo set (no third-party assets).
Add-Type -AssemblyName System.Drawing
function C([string]$hex, [int]$a = 255) { $c = [System.Drawing.ColorTranslator]::FromHtml($hex); [System.Drawing.Color]::FromArgb($a, $c.R, $c.G, $c.B) }
$bmp = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$rect = New-Object System.Drawing.Rectangle 0, 0, $W, $H
$g.FillRectangle((New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, (C $C1), (C $C2), 90.0), $rect)
$rnd = New-Object System.Random ($Motif.GetHashCode() -band 0x7fffffff)
$k = $W / 960.0
function Poly($pts, $brush) { $g.FillPolygon($brush, [System.Drawing.PointF[]]($pts | ForEach-Object { New-Object System.Drawing.PointF $_[0], $_[1] })) }
switch ($Motif) {
  'mountains' {
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (C '#fff4d6' 220)), $W * 0.68, $H * 0.14, $H * 0.22, $H * 0.22)
    $layers = @(@(0.55, '#4b5d7a'), @(0.68, '#2f3e57'), @(0.82, '#1b2536'))
    foreach ($l in $layers) {
      $pts = @(,@(0, ($H))); $x = 0
      while ($x -le $W) { $pts += ,@(($x), ($H * ($l[0] - 0.18 * $rnd.NextDouble()))); $x += $W / (4 + $rnd.Next(4)) }
      $pts += ,@(($W), ($H)); Poly $pts (New-Object System.Drawing.SolidBrush (C $l[1]))
    }
  }
  'stars' {
    for ($i = 0; $i -lt [int](260 * $k * $k + 30); $i++) {
      $r = [float](0.6 + 1.8 * $rnd.NextDouble()) * [Math]::Max($k, 0.35)
      $g.FillEllipse((New-Object System.Drawing.SolidBrush (C '#ffffff' (90 + $rnd.Next(165)))), [float]($rnd.NextDouble() * $W), [float]($rnd.NextDouble() * $H * 0.8), $r * 2, $r * 2)
    }
    for ($i = 0; $i -lt 4; $i++) {
      $x = $rnd.NextDouble() * $W * 0.8; $y = $rnd.NextDouble() * $H * 0.4
      $pen = New-Object System.Drawing.Pen (C $C3 200), ([float](3 * $k + 1))
      $g.DrawLine($pen, [float]$x, [float]$y, [float]($x + 160 * $k), [float]($y + 70 * $k))
    }
    Poly @(@(0, ($H)), @(0, ($H * 0.86)), @(($W * 0.3), ($H * 0.8)), @(($W * 0.6), ($H * 0.88)), @(($W), ($H * 0.82)), @(($W), ($H))) (New-Object System.Drawing.SolidBrush (C '#0b0f1c'))
  }
  'waves' {
    for ($j = 0; $j -lt 6; $j++) {
      $yb = $H * (0.35 + $j * 0.11); $pts = @(,@(0, ($H)))
      for ($x = 0; $x -le $W; $x += [Math]::Max(4, $W / 120)) { $pts += ,@(($x), ($yb + [Math]::Sin($x / ($W / (5 + $j)) + $j) * $H * 0.04)) }
      $pts += ,@(($W), ($H)); Poly $pts (New-Object System.Drawing.SolidBrush (C $C3 (45 + 30 * $j)))
    }
    for ($i = 0; $i -lt 18; $i++) { $r = [float](($rnd.NextDouble() * 10 + 3) * $k + 1); $g.DrawEllipse((New-Object System.Drawing.Pen (C '#bff4ff' 150), 1.5), [float]($rnd.NextDouble() * $W), [float]($rnd.NextDouble() * $H * 0.6), $r, $r) }
  }
  'grid' {
    $pen = New-Object System.Drawing.Pen (C $C3 170), ([float](2 * $k + 0.6))
    $hz = $H * 0.52
    for ($i = -14; $i -le 14; $i++) { $g.DrawLine($pen, [float]($W / 2), [float]$hz, [float]($W / 2 + $i * $W * 0.09), [float]$H) }
    for ($i = 0; $i -lt 9; $i++) { $y = $hz + ($H - $hz) * [Math]::Pow($i / 8.0, 1.8); $g.DrawLine($pen, 0, [float]$y, [float]$W, [float]$y) }
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (C '#ff5ec4' 230)), [float]($W / 2 - $H * 0.2), [float]($hz - $H * 0.32), [float]($H * 0.4), [float]($H * 0.4))
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (C $C2)), 0, [float]($hz - 2), [float]$W, [float]($H * 0.02))
  }
  'forest' {
    for ($row = 0; $row -lt 3; $row++) {
      $base = $H * (0.62 + 0.14 * $row); $col = @('#2e5d45', '#21473a', '#14302a')[$row]
      for ($x = -40 * $k; $x -lt $W + 40; $x += (34 + $rnd.Next(30)) * [Math]::Max($k, 0.4)) {
        $th = $H * (0.25 + 0.15 * $rnd.NextDouble()); $tw = $th * 0.42
        Poly @(@(($x), ($base - $th)), @(($x - $tw / 2), ($base)), @(($x + $tw / 2), ($base))) (New-Object System.Drawing.SolidBrush (C $col))
      }
      $g.FillRectangle((New-Object System.Drawing.SolidBrush (C $col)), 0, [float]$base, [float]$W, [float]($H - $base))
    }
  }
  'ice' {
    for ($i = 0; $i -lt 9; $i++) {
      $r = [float]($H * (0.1 + 0.3 * $rnd.NextDouble()))
      $g.FillEllipse((New-Object System.Drawing.SolidBrush (C '#dff6ff' (25 + $rnd.Next(50)))), [float]($rnd.NextDouble() * $W - $r / 2), [float]($rnd.NextDouble() * $H - $r / 2), $r, $r)
    }
    $cx = $W * 0.62; $cy = $H * 0.5; $s = $H * 0.36
    Poly @(@(($cx), ($cy - $s)), @(($cx + $s * 0.55), ($cy)), @(($cx), ($cy + $s)), @(($cx - $s * 0.55), ($cy))) (New-Object System.Drawing.SolidBrush (C '#e8fbff' 200))
    Poly @(@(($cx), ($cy - $s)), @(($cx + $s * 0.55), ($cy)), @(($cx), ($cy))) (New-Object System.Drawing.SolidBrush (C '#9fdcf5' 220))
  }
  'blocks' {
    $n = 7; $cs = [Math]::Min($W, $H) / ($n + 1)
    for ($i = 0; $i -lt 40; $i++) {
      $x = $rnd.Next([int]($W / $cs)) * $cs; $y = $rnd.Next([int]($H / $cs)) * $cs
      $g.FillRectangle((New-Object System.Drawing.SolidBrush (C @($C3, '#ffd166', '#6ee7a8', '#ff7a6b')[$rnd.Next(4)] (120 + $rnd.Next(120)))), [float]$x, [float]$y, [float]($cs - 3), [float]($cs - 3))
    }
  }
  default {   # rings / generic
    for ($i = 0; $i -lt 6; $i++) {
      $r = [float]($H * (0.15 + 0.12 * $i))
      $g.DrawEllipse((New-Object System.Drawing.Pen (C $C3 (200 - 25 * $i)), ([float](6 * $k + 2))), [float]($W * 0.6 - $r), [float]($H * 0.5 - $r), $r * 2, $r * 2)
    }
  }
}
$g.Dispose()
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
