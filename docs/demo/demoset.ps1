# Creates the VitaDeck demo library in Vita3K (invented titles, generated art) for the README screenshots.
# Run from the repo root after .\build.ps1: .\docs\demo\demoset.ps1 – then install build-win\VitaDeck.vpk in Vita3K.
# Apps with a 1 in the last column get generated portrait box art in ux0:data/VitaDeck/covers/box (like the
# covers VitaDeck downloads), the others show the icon fallback. No real covers are used.
# Remove afterwards: the VDDEMO*/PCS*901* folders in ux0\app, ux0\data\VitaDeck and ur0\tai in Vita3K's folder.
$ErrorActionPreference = 'Stop'
$s = $PSScriptRoot
$v = "$env:APPDATA\Vita3K\Vita3K"
$mk = 'C:\vitasdk\bin\vita-mksfoex.exe'
$apps = @(
  @('PCSB90101', 'Neon Drift',            'grid',      '#1a0b3d', '#3b0f5c', '#29e6ff', 1),
  @('PCSE90102', 'Kryo Station',          'ice',       '#0e2a47', '#3f86b8', '#dff6ff', 1),
  @('PCSB90103', 'Waldläufer',            'forest',    '#f2b880', '#9cc08f', '#2e5d45', 1),
  @('PCSF90104', 'Sternenfall',           'stars',     '#050816', '#2b2d6e', '#ffd166', 1),
  @('PCSE90105', 'Tiefsee Echo',          'waves',     '#062a45', '#0b7a99', '#7ff0ff', 1),
  @('PCSG90106', 'Gipfelsturm',           'mountains', '#f4a259', '#6d8fbf', '#ffffff', 1),
  @('VDDEMO101', 'Shellwerk',             'blocks',    '#1f2b38', '#2a3846', '#3d9bff', 1),
  @('VDDEMO102', 'Paketbote',             'rings',     '#3b1f4f', '#6e3f8c', '#ffd166'),
  @('VDDEMO103', 'Netzradio',             'rings',     '#0f3d2e', '#1f6e4f', '#6ee7a8'),
  @('VDDEMO201', 'Pixel16 Emulator',      'blocks',    '#2b1a10', '#5c3a22', '#ff7a6b'),
  @('VDDEMO202', 'RetroBox Emulator',     'grid',      '#10202b', '#1d4f66', '#ffd166'),
  @('VDDEMO203', 'Taschenkonsole Emulator','stars',    '#1b1030', '#4a2a6e', '#ff9ad5')
)
$covers = "$v\ux0\data\VitaDeck\covers\box"
New-Item -ItemType Directory -Force $covers | Out-Null
foreach ($a in $apps) {
  $dir = "$v\ux0\app\$($a[0])\sce_sys"
  New-Item -ItemType Directory -Force $dir | Out-Null
  # the C runtime of vita-mksfoex converts its arguments to the ANSI code page; pre-encode the
  # title so that conversion yields UTF-8 bytes (otherwise "Waldläufer" ends up broken in param.sfo)
  $t8 = [Text.Encoding]::Default.GetString([Text.Encoding]::UTF8.GetBytes($a[1]))
  & $mk -s "TITLE_ID=$($a[0])" -s 'APP_VER=01.00' -s "STITLE=$t8" $t8 "$dir\param.sfo"
  & "$s\mkart.ps1" -Out "$dir\icon0.png" -W 128 -H 128 -Motif $a[2] -C1 $a[3] -C2 $a[4] -C3 $a[5]
  if ($a.Count -gt 6 -and $a[6]) {
    & "$s\mkart.ps1" -Out "$covers\$($a[0]).png" -W 250 -H 320 -Motif $a[2] -C1 $a[3] -C2 $a[4] -C3 $a[5] -Title $a[1]
  }
}
# history + favourites: minutes since 1970-01-01 (+1), like now_minutes() in ui.c
function M([datetime]$t) { [int](($t.Date - [datetime]'1970-01-01').Days * 1440 + $t.Hour * 60 + $t.Minute + 1) }
$now = Get-Date
$st = @(
  "#bright 70",
  "PCSG90106 0 $(M $now.AddMinutes(-35)) 0",
  "PCSB90101 1 $(M $now.AddDays(-1).Date.AddHours(21).AddMinutes(40)) 0",
  "VDDEMO101 0 $(M $now.AddHours(-3)) 0",
  "PCSE90105 0 $(M $now.AddDays(-3)) 0",
  "PCSF90104 1 0 0"
) -join "`n"
New-Item -ItemType Directory -Force "$v\ux0\data\VitaDeck" | Out-Null
[IO.File]::WriteAllText("$v\ux0\data\VitaDeck\state.txt", $st + "`n", (New-Object Text.UTF8Encoding($false)))
# playtime as the plugin would have logged it ("TITLEID seconds", several lines per title are summed)
$pt = @("PCSG90106 30000", "PCSB90101 4800", "PCSE90102 9000", "PCSF90104 140000", "PCSE90105 2700", "PCSG90106 15000") -join "`n"
[IO.File]::WriteAllText("$v\ux0\data\VitaDeck\playtime.txt", $pt + "`n", (New-Object Text.UTF8Encoding($false)))
# the running plugin's version (plugin/version.h), so the settings do not ask for a reboot
$ver = [regex]::Match((Get-Content -Raw "$(Split-Path (Split-Path $PSScriptRoot))\plugin\version.h"), 'PLUGIN_VERSION\s+(\d+)').Groups[1].Value
[IO.File]::WriteAllText("$v\ux0\data\VitaDeck\plugin.ver", "$ver`n", (New-Object Text.UTF8Encoding($false)))
# settings screen: plugin "installed" (Vita3K only)
New-Item -ItemType Directory -Force "$v\ur0\tai" | Out-Null
[IO.File]::WriteAllText("$v\ur0\tai\config.txt", "*KERNEL`n*main`nur0:tai/vdreturn.suprx`n", (New-Object Text.UTF8Encoding($false)))
Copy-Item "$(Split-Path (Split-Path $PSScriptRoot))\build-win\vdreturn.suprx" "$v\ur0\tai\vdreturn.suprx"
"Demo-Set angelegt: $($apps.Count) Apps"
