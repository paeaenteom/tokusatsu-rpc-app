[한국어](README.md) · **English** · [日本語](README.ja.md)

# TOKU RPC

A tool that **puts what you're watching on tokusatsu streaming sites into your Discord profile**, and keeps a **binge log**.

Supported sites
- Toei Tokusatsu Fan Club (TTFC) — `pc.tokusatsu-fc.jp`
- TSUBURAYA IMAGINATION — `imagination.m-78.jp`
- Disney+ — `www.disneyplus.com` *(experimental)*

> **beta 0.2.8** — This is something I made for myself. There may be bugs.

---

## Install

Just grab **`TOKU-RPC-Setup-<version>.exe`** from the [releases page](https://github.com/paeaenteom/tokusatsu-rpc-app/releases/latest)
and run it. The app is bundled inside, so there's nothing else to download.

In the installer window, **check off the browsers you want the extension in**.
It finds your installed browsers automatically and lists them.

| Supported browsers |
|---|
| Google Chrome · Microsoft Edge · NAVER Whale · Brave · Vivaldi |

What the installer does
1. Installs the app (`%LOCALAPPDATA%\Programs\TOKU RPC`) — your own user folder, no elevation
2. Registers the extension in the browsers you picked
3. Creates a desktop shortcut · launches the app *(optional, you can uncheck these)*

Step 2 brings up the **UAC prompt once** → **[Yes]**.
That's because Windows only lets admins write to the extension policy key (`SOFTWARE\Policies`),
and this is the only step that elevates. If you decline, the app still installs fine — just add the extension manually as described below.

After installing, **quit your browser completely and start it again.**
(It has to be gone from the system tray too, or the extension won't take effect)

> For a silent install, use `TOKU-RPC-Setup-<version>.exe /S` — it registers in every browser it detects.
> The install log is written to `%TEMP%\toku-rpc-install.log`.

<details>
<summary>If the extension doesn't get picked up automatically (manual install)</summary>

1. Download `toku-rpc-extension.zip` from the releases page and unzip it
2. Go to `chrome://extensions` → turn on **Developer mode** in the top right
3. **Load unpacked** → pick the unzipped folder

</details>

---

## How to use

Leave the app running and watch a video on a supported site. Everything else is automatic.

- While you're watching, Discord shows the **title · episode · elapsed time · thumbnail**
- While you're just browsing, it shows what kind of page you're on (show page · series list, etc.)
- The app lives in the tray and starts automatically (without a window) when you boot your PC
  - Right-click the tray icon to turn RPC on/off or disable auto-start

### Binge Mode

Click the extension icon to turn on **Binge Mode**. From then on:

- When a video **plays to the end**, it moves to the next episode automatically
- If you're watching fullscreen, the next episode **stays fullscreen**
- Every episode gets a **watch log** entry sent to a Discord webhook (requires the setup below)
- After the last episode, it logs "binge complete" and turns itself off

> **Pausing does not end a binge.** Stop for a bit, switch tabs, come back much later —
> Binge Mode stays on. It only ends when you *finish the last episode* or *toggle it off yourself*.

### Language

Pick **한국어 · English · 日本語**. The default is **your system language**.
Change it in the app's window, or in the extension's popup.

Leave the extension on "System language" and it follows whatever the app is set to, so
changing one changes both. What shows up on Discord and what goes into the binge log
use the same language.

---

## Binge log (webhook) setup — optional

If you want your binge log posted to a Discord channel, add **your own webhook URL**.
(Everything else works fine without it.)

1. In Discord, pick the channel for the log → **Edit Channel → Integrations → Webhooks → New Webhook** → copy the URL
2. Create a `secrets.json` file at the path below and put the URL in it

```
%APPDATA%\toku-rpc\secrets.json
```

```json
{
  "bingeWebhookUrl": "paste_your_webhook_url_here"
}
```

3. Restart the app

> This file is stored **only on your PC** and is never included in the repo or the installer.
> A webhook URL is like a password. Anyone who has it can post to that channel,
> so don't put it anywhere public (screenshots, repos, etc.).

<details>
<summary>If you'd rather use your own Discord application</summary>

By default it uses my Discord applications (display names `TTFC` / `IMAGINATION`).
To switch to your own, add this to the same `secrets.json`.

```json
{
  "discordAppIds": { "ttfc": "app_id", "imagination": "app_id" }
}
```

If you do, you'll need to upload images to **Rich Presence → Art Assets** in the
[Discord Developer Portal](https://discord.com/developers/applications), using the names below.

| App | Assets needed |
|---|---|
| TTFC | `ttfc_logo`, `play`, `pause`, `kamen_rider_logo`, `super_sentai_series_logo`, `metal_hero_series_logo`, `project_r_e_d_logo` |
| IMAGINATION | `tsuburaya_imagination_logo`, `play`, `pause` |

</details>

---

## Project layout

```
ttfc-app/           Electron app — sends the Discord RPC, writes the binge log, sits in the tray
ttfc-chrome-ext/    Chrome extension — pulls watch info off the sites
tools/              install script · release build script
```

The app and the extension talk only over a local WebSocket (`127.0.0.1:7690`).
Your watch info only ever goes to **Discord**, plus **your own webhook channel** if you set one up.

### Building it yourself

```powershell
cd ttfc-app; npm install          # first time only
powershell -File tools\build-release.ps1
```

`dist-release\` will contain `TOKU-RPC-Setup-<version>.exe` · the extension (CRX/ZIP) · `update.xml`.
Just attach those files to a release as-is.

> The extension signing key `ttfc-chrome-ext-key.pem` is not in the repo.
> That key determines the extension ID, so **if you lose it, auto-updates break for existing users.**

---


## If your antivirus says "virus detected"

**It's a false positive**, common for installers from individual developers without a
code signing certificate.

Windows Defender names it `Trojan:Win32/Sabsik.EN.B!ml`. The **`!ml` suffix means
"machine learning guessed this"** — it did not match known malware. The combination of
*unsigned + self-extracting + writes to the registry + requests admin* just looks
statistically suspicious, and those are all things an installer does by definition.

**If you'd rather not trust it, don't.** How to check or work around it:

- **Even the installer's own code is public** — [`tools/installer/Installer.cs`](tools/installer/Installer.cs)
- **Use the archive** — `TOKU-RPC-<version>-portable.zip` on the releases page isn't an
  executable, so it doesn't get this verdict. Unzip and run `TOKU RPC.exe`
  (add the extension manually from `toku-rpc-extension.zip`)
- **[Report the false positive to Microsoft](https://www.microsoft.com/en-us/wdsi/filesubmission)** — usually cleared within days

A signing certificate would fix it properly; this is a personal project and doesn't have one.
## Things to know

- Windows only (Electron can target macOS too, but I've never tested a macOS build)
- IMAGINATION blocks outside access to its thumbnails, so Discord can't load them directly.
  The app uploads them to a temporary host and shows them from there (litterbox first, then uguu)
- This tool only displays watch info — it doesn't download video or circumvent any copy protection


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
## Changelog

What changed in each version is in [CHANGELOG.en.md](CHANGELOG.en.md).

## License

MIT
