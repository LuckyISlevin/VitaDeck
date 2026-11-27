# VitaDeck – VPK build on Windows with the native VitaSDK toolchain (no cmake/make/WSL).
# Equivalent to CMakeLists.txt: uses VitaSDK's crt0/newlib, src/crt.c and vita.ld are not used.
# Usage:  .\build.ps1          (or .\build.ps1 -Clean)
param([switch]$Clean)
$ErrorActionPreference = 'Stop'

$TitleName = 'VitaDeck'
$TitleId   = 'VLAUNCH01'   # do not change, favourites would be lost
$Version   = '01.60'   # v0.7 (VD_VERSION in src/ui.c)

$Sdk = if ($env:VITASDK) { $env:VITASDK } else { 'C:\vitasdk' }
$Bin = Join-Path $Sdk 'bin'
if (-not (Test-Path "$Bin\arm-vita-eabi-gcc.exe")) { throw "VitaSDK not found in $Sdk (set VITASDK)" }

$Root = $PSScriptRoot
$Out  = Join-Path $Root 'build-win'
if ($Clean -and (Test-Path $Out)) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null

function Invoke-Tool([string]$exe, [string[]]$toolArgs) {
    & (Join-Path $Bin $exe) @toolArgs
    if ($LASTEXITCODE -ne 0) { throw "$exe failed (exit $LASTEXITCODE)" }
}

$Sources = 'gfx', 'img', 'ui', 'platform_vita'
# -ffunction/-fdata-sections + --gc-sections: smaller ELF, and works around a crash of the
# Windows vita-elf-create (0xC0000374, no message) that showed up once ui.c grew (Schritt 4).
$CFlags  = '-O2', '-std=gnu99', '-Wall', '-Wno-unused-function', '-ffunction-sections', '-fdata-sections', "-I$Root\src"
# libcurl + mbedTLS (VitaSDK packages curl-mbedtls, mbedtls, zlib, zstd) for HTTPS cover downloads –
# the Vita's own SSL library cannot talk to GitHub. Static libs first, system stubs after.
$Libs    = 'curl', 'mbedtls', 'mbedx509', 'mbedcrypto', 'everest', 'p256m', 'z', 'zstd', 'z', 'pthread',
           'SceDisplay_stub', 'SceCtrl_stub', 'SceTouch_stub', 'SceRtc_stub', 'ScePower_stub',
           'SceAppMgr_stub', 'SceAVConfig_stub', 'SceSysmem_stub', 'SceIofilemgr_stub',
           'SceLibKernel_stub', 'SceKernelThreadMgr_stub', 'SceKernelModulemgr_stub', 'SceProcessmgr_stub',
           'SceSysmodule_stub', 'SceNet_stub', 'SceNetCtl_stub', 'SceRegistryMgr_stub', 'm'

$objs = @()
foreach ($s in $Sources) {
    $o = "$Out\$s.o"
    Write-Host "CC  src\$s.c"
    Invoke-Tool 'arm-vita-eabi-gcc.exe' ($CFlags + @('-c', "$Root\src\$s.c", '-o', $o))
    $objs += $o
}

Write-Host 'LD  launcher.elf'
Invoke-Tool 'arm-vita-eabi-gcc.exe' (@('-Wl,-q', '-Wl,--gc-sections') + $objs + ($Libs | ForEach-Object { "-l$_" }) + @('-o', "$Out\launcher.elf"))

Write-Host 'ELF launcher.velf'
Invoke-Tool 'vita-elf-create.exe' @("$Out\launcher.elf", "$Out\launcher.velf")

# without -s = unsafe (needs access to ux0:app, ur0:appmeta and ur0:tai)
Write-Host 'SELF eboot.bin (unsafe)'
Invoke-Tool 'vita-make-fself.exe' @('-c', "$Out\launcher.velf", "$Out\eboot.bin")

# ---- return plugin (user plugin for SceShell, no newlib) – packed into the VPK ----
$PluginLibs = 'SceAppMgr_stub', 'SceIofilemgr_stub', 'SceLibKernel_stub', 'SceKernelThreadMgr_stub',
              'SceCtrl_stub', 'SceProcessmgr_stub', 'gcc'
Write-Host 'CC  plugin\vdreturn.c'
Invoke-Tool 'arm-vita-eabi-gcc.exe' @('-O2', '-std=gnu99', '-Wall', '-nostartfiles', '-c', "$Root\plugin\vdreturn.c", '-o', "$Out\vdreturn.o")
Write-Host 'LD  vdreturn.elf'
Invoke-Tool 'arm-vita-eabi-gcc.exe' (@('-Wl,-q', '-nostartfiles', '-nostdlib', "$Out\vdreturn.o") + ($PluginLibs | ForEach-Object { "-l$_" }) + @('-o', "$Out\vdreturn.elf"))
Invoke-Tool 'vita-elf-create.exe' @('-e', "$Root\plugin\exports.yml", "$Out\vdreturn.elf", "$Out\vdreturn.velf")
Invoke-Tool 'vita-make-fself.exe' @('-c', "$Out\vdreturn.velf", "$Out\vdreturn.suprx")
Write-Host ("SUPRX vdreturn.suprx ({0:N0} KB)" -f ((Get-Item "$Out\vdreturn.suprx").Length / 1KB))

Write-Host 'SFO param.sfo'
Invoke-Tool 'vita-mksfoex.exe' @('-s', "TITLE_ID=$TitleId", '-s', "APP_VER=$Version", $TitleName, "$Out\param.sfo")

$files = 'sce_sys/icon0.png',
         'sce_sys/livearea/contents/bg.png',
         'sce_sys/livearea/contents/startup.png',
         'sce_sys/livearea/contents/template.xml',
         'res/Lato-Regular.ttf', 'res/Lato-Bold.ttf',
         'res/Lexend-Regular.ttf', 'res/Lexend-Bold.ttf',
         'res/OFL-Lato.txt', 'res/OFL-Lexend.txt',
         'res/cacert.pem'   # Mozilla CA bundle (curl.se, MPL-2.0) for certificate checks
$packArgs = @('-s', "$Out\param.sfo", '-b', "$Out\eboot.bin")
foreach ($f in $files) { $packArgs += @('-a', "$Root\$($f -replace '/', '\')=$f") }
$packArgs += @('-a', "$Out\vdreturn.suprx=plugin/vdreturn.suprx")   # app0:plugin/ – installed from Settings
$vpk = "$Out\$TitleName.vpk"
Write-Host "VPK $TitleName.vpk"
Invoke-Tool 'vita-pack-vpk.exe' ($packArgs + @($vpk))

$size = (Get-Item $vpk).Length / 1KB
Write-Host ("Done: {0} ({1:N0} KB)" -f $vpk, $size)
