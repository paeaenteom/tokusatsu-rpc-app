Thumbnails aren't cropped any more, responsiveness is way up, and the ghost presence that stuck around after closing a tab is gone.

## Install

Just grab **`TOKU-RPC-Setup-0.2.3-beta.exe`** below and run it — that one file is all you need.
The app and the browser extension are installed in one go. There's nothing else to download.

- In the installer window, just **check off the browsers you want the extension in**
  (Chrome · Edge · NAVER Whale · Brave · Vivaldi)
- Registering the extension brings up the **UAC prompt once** → **[Yes]**
  (That's because Windows only lets admins write to the extension policy key. The app itself installs to your own user folder)
- After installing, **quit your browser completely and start it again**
  (Make sure it's not still running in the system tray, or the extension won't take effect)

## What's new in 0.2.3

### Thumbnails are no longer cropped
- Discord draws the large image **cropped to a square** → a 16:9 thumbnail lost
  43% of its width and only the middle survived
- The whole frame is now fitted inside a square before sending (transparent
  padding above and below) → **TTFC, IMAGINATION and Disney+ alike**

### Responsiveness — effectively instant
- Changes reach Discord **immediately** instead of waiting
  - Every change used to sit through a fixed 0.5s (1s while browsing).
    **That is why the booster never felt different** — only the extension got
    faster while the app added the same delay back every time
  - Discord's limit (5 per 20s) is still respected; spacing kicks in only on rapid changes
- Disney+ play/pause comes from the player's own events (previously up to 0.4s late)
- The booster is a safety net now. Leaving it off costs you almost nothing and saves CPU

### Fixed
- **Presence stayed up after closing the tab** — the tab-close handler read a
  variable that doesn't exist and died, so the notice failed precisely when it
  was needed
  - Closed tabs no longer come back to life on reconnect
  - New app-side safety net: a presence is dropped after 5 minutes of silence
- **Closing one site also turned off the others**
- **A cleared presence wouldn't come back** for the same screen (while paused)

### Disney+
- Title pages show that title's **logo** (falls back to the backdrop when there is none)

## What's new in 0.2.2

### New — all toggled in the app window under "App settings"
- **Update notifications** — checks GitHub releases every 6 hours and tells you about new versions
  - Turn it off and it doesn't check at all (no network request either)
  - It doesn't download or install. It tells you, and opens the release page when you click
- **Start right at logon** — skips the queue Windows uses to stagger startup apps
  - Uses Task Scheduler. No admin needed
- **Booster** — checks state more often so reactions are faster. Uses a bit more CPU

### Fixed
- **Wasn't connecting to Discord** — in 0.2 it didn't even try until you opened a
  supported site, so the window showed "Not connected"
- The taskbar icon showed Electron's default icon
- Hid the scrollbar in the app window

Per-version history is in the [CHANGELOG](https://github.com/paeaenteom/tokusatsu-rpc-app/blob/main/CHANGELOG.en.md).

## What's new in 0.2

### Disney+ support *(experimental)*
- Shows the title · season/episode · episode name · elapsed time · **episode thumbnail**
- Brand pages too (Disney · PIXAR · MARVEL · STAR WARS · National Geographic)
- Still being verified, so it uses its own Discord application — if something goes wrong
  there, TTFC and IMAGINATION are unaffected

### Several sites at once
- Before, only one site could show at a time, and it flickered while deciding which one
- Now every site you have open shows on your profile. Opening the same site in multiple
  tabs still won't flicker

### Memory and responsiveness
- Sitting in the tray now uses **177 MB instead of 310 MB** — the window isn't created
  until you actually open it
- The once-a-second redraw and log traffic stop while the window is closed. Opening it
  brings everything up to date immediately
- State changes reach Discord right away instead of waiting for the next tick

### Also
- Fixed the taskbar icon showing Electron's default icon
- Fixed the tray icon looking like a smudge (it now uses the icon made for that size)
- Fixed the Korean home page showing nothing on Discord
  (Discord rejects text shorter than two characters)

## Main features

- While you're watching, Discord shows the **title · episode · elapsed time · thumbnail**
- Automatically switches which Discord application it uses depending on which site you're on
  (TTFC / IMAGINATION / Disney+)
- **Binge Mode** — when a video plays to the end, it moves to the next episode automatically and stays fullscreen
  - Pausing does not end a binge
- Sends your binge log to a Discord webhook *(optional — see the README)*
- Lives in the tray and starts automatically when you boot your PC
- **한국어 · English · 日本語** — defaults to your system language
  - Change it in the app's window or the extension's popup; the extension follows the app
  - What shows on Discord and what goes into the binge log use the same language

## Files

| File | What it is |
|---|---|
| `TOKU-RPC-Setup-0.2.3-beta.exe` | **This is the only one you need** (app bundled inside) |
| `toku-rpc-extension.crx`, `update.xml` | For automatic extension installation and updates — the browser fetches these on its own |
| `toku-rpc-extension.zip` | For installing the extension manually (only if the automatic install is blocked) |

## Things to know

- Windows only
- It's an unsigned program, so SmartScreen may warn you
  → *More info → Run anyway*
- The install log is written to `%TEMP%\toku-rpc-install.log` (check this file if something goes wrong)
- This tool only displays watch info — it doesn't download video or circumvent any copy protection
