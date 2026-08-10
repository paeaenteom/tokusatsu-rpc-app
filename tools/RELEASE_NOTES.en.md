The first beta of a tool that shows what tokusatsu you're watching on Discord and keeps a binge log.

## Install

Just grab **`TOKU-RPC-Setup-0.1.0-beta.exe`** below and run it — that one file is all you need.
The app and the browser extension are installed in one go. There's nothing else to download.

- In the installer window, just **check off the browsers you want the extension in**
  (Chrome · Edge · NAVER Whale · Brave · Vivaldi)
- Registering the extension brings up the **UAC prompt once** → **[Yes]**
  (That's because Windows only lets admins write to the extension policy key. The app itself installs to your own user folder)
- After installing, **quit your browser completely and start it again**
  (Make sure it's not still running in the system tray, or the extension won't take effect)

## Main features

- While you're watching, Discord shows the **title · episode · elapsed time · thumbnail**
- Automatically switches which Discord application it uses depending on which site you're on (TTFC / IMAGINATION)
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
| `TOKU-RPC-Setup-0.1.0-beta.exe` | **This is the only one you need** (app bundled inside) |
| `toku-rpc-extension.crx`, `update.xml` | For automatic extension installation and updates — the browser fetches these on its own |
| `toku-rpc-extension.zip` | For installing the extension manually (only if the automatic install is blocked) |

## Things to know

- Windows only
- It's an unsigned program, so SmartScreen may warn you
  → *More info → Run anyway*
- The install log is written to `%TEMP%\toku-rpc-install.log` (check this file if something goes wrong)
- This tool only displays watch info — it doesn't download video or circumvent any copy protection
