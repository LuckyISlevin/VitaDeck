# VitaDeck – Feature ideas

As of 2026-11-27, after v0.12. Sorted by value relative to effort.
**All items done or dropped.** New ideas go below.

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
- 7. PSP games: built in v0.9 (official PSP titles from ux0:pspemu and Adrenaline bubbles as an own row/tab), removed again in v0.12 at the user's request – PSP is played from Adrenaline. psgm:play only starts PSP titles with a bubble; a PSP-side boot plugin inside Adrenaline (as Pocket Shelf does) worked in principle but was not wanted
- 8. Home screen background: built in v0.9, removed again in v0.12 at the user's request (building it on the UI thread made quick browsing stutter, and it did not look good enough)
- 12. Homebrew browser (VitaDB): dropped. Listing, download and updates worked, but registering *new* apps needs the fake package header (head.bin template from a Sony package + HMAC key) that VitaShell and VitaDB Downloader use under GPL – not something to put into an MIT repo. scePromoterUtilityPromoteImport without it fails with 0x80AF5022. VitaDB Downloader covers this.
- 9. Custom collections: up to 8 named rows (options → "Sammlungen …"), stored as `#coll i name` + a membership column in state.txt; shown on the home screen after the favourites
- 10. Library search with an own on-screen keyboard (QWERTZ, umlauts; the system IME needs GXM): options → "Suchen …", filters while typing, O ends it
- 11. Navigation sounds: generated sine tones (tick / confirm / back) on an own audio thread (SceAudio main port, mono 48 kHz), switch in quick access, `#sound` in state.txt
