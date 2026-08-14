[한국어](CHANGELOG.md) · **English** · [日本語](CHANGELOG.ja.md)

# Changelog

What changed in each version. Newest first.

---

## 0.2.4 beta — 2026-08-14

### Disney+ title pages look like the page itself
- The title's **logo** is now drawn over the backdrop artwork (same shape as the
  Disney+ page hero)
- On its own the logo is very wide (4.5:1) and became a thin strip on the card.
  With a backdrop behind it the whole square is used and the logo still reads
- Nothing breaks if either piece is missing
  → logo + backdrop composites / logo only / backdrop only / otherwise the site logo

### Also
- Allowed one more Disney+ image CDN (some images come from a different host)

---

## 0.2.3 beta — 2026-08-14

### Thumbnails are no longer cropped
- Discord draws the large image **cropped to a square**, so a 16:9 thumbnail
  lost 43% of its width and only the middle survived
- The whole frame is now fitted inside a square before sending (transparent
  padding above and below) → **TTFC, IMAGINATION and Disney+ alike**

### Responsiveness
- Changes now reach Discord **immediately** instead of waiting
  - Every change used to sit through a fixed 0.5s (1s while browsing).
    **That is why the booster never felt different** — only the extension got
    faster while the app added the same delay back every time
  - Discord's limit (5 per 20s) is still respected; spacing kicks in only on rapid changes
- Disney+ play/pause is picked up from the player's own events (previously up to 0.4s late)

### Fixed
- **Presence stayed up after closing the tab** — the tab-close handler read a
  variable that doesn't exist and died, so the notice failed precisely when it
  was needed
  - Closed tabs no longer come back to life on reconnect
  - The app has its own safety net now: it drops a presence after 5 minutes of
    silence from the extension (the 30s refresh kept resetting the old one, so
    it never fired)
- **Closing one site also turned off the others**
- **A cleared presence wouldn't come back** for the same screen (while paused)

### Disney+
- Title pages show that title's **logo** (falls back to the backdrop when there is none)

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
