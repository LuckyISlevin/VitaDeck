# VitaDeck – project context for Claude Code

SteamOS-style homebrew launcher (without third-party branding) for the PS Vita (HENkaku/Ensō, h-encore).
Everything committed to git is in English (docs, comments, script output, commit messages); only the app's UI texts are German.
Personal/local setup goes into `CLAUDE.local.md` (git-ignored).

## Status
- v0.3, title ID `VLAUNCH01` (do not change, favourites would be lost), app version `01.20`. The version lives in `VD_VERSION` (ui.c) and APP_VER in build.ps1 / CMakeLists.txt / Makefile
- Tested on real hardware (FW 3.65, Ensō): ~12 ms per frame, 60 fps
- Performance: cover and overlay caches, header/hero cache, icons pre-composited onto `C_CARD` when loaded (opaque → row memcpy), kerning cache, present thread on core 1 copies into the CDRAM framebuffer, CPU at 444 MHz. Measurements in quick access ("Bildaufbau" + breakdown).
- Font sizes go through `FS(n)` in `ui.c` (currently +2 px; chip labels ≤10 px unchanged)
- Hero is 208 px tall with a background image per app (readable from `ur0:appmeta` on hardware; the copies in `ux0:app`/`ux0:appmeta` are PFS-encrypted). Source order in `plat_bg_paths()`; own images in `ux0:data/VitaDeck/covers/<ID>.png|jpg`. Loading/cropping/darkening runs in the job thread (`plat_job`, core 2); preloading only after the icons and after 0.6 s without input; up to 64 images in RAM, heap 96 MB (`_newlib_heap_size_user`)
- Thread priorities: absolute 64..191 or exactly `0x10000100`; values like `0x10000120` are rejected with ILLEGAL_PRIORITY (quick access shows "Lader: Thread/inline")
- The main thread must not do file IO while a job runs (it would block until the job's read is done)
- Hiding apps: △ → Ausblenden; library tab "Ausgeblendet" (only when not empty); △ opens the options in the library too (sorting is an entry there). `state.txt`: `id fav last hidden`
- `plat_write_file` writes via `.tmp` + rename (Vita3K ignores `SCE_O_TRUNC`), falls back to a direct write if the rename fails
- Logo: `docs/logo.svg`, `docs/logo-wordmark-{dark,light}.png` (from `docs/make-logo.ps1`); README screenshots in `docs/screenshots/`, demo set generator in `docs/demo/`
- PowerShell 5.1 mangles double quotes in `git commit -m` – use `git commit -F <file>` for messages with quotes
- Vita3K reports `sceAppMgrLaunchAppByUri` as success (not implemented), cannot set brightness, does not load SceShell plugins; keys follow US positions (□ = Y on QWERTZ)
- Built as "unsafe" homebrew (needs access to `ux0:app`, `ur0:appmeta`, `ur0:tai`)

## Layout
- `src/ui.c` – the whole UI: home, library, main menu (START), quick access (SELECT), options (△), settings, touch hit testing, logo (concept 3E) and wordmark
- `src/gfx.c/.h` – software renderer (AA primitives, image blits, stb_truetype text with glyph and kerning cache), pixel format 0xAABBGGRR
- `src/platform.h` – interface between UI and platform
- `src/platform_vita.c` – Vita: display (2× CDRAM framebuffer + present thread), input, IO, app scan, launch via `sceAppMgrLaunchAppByUri("psgm:play?titleid=…")`, brightness/volume via SceAVConfig, job thread, plugin install/config
- `plugin/vdreturn.c` – return/autostart user plugin for SceShell (no newlib, Sce* calls only), packed into the VPK as `app0:plugin/vdreturn.suprx`
- `src/crt.c` + `vita.ld` – own startup code and linker script, **only** for the Makefile build without VitaSDK
- `host/platform_host.c` – Linux build that renders screenshots to `/tmp/vlshots/` (demo apps, scripted input)
- `res/` – Lato (UI) and Space Grotesk (wordmark), both OFL, reduced to Latin
- `sce_sys/` – icon0 and LiveArea (8-bit palettised PNG)

## Building
- **Windows:** native VitaSDK (e.g. `C:\vitasdk`). `.\build.ps1 [-Clean]` → `build-win\VitaDeck.vpk` incl. plugin.
- `-ffunction-sections`/`-fdata-sections` + `--gc-sections` are required, otherwise the Windows `vita-elf-create` crashes without a message (0xC0000374) once the ELF grows
- **With VitaSDK (Linux/WSL):** `export VITASDK=…; mkdir build-sdk && cd build-sdk && cmake .. && make` → `VitaDeck.vpk` (without the plugin). Uses VitaSDK's crt0/newlib; `src/crt.c` and `vita.ld` are not used.
- **Without VitaSDK:** `make` with arm-none-eabi-gcc, vita-toolchain tools and self-generated stubs (paths at the top of the `Makefile`).
- **Check the UI:** `make host && ./build/host` (needs `/tmp/vlshots/`, optional test icons in `/tmp/vlicons/`)

## Conventions
- C99, no dependencies except stb (`stb_image`, `stb_truetype`, `stb_image_write` host only)
- UI texts in German, colours as `#define C_*` at the top of `ui.c` (accent `#3d9bff`)
- No Sony/Valve logos or assets; button glyphs are drawn by the app
- Save data: `ux0:data/VitaDeck/state.txt` (fallback: old path `ux0:data/VitaLauncher/`)

## Return/autostart plugin (tested on hardware)
- VitaDeck writes `ux0:data/VitaDeck/return.txt` (title ID) before launching a game and deletes stale markers on start. The plugin polls every ~0.5 s via `sceAppMgrGetIdByName`, waits for the game to start (max 20 s) and end, waits 2.5 s, deletes the marker and starts VitaDeck via `sceAppMgrLaunchAppByUri(0x20000, …)` with verify + up to 3 retries. `sceAppMgrLaunchAppByName2ForShell` only opens the LiveArea page – do not use it as the primary path. Games without a marker (started from the LiveArea) are left alone; system apps (`NPXS…`) never count.
- Autostart once per boot after `NPXS10079` has come and gone (~12 s, at the latest 15 s), only if no other app runs; SELECT at boot skips it.
- `ux0:data/VitaDeck/plugin.txt`: `autostart=0|1`, `return=0|1` (missing = on). `return` is read on every marker (applies immediately), `autostart` only at boot.
- Settings → install: copies the plugin to `ur0:tai/`, rewrites the config.txt taiHEN uses (`ux0:tai` before `ur0:tai`): removes every line loading vdreturn.suprx, inserts `ur0:tai/vdreturn.suprx` below `*main` (creates the section if missing), keeps comments and line endings, backup `config.txt.vitadeck.bak`, verifies the result.
- Escape hatches: hold L at boot (taiHEN loads no plugins), SELECT (autostart only), switches, delete the line in config.txt. Log: `ux0:data/VitaDeck/plugin.log` (previous boot: `plugin.old.log`). Only testable on hardware.

## Open items
Prioritised feature list: [docs/ROADMAP.md](docs/ROADMAP.md).
