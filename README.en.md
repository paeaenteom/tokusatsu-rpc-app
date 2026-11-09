[한국어](README.md) · **English** · [日本語](README.ja.md)

# TOKU RPC

A Windows tool that shows what you are watching on tokusatsu streaming sites as a **Discord profile card**, and keeps a **binge log** in a Discord webhook channel.

Supported sites
- 東映特撮ファンクラブ (TTFC) — `pc.tokusatsu-fc.jp`
- TSUBURAYA IMAGINATION — `imagination.m-78.jp`
- YouTube — `www.youtube.com`
- Disney+ — `www.disneyplus.com` *(experimental)*
- CHZZK — `chzzk.naver.com`
- dアニメストア — `animestore.docomo.ne.jp`

> **C version** — rewritten with C11 and the Win32 API only (no external libraries or runtime, one exe of about 1.1 MB).
> Beta 0.1.0 · test version (alpha) 0.2.0. A personal tool, so there may be bugs.
> The old Electron version (up to 0.3.1) is kept at the [`v0.3.1-beta` tag](https://github.com/paeaenteom/tokusatsu-rpc-app/tree/v0.3.1-beta).

---

## Layout

```
toku-native/        C app — Discord RPC, binge log, settings window, tray
  src/base          byte buffer · JSON · SHA-1/Base64 · UTF-8
  src/net           WebSocket server (127.0.0.1:7690) · WinHTTP
  src/discord       Discord IPC (named pipe)
  src/core          card engine (per-site clients · send queue · auto clear)
  src/app           window (GDI drawing) · tray menu · webhooks · background · key bindings · backfill
  src/setup         setup program (carries the app and extension, installs and uninstalls)
  tests/            Node test harnesses (fake Discord · fake extension)
ttfc-chrome-ext/    Chrome extension — reads what you watch, player button keys
tools/              sync-chrome-ext.js — keeps an unpacked extension copy in sync with the source folder
```

The app and the extension talk only over a local WebSocket (`127.0.0.1:7690`).
Viewing data leaves your PC only to **Discord**, to **your own webhook channel** if you set one, and to an image host for thumbnails Discord cannot open (your own Worker, or litterbox · uguu · catbox).

---

## Build

You need: Windows 10/11 x64 and [WinLibs](https://winlibs.com/) GCC (mingw-w64 UCRT, includes `windres`)

```bat
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

Run `toku-native\build-app.bat` to get `toku-native\build\TOKU-RPC-beta.exe` (app only, no tests).

> `build.bat` builds and runs every test. Some tests compare output character by character with the old Electron JS code,
> so they need Node.js 24 and the `ttfc-app` source from `v0.3.1-beta` (see the paths in the test files).
> The UI strings in `src/app/strings.h` are already generated.

---

## Install

1. Download **`TOKU-RPC-Setup.exe`** from [Releases](https://github.com/paeaenteom/tokusatsu-rpc-app/releases/latest) and run it. No administrator rights needed.
   - It puts the app in `%LOCALAPPDATA%\Programs\TOKU RPC Beta`, adds a Start menu shortcut and an uninstall entry in 'Apps', then starts the app.
   - A running copy is closed normally first. Settings and webhook data are kept.
   - The exe is not signed, so Windows SmartScreen may warn → **More info → Run anyway**.
2. Create the `secrets.json` below (the last setup screen tells you if it is missing).
3. Chrome extension: **Open extension folder** on the last setup screen → Chrome `chrome://extensions` → Developer mode → **Load unpacked** → that folder (`chrome-ext`).
   When extension files change, press **Apply new version** in the extension popup.

To uninstall use TOKU RPC in 'Settings → Apps → Installed apps', or `Uninstall TOKU RPC.exe` in the install folder. Settings and webhook data go to the Recycle Bin only if you choose so.
If you built it yourself, `toku-native\build-setup.bat` makes the same setup file (needs Node.js).

### secrets.json (required)

The C version keeps no Discord application IDs in its code. Create your own applications and add their IDs.

```
%APPDATA%\toku-rpc-native\secrets.json
```

```json
{
  "discordAppIds": {
    "ttfc": "APP_ID", "imagination": "APP_ID", "youtube": "APP_ID",
    "disneyplus": "APP_ID", "chzzk": "APP_ID", "danime": "APP_ID"
  },
  "bingeWebhookUrl": "optional — default webhook URL for the binge log"
}
```

In the [Discord Developer Portal](https://discord.com/developers/applications), upload images under **Rich Presence → Art Assets** of each app with these names.

| Site | Assets |
|---|---|
| ttfc | `ttfc_logo`, `play`, `pause` (+ series banners such as `kamen_rider_logo`) |
| imagination | `tsuburaya_imagination_logo`, `play`, `pause` |
| youtube | `logo`, `play`, `pause`, `live` |
| disneyplus | `disney__logo`, `play`, `pause` |
| chzzk | `chzzk_logo`, `play`, `pause`, `live` |
| danime | `danime_logo`, `play`, `pause` |

> Keep this file **on your PC only**. A webhook URL works like a password: anyone who has it can post to that channel.
> Per-title webhooks and characters added in the app's **Webhooks** tab are saved to the same file (`titleWebhooks`) and the settings.
> To upload thumbnails to your own Cloudflare Worker, add `"avatarUpload": { "url": "...", "token": "..." }` (public hosts are used otherwise).

---

## Usage

Settings window tabs
- **Activity** — the card Discord shows right now (drawn like Discord), connection state, binge mode switch
- **Display** — progress time style, show title / episode / thumbnail / buttons, activity type, status display per site
- **App** — language, RPC on/off, start at logon, **Test version (alpha)** switch (the app restarts when switched)
- **Keys** — a key for each video player button (the Chrome extension presses that button)
- **Background** — a background photo for the window (fit · position · zoom · blur · brightness)
- **Webhooks** — per-title binge webhook folders · icons · characters (name and avatar)
- **Backfill** — log episodes you watched without the extension. **From a link** (TTFC · IMAGINATION episode link) or **Enter manually** (DVD extras, theater-only clips, etc.: title · episode · watch time · source · image)

Right-click the tray icon for a menu with extension / Discord response times (ms) and switches.

### Beta / test version (alpha)

One exe has two modes. **Beta** is the default; turning on the switch in App settings restarts the app as the **test version (alpha)**.
The alpha tries a new way of sending that follows the Discord client's presence limit (5 per 20 seconds):
identical activity is not counted, and quick play / pause / seek changes are gathered for 0.35 s so only the last state is sent.

### Data location

`%APPDATA%\toku-rpc-native\` — `config.json` (settings) · `secrets.json` · `logs\main.log` · `backgrounds\` · `hook-avatars\` · `thumb-cache.json`

---

## Changelog

[CHANGELOG.en.md](CHANGELOG.en.md)

## License

MIT
