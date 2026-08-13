[한국어](CHANGELOG.md) · **English** · [日本語](CHANGELOG.ja.md)

# Changelog

What changed in each version. Newest first.

---

## 0.2.2 beta — 2026-08-14

### New (all toggled in the app window under "App settings")
- **Update notifications** — checks GitHub releases every 6 hours and tells you about new versions
  - Turn it off and it doesn't check at all (no network request either)
  - The "Check for updates" button still works while it's off
  - It doesn't download or install. It tells you, and opens the release page when you click
- **Start right at logon** — skips the queue Windows uses to stagger startup apps
  - Uses Task Scheduler's logon trigger (no admin needed)
  - Turning it on removes the old startup entry, so it doesn't launch twice
- **Booster** — checks state more often so reactions are faster. Uses a bit more CPU

### Fixed
- **Wasn't connecting to Discord** — it didn't even try until you opened a supported site,
  so the window showed "Not connected" (a regression introduced in 0.2)
- **Taskbar icon** showed Electron's default icon
  (the AppUserModelId that ties the window to the app was missing)
- Hid the scrollbar in the app window

---

## 0.2 beta — 2026-08-13

### Disney+ support *(experimental)*
- Title · season/episode · episode name · elapsed time · **episode thumbnail**
- Brand pages (Disney · PIXAR · MARVEL · STAR WARS · National Geographic)
- Uses its own Discord application — if it breaks, TTFC and IMAGINATION are unaffected

### Several sites at once
- Before, only one could show, and it flickered while deciding which
- Now every site you have open shows on your profile. Opening the same site in
  multiple tabs still won't flicker

### Memory and responsiveness
- **310 MB → 177 MB** while sitting in the tray (the window isn't created until you open it)
- The once-a-second redraw and log traffic stop while the window is closed
- State changes reach Discord right away instead of waiting for the next tick

### Fixed
- The Korean home page showed nothing on Discord
  (Discord rejects text shorter than two characters)
- The tray icon looked like a smudge (it now uses the icon made for that size)
- The app exe wasn't getting its icon

---

## 0.1 beta — 2026-08-09

First public release.

- Shows the **title · episode · elapsed time · thumbnail** on Discord while you watch
- Supports 東映特撮ファンクラブ and TSUBURAYA IMAGINATION
- **Binge Mode** — plays to the end, moves to the next episode, stays fullscreen
  - Pausing does not end it
- Sends your binge log to a Discord webhook (optional)
- Lives in the tray and starts with your PC
- **한국어 · English · 日本語**
- An installer that sets up the app and the extension in one go
