# Renders every logo bitmap from the concept 3E "Start-Kachel" SVG (docs/logo.svg, docs/logo-light.svg):
#   docs/logo-wordmark-{dark,light}.png         README header (mark + "Vita" regular / "Deck" bold, Lexend)
#   sce_sys/icon0.png, sce_sys/livearea/contents/{bg,startup}.png   bubble + LiveArea (8-bit palettised)
# Uses headless Chrome or Edge for exact SVG/font rendering (System.Drawing cannot pick the bold
# face from a private font collection). Run from anywhere: .\docs\make-logo.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot
$browser = @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
             "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $browser) { throw 'Chrome or Edge is required' }
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('vdlogo-' + [guid]::NewGuid())
New-Item -ItemType Directory $tmp | Out-Null
$res = 'file:///' + ("$root\res" -replace '\\', '/')

function Get-Mark([hashtable]$p) {
    "<rect x=`"8`" y=`"8`" width=`"46`" height=`"46`" rx=`"10`" fill=`"$($p.t1)`"/>" +
    "<rect x=`"59`" y=`"3`" width=`"58`" height=`"58`" rx=`"15`" fill=`"none`" stroke=`"$($p.acc)`" stroke-width=`"3`"/>" +
    "<rect x=`"65`" y=`"9`" width=`"46`" height=`"46`" rx=`"10`" fill=`"$($p.acc)`"/>" +
    "<path d=`"M83.5 24.5 L83.5 39.5 L96.5 32 Z`" fill=`"$($p.ink)`" stroke=`"$($p.ink)`" stroke-width=`"5`" stroke-linejoin=`"round`"/>" +
    "<rect x=`"8`" y=`"66`" width=`"46`" height=`"46`" rx=`"10`" fill=`"$($p.t3)`"/>" +
    "<rect x=`"65`" y=`"66`" width=`"46`" height=`"46`" rx=`"10`" fill=`"$($p.t4)`"/>"
}
# 3E colours: tile top-left, focus ring + start tile, arrow, tile bottom-left, tile bottom-right, text
$Dark  = @{ t1 = '#2a3846'; acc = '#3d9bff'; ink = '#06121f'; t3 = '#3a4a5c'; t4 = '#2a3846'; text = '#e6edf3' }
$Light = @{ t1 = '#c3ccd6'; acc = '#1f6fd1'; ink = '#eef1f4'; t3 = '#9eabb9'; t4 = '#c3ccd6'; text = '#0b1016' }
$head = "<!doctype html><html><head><meta charset=`"utf-8`"><style>" +
        "@font-face{font-family:SG;font-weight:400;src:url($res/Lexend-Regular.ttf)}" +
        "@font-face{font-family:SG;font-weight:700;src:url($res/Lexend-Bold.ttf)}" +
        "@font-face{font-family:Lato;src:url($res/Lato-Regular.ttf)}" +
        "html,body{margin:0;overflow:hidden;background:transparent}" +
        ".wm{font-family:SG;line-height:1;letter-spacing:-0.02em}</style></head><body>"
$wm = '<span style="font-weight:400">Vita</span><span style="font-weight:700">Deck</span>'

function Shot([string]$name, [string]$body, [int]$w, [int]$h, [int]$scale = 1) {
    $html = "$tmp\$name.html"; $png = "$tmp\$name.png"
    [IO.File]::WriteAllText($html, "$head$body</body></html>")
    $url = 'file:///' + ($html -replace '\\', '/')
    $p = Start-Process $browser -Wait -PassThru -WindowStyle Hidden -ArgumentList @('--headless=new', '--disable-gpu',
        '--allow-file-access-from-files', '--hide-scrollbars', "--force-device-scale-factor=$scale",
        '--default-background-color=00000000', "--window-size=$w,$h", "--screenshot=$png", $url)
    if (-not (Test-Path $png)) { throw "rendering $name failed" }
    return $png
}

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Collections.Generic; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class VdLogo {
  // opaque 8-bit palettised PNG (the Vita wants these for icon0 and LiveArea):
  // the 256 most frequent 5-bit colour buckets, every pixel mapped to the nearest entry
  public static void Palettise(string src, string dst) {
    var b = new Bitmap(src); int w = b.Width, h = b.Height; var px = new int[w * h];
    var d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    Marshal.Copy(d.Scan0, px, 0, px.Length); b.UnlockBits(d); b.Dispose();
    var cnt = new Dictionary<int, long[]>();
    foreach (int p in px) {
      int k = ((p >> 19) & 31) << 10 | ((p >> 11) & 31) << 5 | ((p >> 3) & 31); long[] a;
      if (!cnt.TryGetValue(k, out a)) { a = new long[4]; cnt[k] = a; }
      a[0]++; a[1] += (p >> 16) & 255; a[2] += (p >> 8) & 255; a[3] += p & 255;
    }
    var keys = new List<int>(cnt.Keys); keys.Sort((x, y) => cnt[y][0].CompareTo(cnt[x][0]));
    int n = Math.Min(256, keys.Count); var pal = new int[n, 3];
    for (int i = 0; i < n; i++) { var a = cnt[keys[i]]; for (int c = 0; c < 3; c++) pal[i, c] = (int)(a[c + 1] / a[0]); }
    var o = new Bitmap(w, h, PixelFormat.Format8bppIndexed); var cp = o.Palette;
    for (int i = 0; i < 256; i++) cp.Entries[i] = i < n ? Color.FromArgb(255, pal[i, 0], pal[i, 1], pal[i, 2]) : Color.Black;
    o.Palette = cp;
    var od = o.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format8bppIndexed);
    var row = new byte[od.Stride]; var memo = new Dictionary<int, byte>();
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int p = px[y * w + x] & 0xffffff; byte bi;
        if (!memo.TryGetValue(p, out bi)) {
          int r = (p >> 16) & 255, g = (p >> 8) & 255, bb = p & 255, best = 0, bd = int.MaxValue;
          for (int i = 0; i < n; i++) { int dr = r - pal[i, 0], dg = g - pal[i, 1], db = bb - pal[i, 2], dd = dr * dr + dg * dg + db * db; if (dd < bd) { bd = dd; best = i; } }
          bi = (byte)best; memo[p] = bi;
        }
        row[x] = bi;
      }
      Marshal.Copy(row, 0, od.Scan0 + y * od.Stride, od.Stride);
    }
    o.UnlockBits(od); o.Save(dst, ImageFormat.Png); o.Dispose();
  }
  // crop a transparent render to its content plus a margin
  public static string Crop(string src, string dst, int m) {
    var b = new Bitmap(src); int w = b.Width, h = b.Height, x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
      if (b.GetPixel(x, y).A > 0) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    var r = new Rectangle(Math.Max(0, x0 - m), Math.Max(0, y0 - m), 0, 0);
    r.Width = Math.Min(w, x1 + m + 1) - r.X; r.Height = Math.Min(h, y1 + m + 1) - r.Y;
    var c = b.Clone(r, PixelFormat.Format32bppArgb); b.Dispose(); c.Save(dst, ImageFormat.Png); c.Dispose();
    return r.Width + "x" + r.Height;
  }
}
'@

try {
    # README wordmarks: same proportions as the 3E board (mark 112, text 52 px, gap 22), rendered at 2x
    foreach ($v in @(@{ n = 'dark'; p = $Dark }, @{ n = 'light'; p = $Light })) {
        $body = "<div style=`"display:inline-flex;align-items:center;gap:22px;padding:8px 14px 8px 4px`">" +
                "<svg width=`"112`" height=`"112`" viewBox=`"0 0 120 120`">$(Get-Mark $v.p)</svg>" +
                "<span class=`"wm`" style=`"font-size:52px;color:$($v.p.text)`">$wm</span></div>"
        $png = Shot "wm-$($v.n)" $body 600 160 2
        $out = "$PSScriptRoot\logo-wordmark-$($v.n).png"
        "wrote $out ($([VdLogo]::Crop($png, $out, 16)))"
    }
    $mark = Get-Mark $Dark
    # icon0: the 3E app icon (rounded tile, mark scaled 0.73)
    $png = Shot 'icon0' ("<svg width=`"128`" height=`"128`" viewBox=`"0 0 120 120`" style=`"display:block`"><rect width=`"120`" height=`"120`" fill=`"#0b1016`"/>" +
        "<rect width=`"120`" height=`"120`" rx=`"26`" fill=`"#0f161e`"/><g transform=`"translate(16 16) scale(0.73)`">$mark</g></svg>") 128 128
    [VdLogo]::Palettise($png, "$root\sce_sys\icon0.png")
    # LiveArea background
    $png = Shot 'bg' ("<div style=`"width:840px;height:500px;background:#0b1016;display:flex;align-items:center;padding-left:80px;box-sizing:border-box;gap:36px`">" +
        "<svg width=`"128`" height=`"128`" viewBox=`"0 0 120 120`">$mark</svg><div><div class=`"wm`" style=`"font-size:52px;color:#e6edf3`">$wm</div>" +
        "<div style=`"font-family:Lato;font-size:21px;color:#8fa3b8;margin-top:18px`">Launcher im SteamOS-Stil</div></div></div>") 840 500
    [VdLogo]::Palettise($png, "$root\sce_sys\livearea\contents\bg.png")
    # LiveArea start button
    $png = Shot 'startup' ("<div style=`"width:280px;height:158px;background:#121b25;display:flex;align-items:center;justify-content:center;gap:18px`">" +
        "<svg width=`"72`" height=`"72`" viewBox=`"0 0 120 120`">$mark</svg><span class=`"wm`" style=`"font-weight:700;font-size:30px;color:#e6edf3`">Starten</span></div>") 280 158
    [VdLogo]::Palettise($png, "$root\sce_sys\livearea\contents\startup.png")
    'wrote sce_sys/icon0.png, sce_sys/livearea/contents/bg.png, startup.png'
} finally {
    Remove-Item -Recurse -Force $tmp
}
