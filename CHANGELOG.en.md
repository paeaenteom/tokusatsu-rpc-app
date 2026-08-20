[한국어](CHANGELOG.md) · **English** · [日本語](CHANGELOG.ja.md)

# Changelog

What changed in each version. Newest first.

---

## 0.3.0 beta — 2026-08-21

### Speed
- **Title and channel name appear much sooner** (YouTube)
  - Asks the player directly instead of waiting for the page to update.
    Measured: **340ms** after navigating (the page itself takes 1.1–2.5s)
  - The previous video’s title and channel no longer flash for a moment.
    Nothing is sent until the current video is known for certain
- **Play and pause register instantly** — measured **32ms** on average (with speed priority on)

### Timeline accuracy
- The progress bar’s anchor is now computed **at the moment the page is read**
  - The app used to build it from "now − seconds received", losing the fraction and
    adding transfer delay, so the position shifted on every refresh.
    Applies to TTFC, IMAGINATION and YouTube
- Total length was one second longer than the real one; fixed
- No anchor is set while the video cannot play yet (buffering). Setting one there made
  the card run ahead by however long playback took to actually start

> The remaining **±1 second** cannot be removed. Discord only accepts whole-second
> timestamps, so the card ticks on clock seconds while the player ticks on video
> seconds. They disagree for part of every second (the current setting matches best).

### Fixed
- **Elapsed time froze at its first value**, so the progress bar kept resetting
- **Unhandled errors were logged** when the connection closed (two places)
- **Channel name appeared twice** on channel pages
- The title briefly read **"YouTube"** right after navigating

### Also
- Added a diagnostic line: logged when the progress bar differs from the real position by over 2s

### Good to know
- One script now runs **in the same space as the page** to read YouTube playback
  (`extractors/youtube-main.js`). It only asks the player for position and title;
  it does not modify the page. Disney+ already works the same way
- **0.2.10 was never published.** Coming from 0.2.9 you also receive its changes
  (YouTube support, Discord connection stability) listed below

---

## 0.2.10 beta — 2026-08-19

### New
- **YouTube support**
  - Video title, channel name, elapsed time, thumbnail
  - **Shorts**
  - **Live streams** — the timer counts up from when the broadcast started,
    so joining midway still shows the full elapsed time
  - **Channel pages** show that channel’s profile picture
  - Browsing: Home, Search, Subscriptions, History, Playlist, Channel

### Fixed (affects every site)
- **Discord dropped its own connection right after startup**
  - The first client was logged in twice; the abandoned one failed 10 seconds
    later and tore down the healthy connection with it
  - Measured: dropped at 10s, retried at 20s, reconnected at 30s. Now zero
- **The card took a long time to appear when the app had just started**
  - A state that arrived before the connection was ready was discarded, so the
    profile stayed empty until the 30-second refresh came around
- **Videos without a thumbnail were re-fetched every 5 seconds forever**
  - A give-up counter existed, but the app’s recovery request reset it to zero
    every time, so it never fired

---

## 0.2.9 beta — 2026-08-18

### Fixed
- **Errors logged** by the Discord reconnect fix in 0.2.8
  - The cleanup call is async but was wrapped synchronously, so writing to an
    already-closed socket escaped as an unhandled rejection. Harmless, but it
    cluttered the log

---

## 0.2.8 beta — 2026-08-18

### Fixed
- **Discord connection died after a while and never came back**
  - Each reconnect created a new connection without disposing the old one, so handles
    piled up until Discord refused them all — **retrying made it worse**
  - Retries now back off (10s up to 2 min), resetting once connected
- **Your presence vanished when another window came to the front**
  - Chrome treats a tab as hidden when the browser is merely covered, so opening this
    app's window or Discord wiped the presence
  - While watching, being covered no longer clears it. While browsing, it clears only
    after 15 seconds of staying covered. Closing the tab still clears immediately

### New
- **Pick how long before the presence auto-clears** — in the app window under "Time display"
  - Never / 5 min / 30 min / 1 h / 3 h / 6 h / 12 h / 24 h (default 5 min)

### Images appear faster
- Fixed images taking **over a minute** after a transient failure
  - Failures were counted, not spaced — with the booster on, three strikes burned in
    **0.9 seconds** and the image was abandoned for the rest of the page
  - Recovery requests now every 5 seconds instead of 20
  - TTFC thumbnails had no working recovery path at all
- Uploaded images are smaller (**about 44% less**), so they show up sooner

### Binge notifications
- The attached image is back to its **original aspect ratio** (the profile card stays square)

---

## 0.2.7 beta — 2026-08-15

### Antivirus false positive
- Windows Defender flagged the installer as `Trojan:Win32/Sabsik.EN.B!ml` and
  **deleted the download automatically**. It's a false positive — the `!ml` suffix
  means a machine-learning guess, not a match against known malware
- **The executables now say who they are.** The installer had completely blank
  metadata (version 0.0.0.0, no product or company), and the app itself still
  claimed to be "GitHub, Inc." from Electron's defaults. Unsigned *and* anonymous
  is a bad combination for these classifiers
- **A no-install archive ships alongside it** — `TOKU-RPC-<version>-portable.zip`.
  It isn't an executable, so it doesn't get this verdict. Unzip and run `TOKU RPC.exe`
- The README and release notes now explain why this happens and how to report it

> The real fix is a code signing certificate. This is a personal project and doesn't have one.

---

## 0.2.6 beta — 2026-08-15

### Fixed
- **Going back from a video left that video on your profile**
  - If the page you returned to was the one you came from, it decided "nothing
    changed" and skipped the update — even though a video card was on screen
  - Measured: it stayed for **44 seconds** after going back, and only cleared
    when the tab was switched
- **The app had no icon** — depending on how it was built, the step that embeds
  the icon was skipped entirely. It is now always applied
- Closing a tab sent the same signal twice; now once

### Fewer uploads
- **Restarting the app no longer re-uploads images it already uploaded**
  - The cache used to start empty on every launch
- Retention now matches the host (images kept for 72 hours were being discarded
  after 2 and uploaded again)

---

## 0.2.5 beta — 2026-08-15

### Memory
- **315MB → 171MB while sitting in the tray**
  - Closing the window now actually destroys the process that draws it. It used to
    only hide, so it kept holding memory the whole time (95MB measured)
  - Reopening builds it fresh. Speed and behaviour are unchanged
- Original image bytes were kept once per site; now they're kept once

### Images appear faster
- **The same image was uploaded three times — now once** (one upload per site connection)
- Composited images are sent as JPEG instead of PNG (there is no transparency in them,
  so PNG bought nothing). About a quarter of the size
- Fixed the order images are fetched in. The path that always fails was tried first,
  and that wasted round trip was pure waiting time

### Disney+
- **List and collection pages show the title's logo too** (previously title pages only)
- The backdrop is fetched at full resolution (400x225 → 1920x1080). On some pages it
  wasn't being found at all

### Cleanup
- Removed the upload path that no longer runs (no behavioural change)

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
