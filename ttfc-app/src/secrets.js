// ============================================================
//  TOKU RPC — 개인 설정값(비밀) 로더
//
//  Discord 애플리케이션 ID·웹훅 주소처럼 사람마다 다르고 공개하면 안 되는 값을
//  소스 밖에서 읽는다. 저장소·배포 설치본 어디에도 실제 값이 들어가지 않는다.
//
//  읽는 순서
//   1) %APPDATA%\toku-rpc\secrets.json      ← 권장. 앱을 새로 설치해도 유지됨
//   2) 환경변수 (TOKU_APPID_TTFC 등)
//   3) <설치경로>\src\secrets.local.json     ← 개발용. 배포본에는 포함되지 않음
//
//  형식
//   {
//     "discordAppIds": { "ttfc": "숫자ID", "imagination": "숫자ID" },
//     "bingeWebhookUrl": "https://discord.com/api/webhooks/..."
//   }
// ============================================================

const fs = require('fs');
const path = require('path');
const os = require('os');

let cache = null;

function readJson(file) {
    try {
        if (fs.existsSync(file)) return JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch (e) { /* 형식 오류는 무시하고 다음 후보로 */ }
    return null;
}

function userDataFile() {
    // electron app.getPath('userData')와 같은 위치를 electron 의존 없이 계산
    const base = process.env.APPDATA || path.join(os.homedir(), 'AppData', 'Roaming');
    return path.join(base, 'toku-rpc', 'secrets.json');
}

function load() {
    if (cache) return cache;
    const merged = { discordAppIds: {}, bingeWebhookUrl: '' };

    const fromUser = readJson(userDataFile());
    const fromLocal = readJson(path.join(__dirname, 'secrets.local.json'));
    for (const src of [fromLocal, fromUser]) {          // 뒤엣것이 우선
        if (!src) continue;
        if (src.discordAppIds) Object.assign(merged.discordAppIds, src.discordAppIds);
        if (src.bingeWebhookUrl) merged.bingeWebhookUrl = src.bingeWebhookUrl;
    }
    if (process.env.TOKU_APPID_TTFC) merged.discordAppIds.ttfc = process.env.TOKU_APPID_TTFC;
    if (process.env.TOKU_APPID_IMAGINATION) merged.discordAppIds.imagination = process.env.TOKU_APPID_IMAGINATION;
    if (process.env.TOKU_APPID_YOUTUBE) merged.discordAppIds.youtube = process.env.TOKU_APPID_YOUTUBE;
    if (process.env.TOKU_BINGE_WEBHOOK) merged.bingeWebhookUrl = process.env.TOKU_BINGE_WEBHOOK;

    cache = merged;
    return cache;
}

module.exports = {
    /** 사이트별 Discord 애플리케이션 ID (없으면 '') */
    appId(site) { return load().discordAppIds[site] || ''; },
    /** 정주행 기록용 웹훅 주소 (없으면 '') */
    webhookUrl() { return load().bingeWebhookUrl || ''; },
    /** 설정 파일 위치 — 안내 메시지용 */
    configPath() { return userDataFile(); },
};
