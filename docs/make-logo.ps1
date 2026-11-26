# Renders the VitaDeck logo (mark + "VitaDeck" wordmark in Space Grotesk) as PNGs for the README.
# Geometry = draw_logo() in src/ui.c (concept 3E). Run from the repo root: .\docs\make-logo.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot

$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile("$root\res\SpaceGrotesk-Regular.ttf")
$fonts.AddFontFile("$root\res\SpaceGrotesk-Bold.ttf")
$family = $fonts.Families | Where-Object { $_.Name -like 'Space Grotesk*' } | Select-Object -First 1

function New-RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure(); return $p
}
function C([string]$hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }

function Draw-Mark($g, [float]$ox, [float]$oy, [float]$size) {
    $k = $size / 120
    $fill = { param($x, $y, $w, $h, $r, $col) $g.FillPath((New-Object System.Drawing.SolidBrush (C $col)), (New-RoundRect ($ox + $x * $k) ($oy + $y * $k) ($w * $k) ($h * $k) ($r * $k))) }
    & $fill 8 8 46 46 10 '#2a3846'
    $pen = New-Object System.Drawing.Pen (C '#3d9bff'), (3 * $k)
    $g.DrawPath($pen, (New-RoundRect ($ox + 60 * $k) ($oy + 4 * $k) (56 * $k) (56 * $k) (14 * $k)))
    & $fill 65 9 46 46 10 '#3d9bff'
    $pts = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF ($ox + 83.5 * $k), ($oy + 24.5 * $k)),
        (New-Object System.Drawing.PointF ($ox + 83.5 * $k), ($oy + 39.5 * $k)),
        (New-Object System.Drawing.PointF ($ox + 96.5 * $k), ($oy + 32 * $k)))
    $ink = C '#06121f'
    $g.FillPolygon((New-Object System.Drawing.SolidBrush $ink), $pts)
    $tp = New-Object System.Drawing.Pen $ink, (5 * $k); $tp.LineJoin = 'Round'
    $g.DrawPolygon($tp, $pts)
    & $fill 8 66 46 46 10 '#3a4a5c'
    & $fill 65 66 46 46 10 '#2a3846'
}

function Render([string]$out, [string]$textColor) {
    $H = 240; $mark = 192; $fs = 112
    $reg = New-Object System.Drawing.Font $family, $fs, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    $bold = New-Object System.Drawing.Font $family, $fs, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $probe = [System.Drawing.Graphics]::FromImage((New-Object System.Drawing.Bitmap 1, 1))
    $sf = [System.Drawing.StringFormat]::GenericTypographic
    $w1 = $probe.MeasureString('Vita', $reg, 2000, $sf).Width
    $w2 = $probe.MeasureString('Deck', $bold, 2000, $sf).Width
    $W = [int](24 + $mark + 40 + $w1 + $w2 + 32)
    $bmp = New-Object System.Drawing.Bitmap $W, $H
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'; $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    Draw-Mark $g 24 (($H - $mark) / 2) $mark
    $br = New-Object System.Drawing.SolidBrush (C $textColor)
    $tx = 24 + $mark + 40
    $ty = ($H - $reg.GetHeight()) / 2
    $g.DrawString('Vita', $reg, $br, $tx, $ty, $sf)
    $g.DrawString('Deck', $bold, $br, $tx + $w1, $ty, $sf)
    $g.Dispose()
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "wrote $out ($W x $H)"
}

Render "$PSScriptRoot\logo-wordmark-dark.png" '#e6edf3'    # light text, for dark backgrounds
Render "$PSScriptRoot\logo-wordmark-light.png" '#0b1016'   # dark text, for light backgrounds
