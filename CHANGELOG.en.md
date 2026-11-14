[한국어](CHANGELOG.md) · **English** · [日本語](CHANGELOG.ja.md)

# Changelog

## Beta 0.1.1 · test version (alpha) 0.2.1 (2026-11-14)

Webhook tab
- **Webhook folder search** — find a series by its Korean reading (e.g. `히비키`, `포제`), kana or kanji. Words separated by spaces must all match (Ctrl+F)
- **Characters: all on · all off · random** — per folder; random flips a coin for each character
- Series icons and character avatars not showing — only on-screen images are fetched, 4 downloads at once, and fetched images are kept on disk for 30 days so they show instantly next time
- The search box covered the clear button

Speed
- Webhook tab stutter — secrets.json was read hundreds of times per paint; now only when it changes, and off-screen folders are not drawn. Paint 40–80 ms → 6–13 ms (122 folders)
- Faster rounded cards and wrapped text (pixel-identical output) — app settings tab paint 16 ms → 8 ms

## C version — beta 0.1.0 · test version (alpha) 0.2.0 (2026-11)

The Electron app was rewritten with **C11 and the Win32 API only**. No external libraries or runtime — a single exe of about 1.1 MB.
The card engine, webhooks, key bindings, background and backfill were ported by feeding the same random inputs to the old Electron code and checking that the results match.

New or changed
- **Settings window** drawn by the app — a dark window with the window buttons merged into the header, a Discord-style activity card, tabs: Activity · Display · App · Keys · Background · Webhooks · Backfill
- **Tray menu** matching the current app — logo and version, extension / Discord response times (ms, median of the last 5), switches, a red Quit item
- **Response time** — messages from the extension and Discord are handled the moment they arrive (about 15.7 ms before because of the Windows timer tick, now under 1 ms)
- **Backfill: enter manually** — log videos that are not on TTFC or IMAGINATION (DVD extras, theater-only clips, etc.) with title · episode · watch time · source · image (link or file)
- **Beta / test version (alpha)** — two modes in one exe; switching in App settings restarts the app
  - Alpha: sending that follows the Discord client's presence limit (5 per 20 seconds) — identical activity is not counted, and play / pause / seek changes are gathered for 0.35 s so only the last state is sent
- **No app IDs in the code** — read from `discordAppIds` in `%APPDATA%\toku-rpc-native\secrets.json` (see the README)
- Chrome extension 0.3.3 — link reading for backfill, video button keys, a guard for the TTFC skip button flipping play/pause

The history of the old Electron version (up to 0.3.1) is in the [CHANGELOG at the `v0.3.1-beta` tag](https://github.com/paeaenteom/tokusatsu-rpc-app/blob/v0.3.1-beta/CHANGELOG.en.md).
