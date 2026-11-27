# VitaDeck – Feature ideas

As of 2026-11-27, after v0.5. Sorted by value relative to effort.
**Round 1 → 5 → 6 is done.** Candidates for the next round: 2, 3, 4, 13 (airplane mode), 14 (overclocking).

## Small but noticeable
2. **"Always return to VitaDeck"** as a third switch in the settings – the plugin sees every game, so VitaDeck could also come back after games started from the LiveArea (replacement home screen). New key in `plugin.txt`.
3. **App details in the options menu (△)** – version (`APP_VER` from `param.sfo`), size on the memory card, title ID.
4. **Free space on ux0:** in quick access, next to the battery.
13. **Airplane mode in the main menu (START)** – toggle entry next to standby/restart. *Research first:* which registry key / system call switches airplane mode (and whether the shell picks up the change without a restart).

## Medium effort, high value
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
- v0.4: box art download, portrait tiles, art cache, logo "3E · Lexend"
- 1. Brightness: the system setting (registry `/CONFIG/DISPLAY/brightness`) is read at startup and written back on every change; VitaDeck's saved value is only applied as a fallback
- 5. Playtime: the plugin (v2) appends "TITLEID seconds" to playtime.txt every minute and at the end of a session; VitaDeck sums and compacts it, shows it in the hero and sorts the library by it
- 6. Swipe scrolling: home rows (sideways), row jumps (up/down), library grid (up/down), tab switch (sideways); snaps to whole tiles with a fling; rows scroll only when the focus reaches an edge
- Extra (not on the list): colour schemes (Nacht, Glut, Aurora, Amethyst, OLED) and a Wi-Fi icon in the header
