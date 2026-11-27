# Renders the VitaDeck logo (mark + "VitaDeck" wordmark in Space Grotesk) as PNGs for the README.
# Logo = concept 3E "Start-Kachel" from the logo proposals; the dark variant matches draw_logo() in
# src/ui.c and docs/logo.svg, the light variant docs/logo-light.svg. Run from the repo root: .\docs\make-logo.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot

$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile("$root\res\SpaceGrotesk-Regular.ttf")
$fonts.AddFontFile("$root\res\SpaceGrotesk-Bold.ttf")
$family = $fonts.Families | Where-Object { $_.Name -like 'Space Grotesk*' } | Select-Object -First 1

# 3E colours: tile top-left, focus ring + start tile, arrow, tile bottom-left, tile bottom-right, text
$Dark  = @{ t1 = '#2a3846'; acc = '#3d9bff'; ink = '#06121f'; t3 = '#3a4a5c'; t4 = '#2a3846'; text = '#e6edf3' }
$Light = @{ t1 = '#c3ccd6'; acc = '#1f6fd1'; ink = '#eef1f4'; t3 = '#9eabb9'; t4 = '#c3ccd6'; text = '#0b1016' }

function New-RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure(); return $p
}
function C([string]$hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }

# geometry in the 120x120 box of the 3E SVG
function Draw-Mark($g, [float]$ox, [float]$oy, [float]$size, $pal) {
    $k = $size / 120
    $fill = { param($x, $y, $w, $h, $r, $col) $g.FillPath((New-Object System.Drawing.SolidBrush (C $col)), (New-RoundRect ($ox + $x * $k) ($oy + $y * $k) ($w * $k) ($h * $k) ($r * $k))) }
    & $fill 8 8 46 46 10 $pal.t1
    $pen = New-Object System.Drawing.Pen (C $pal.acc), (3 * $k)
    $g.DrawPath($pen, (New-RoundRect ($ox + 59 * $k) ($oy + 3 * $k) (58 * $k) (58 * $k) (15 * $k)))
    & $fill 65 9 46 46 10 $pal.acc
    $pts = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF ($ox + 83.5 * $k), ($oy + 24.5 * $k)),
        (New-Object System.Drawing.PointF ($ox + 83.5 * $k), ($oy + 39.5 * $k)),
        (New-Object System.Drawing.PointF ($ox + 96.5 * $k), ($oy + 32 * $k)))
    $ink = C $pal.ink
    $g.FillPolygon((New-Object System.Drawing.SolidBrush $ink), $pts)
    $tp = New-Object System.Drawing.Pen $ink, (5 * $k); $tp.LineJoin = 'Round'
    $g.DrawPolygon($tp, $pts)
    & $fill 8 66 46 46 10 $pal.t3
    & $fill 65 66 46 46 10 $pal.t4
}

function Render([string]$out, $pal) {
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
    Draw-Mark $g 24 (($H - $mark) / 2) $mark $pal
    $br = New-Object System.Drawing.SolidBrush (C $pal.text)
    $tx = 24 + $mark + 40
    $ty = ($H - $reg.GetHeight()) / 2
    $g.DrawString('Vita', $reg, $br, $tx, $ty, $sf)
    $g.DrawString('Deck', $bold, $br, $tx + $w1, $ty, $sf)
    $g.Dispose()
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "wrote $out ($W x $H)"
}

Render "$PSScriptRoot\logo-wordmark-dark.png" $Dark     # for dark backgrounds
Render "$PSScriptRoot\logo-wordmark-light.png" $Light   # for light backgrounds
