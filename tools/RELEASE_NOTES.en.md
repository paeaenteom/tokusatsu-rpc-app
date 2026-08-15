Addressed the antivirus false positive that was deleting the download, and added a no-install archive.

## Install

Just grab **`TOKU-RPC-Setup-0.2.7-beta.exe`** below and run it — that one file is all you need.
The app and the browser extension are installed in one go. There's nothing else to download.

- In the installer window, just **check off the browsers you want the extension in**
  (Chrome · Edge · NAVER Whale · Brave · Vivaldi)
- Registering the extension brings up the **UAC prompt once** → **[Yes]**
  (That's because Windows only lets admins write to the extension policy key. The app itself installs to your own user folder)
- After installing, **quit your browser completely and start it again**
  (Make sure it's not still running in the system tray, or the extension won't take effect)

## What's new in 0.2.7

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

## What's new in 0.2.6

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

## What's new in 0.2.5

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

## What's new in 0.2.4

### Disney+ title pages look like the page itself
- The title's **logo** is now drawn over the backdrop artwork (same shape as the
  Disney+ page hero)
- On its own the logo is very wide (4.5:1) and became a thin strip on the card.
  With a backdrop behind it the whole square is used and the logo still reads
- Nothing breaks if either piece is missing
  → logo + backdrop composites / logo only / backdrop only / otherwise the site logo

### Also
- Allowed one more Disney+ image CDN (some images come from a different host)

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
| `TOKU-RPC-Setup-0.2.7-beta.exe` | **This is the only one you need** (app bundled inside) |
| `toku-rpc-extension.crx`, `update.xml` | For automatic extension installation and updates — the browser fetches these on its own |
| `toku-rpc-extension.zip` | For installing the extension manually (only if the automatic install is blocked) |
| `TOKU-RPC-<version>-portable.zip` | No-install archive (use if antivirus blocks the exe above) |


## If your antivirus says "virus detected"

**It's a false positive.** This is common for installers from individual developers who
haven't bought a code signing certificate.

Windows Defender calls it `Trojan:Win32/Sabsik.EN.B!ml`, and the **`!ml` suffix means
"machine learning guessed this"** — not that it matched known malware. The combination of
*unsigned + self-extracting + touches the registry + asks for admin* looks statistically
suspicious. Those are all things an installer does by definition.

**If you'd rather not trust it, don't.** Here's how to check or work around it:

- **All the source is public** — the [repository](https://github.com/paeaenteom/tokusatsu-rpc-app)
  includes the installer's own code (`tools/installer/Installer.cs`)
- **Grab the archive instead** — `TOKU-RPC-<version>-portable.zip` isn't an executable, so it
  doesn't get this verdict. Unzip it and run `TOKU RPC.exe`
  (add the extension manually from `toku-rpc-extension.zip`)
- **Report the false positive to Microsoft** — [submit it here](https://www.microsoft.com/en-us/wdsi/filesubmission);
  it usually clears within a few days

A signing certificate would fix this properly. This is a personal project and doesn't have one yet.

### If Smart App Control blocks it

This is a **different problem** from the antivirus false positive above, and there is
currently no way around it.

Smart App Control (Windows 11 only) blocks unsigned executables **unconditionally**.
Reputation doesn't help, and neither does file metadata. It only allows programs signed
with a code signing certificate from a CA in the Microsoft Trusted Root Program.

This program has no such certificate. They aren't cheap for an individual (from roughly
US$200/year), and Microsoft's affordable alternative, Azure Artifact Signing, currently
**restricts individual developer sign-up to residents of the US and Canada**.

So there are only two options:

- **Don't install it** — the safest choice, and a perfectly reasonable one
- **Turn Smart App Control off** — *Windows Security → App & browser control → Smart App Control*
  ⚠ **Once off, it cannot be turned back on without reinstalling Windows.** That means
  permanently lowering a system-wide protection for the sake of one program. Please weigh
  whether that's worth it. [All the source is public](https://github.com/paeaenteom/tokusatsu-rpc-app)
  if you'd rather read it and judge for yourself.

The moment a certificate is in place, signed builds will ship.
## Things to know

- Windows only
- It's an unsigned program, so SmartScreen may warn you
  → *More info → Run anyway*
- The install log is written to `%TEMP%\toku-rpc-install.log` (check this file if something goes wrong)
- This tool only displays watch info — it doesn't download video or circumvent any copy protection
