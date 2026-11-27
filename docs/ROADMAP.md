# VitaDeck – Feature ideas

As of 2026-11-27, after v0.9. Sorted by value relative to effort.
**Done so far: 1–8 and 14.** Next: 9–12.

## Larger
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
- 2. "Nach Spielende zurück" has three modes: off, after VitaDeck starts, always (also LiveArea starts; plugin v3, `always=` in plugin.txt)
- 3. Options menu (△) shows title ID, version (update version if one is installed) and size incl. update
- 4. Quick access shows free space on ux0:
- 14. Per-game clocks via PSVshell (installed by the user): the options menu writes its profiles ur0:data/PSVshell/profiles/<TITLEID> – Standard (none), Schnell (CPU 444 / GPU 222 / BUS 222 / XBAR 166), Maximal (CPU 500). Profile = 28 bytes (1-byte enums on arm-vita-eabi).
- 7. PSP games: official PSP titles in ux0:pspemu/PSP/GAME (metadata from ur0:appmeta or EBOOT.PBP, icon cached in ux0:data/VitaDeck/pspicons) and Adrenaline bubbles (data/boot.inf) get the category PSP: own home row, library tab (only when present), covers from HexFlow-Covers/PSP
- 8. Home screen background: the hero app's blurred background, darkened and faded towards the rows, built once the focus rests (250 ms)
