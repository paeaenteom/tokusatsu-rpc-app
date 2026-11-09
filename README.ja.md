[한국어](README.md) · [English](README.en.md) · **日本語**

# TOKU RPC

特撮配信サイトの視聴状況を **Discord のプロフィールカード** に表示し、**一気見の記録** を Discord の Webhook チャンネルに残す Windows 用ツール。

対応サイト
- 東映特撮ファンクラブ (TTFC) — `pc.tokusatsu-fc.jp`
- TSUBURAYA IMAGINATION — `imagination.m-78.jp`
- YouTube — `www.youtube.com`
- Disney+ — `www.disneyplus.com` *(実験的)*
- CHZZK (チジック) — `chzzk.naver.com`
- dアニメストア — `animestore.docomo.ne.jp`

> **C 版** — C11 と Win32 API だけで作り直したアプリです（外部ライブラリ・ランタイムなし、exe 1つで約 1.1MB）。
> ベータ 0.1.0 · テスト版（アルファ）0.2.0。個人用に作ったツールなので不具合があるかもしれません。
> 以前の Electron 版（0.3.1 まで）は [`v0.3.1-beta` タグ](https://github.com/paeaenteom/tokusatsu-rpc-app/tree/v0.3.1-beta) に残っています。

---

## 構成

```
toku-native/        C アプリ — Discord RPC 送信、一気見の記録、設定ウィンドウ、トレイ
  src/base          バイトバッファ · JSON · SHA-1/Base64 · UTF-8
  src/net           WebSocket サーバー (127.0.0.1:7690) · WinHTTP
  src/discord       Discord IPC（名前付きパイプ）
  src/core          カードエンジン（サイト別クライアント · 送信キュー · 自動解除）
  src/app           ウィンドウ（GDI 直接描画）· トレイメニュー · Webhook · 背景 · キー設定 · 補充記録
  tests/            Node テストハーネス（偽 Discord · 偽拡張機能）
ttfc-chrome-ext/    Chrome 拡張機能 — サイトから視聴情報を取得、プレーヤーボタンのキー
tools/              sync-chrome-ext.js — 展開して読み込んだ拡張機能のコピーを作業フォルダと揃える
```

アプリと拡張機能はローカル WebSocket（`127.0.0.1:7690`）だけで通信します。
視聴情報が外に出るのは **Discord**、設定した場合は **自分の Webhook チャンネル**、そして Discord が開けないサムネイルを置く画像ホスト（自分の Worker、または litterbox · uguu · catbox）だけです。

---

## ビルド

必要なもの: Windows 10/11 x64、[WinLibs](https://winlibs.com/) GCC（mingw-w64 UCRT、`windres` 付き）

```bat
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

`toku-native\build-app.bat` を実行すると `toku-native\build\TOKU-RPC-beta.exe` ができます（テストなし、アプリのみ）。

> `build.bat` はビルドと全テストです。一部のテストは以前の Electron 版の JS コードと結果を1文字ずつ比べるため、
> Node.js 24 と `v0.3.1-beta` の `ttfc-app` ソースが必要です（テストファイル内のパスを参照）。
> 画面の文言 `src/app/strings.h` は生成済みのファイルが入っています。

---

## インストール

1. exe を `%LOCALAPPDATA%\Programs\TOKU RPC Beta\TOKU-RPC-beta.exe` に置きます。
   （ログオン時の自動起動は、このパスから起動したときだけ登録します。）
2. 下の `secrets.json` を作ってから exe を起動します。トレイにアイコンが出て、設定ウィンドウが開きます。
3. Chrome → `chrome://extensions` → デベロッパーモード → **パッケージ化されていない拡張機能を読み込む** → `ttfc-chrome-ext` フォルダ。
   拡張機能のファイルを変えたあとは、拡張機能ポップアップの **新しいバージョンを適用** を押します。

### secrets.json（必須）

C 版はコードに Discord アプリケーション ID を持ちません。自分のアプリケーションを作って ID を入れてください。

```
%APPDATA%\toku-rpc-native\secrets.json
```

```json
{
  "discordAppIds": {
    "ttfc": "アプリID", "imagination": "アプリID", "youtube": "アプリID",
    "disneyplus": "アプリID", "chzzk": "アプリID", "danime": "アプリID"
  },
  "bingeWebhookUrl": "任意 — 一気見記録の既定 Webhook URL"
}
```

[Discord Developer Portal](https://discord.com/developers/applications) で各アプリの **Rich Presence → Art Assets** に次の名前で画像を登録します。

| サイト | アセット |
|---|---|
| ttfc | `ttfc_logo`, `play`, `pause`（+ `kamen_rider_logo` などシリーズのバナー） |
| imagination | `tsuburaya_imagination_logo`, `play`, `pause` |
| youtube | `logo`, `play`, `pause`, `live` |
| disneyplus | `disney__logo`, `play`, `pause` |
| chzzk | `chzzk_logo`, `play`, `pause`, `live` |
| danime | `danime_logo`, `play`, `pause` |

> このファイルは **自分の PC にだけ** 置きます。Webhook URL はパスワードと同じで、知っている人なら誰でもそのチャンネルに書き込めます。
> 作品別の Webhook とキャラクターは、アプリの **Webhook** タブで追加すると同じファイル（`titleWebhooks`）と設定に保存されます。
> サムネイルを自分の Cloudflare Worker に上げる場合は `"avatarUpload": { "url": "...", "token": "..." }` を加えます（なければ公開ホストを使います）。

---

## 使い方

設定ウィンドウのタブ
- **アクティビティ** — いま Discord に出ているカード（Discord と同じ見た目）、接続状態、一気見モードのスイッチ
- **表示設定** — 進行時間の形、作品・話・サムネイル・ボタンの表示、アクティビティの種類、サイト別のステータス表示
- **アプリ設定** — 言語、RPC のオン/オフ、ログオン時に起動、**テスト版（アルファ）** スイッチ（切り替えるとアプリが再起動）
- **キー設定** — 動画プレーヤーのボタンごとのキー（Chrome 拡張機能がそのボタンを押す）
- **背景** — ウィンドウの背景写真（合わせ方 · 位置 · 拡大 · ぼかし · 明るさ）
- **Webhook** — 作品別の一気見 Webhook フォルダ · アイコン · キャラクター（名前とプロフィール画像）
- **補完** — 拡張機能なしで観た話を後から記録。**リンクから**（TTFC · IMAGINATION のエピソードリンク）または **手動で入力**（DVD 特典・劇場限定映像など: 作品 · 話 · 視聴時刻 · 出典 · 画像）

トレイアイコンを右クリックすると、拡張機能 / Discord の応答時間（ms）とスイッチが並んだメニューが開きます。

### ベータ / テスト版（アルファ）

exe 1つに2つのモードがあります。普段は **ベータ**、アプリ設定のスイッチをオンにするとアプリが再起動して **テスト版（アルファ）** になります。
アルファは Discord クライアントの presence 送信上限（20秒に5回）に合わせた新しい送り方を先に試します
（同じ内容は数えず、再生・一時停止・シークが続いたら 0.35 秒まとめて最後の状態だけ送ります）。

### データの場所

`%APPDATA%\toku-rpc-native\` — `config.json`（設定）· `secrets.json` · `logs\main.log` · `backgrounds\` · `hook-avatars\` · `thumb-cache.json`

---

## 変更履歴

[CHANGELOG.ja.md](CHANGELOG.ja.md)

## ライセンス

MIT
