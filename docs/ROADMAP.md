# VitaDeck – Feature ideas

As of 2026-11-26, after v0.3. Sorted by value relative to effort.
**Planned order for the next round: 1 → 5 → 6.**

## Small but noticeable
1. **Apply the saved brightness on startup** – `load_state()` reads `#bright`, but `plat_set_brightness()` is never called at startup.
2. **"Always return to VitaDeck"** as a third switch in the settings – the plugin sees every game, so VitaDeck could also come back after games started from the LiveArea (replacement home screen). New key in `plugin.txt`.
3. **App details in the options menu (△)** – version (`APP_VER` from `param.sfo`), size on the memory card, title ID.
4. **Free space on ux0:** in quick access, next to the battery.
13. **Airplane mode in the main menu (START)** – toggle entry next to standby/restart. *Research first:* which registry key / system call switches airplane mode (and whether the shell picks up the change without a restart).

## Medium effort, high value
5. **Playtime tracking** – the plugin already sees every game start and end; it writes the times to a file (e.g. `ux0:data/VitaDeck/playtime.txt`), VitaDeck shows "Gespielt: 12 Std." in the hero, plus sorting by playtime.
6. **Swipe scrolling on the touch screen** – scroll rows and the library with a finger; today only taps work (`ui_frame`: `touch_moved` is only used to discard taps).
7. **Recognise PSP/PS1 bubbles** – Adrenaline bubbles currently end up under "Homebrew"; use the category in `param.sfo` to show them as their own "PSP" row (no launching of ISOs by VitaDeck itself).
8. **Home screen background from the hero image** – blurred and darkened over the whole screen, cached like the header/hero.

## Larger
14. **Overclocking per game** – CPU/GPU/bus clock profiles (e.g. 333 → 444 MHz) chosen per app in the options menu (△). Clocks are per running app and reset when a game starts, so this needs a **kernel plugin** that applies the profile at game start (the SceShell user plugin cannot). *Research first:* how existing tools (e.g. PSVshell) do it; keep a safe default and an escape hatch.
9. **Custom collections** – rows such as "Racing" or "With friends", assigned via the options menu.
10. **Search / A–Z jump** in the library (system keyboard `sceImeDialog`); only worth it with many apps.
11. **Navigation sounds** – soft click/confirm sounds; VitaDeck has no audio output yet (SceAudio).
12. **Homebrew browser based on VitaDB** – networking, HTTPS with a bundled TLS library (the firmware certificates are outdated), VPK installation; check the API first. The largest sub-project.

## Done (selection)
- Return after a game and autostart (plugin, steps 1–4), settings with installer
- Hero backgrounds, hiding apps, performance (~12 ms per frame), logo, screenshots
