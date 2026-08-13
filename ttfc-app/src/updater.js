// ============================================================
//  업데이트 확인
//
//  GitHub 릴리스를 보고 새 버전이 있으면 알린다.
//  ⚠ 내려받거나 설치하지 않는다. 알리고, 눌렀을 때 릴리스 페이지를 열 뿐이다.
//    서명 없는 설치본을 사용자 모르게 갈아치우는 건 하지 않는다.
//
//  알림은 끌 수 있다 (설정 updateNotify). 꺼두면 확인 자체를 하지 않는다 —
//  네트워크 요청도 안 나간다.
// ============================================================

const https = require('https');
const log = require('electron-log');

const REPO = 'paeaenteom/tokusatsu-rpc-app';
const CHECK_URL = `https://api.github.com/repos/${REPO}/releases/latest`;
const RELEASES_PAGE = `https://github.com/${REPO}/releases/latest`;

const FIRST_DELAY = 20 * 1000;         // 켜자마자 말고 조금 뒤에 (시작을 방해하지 않게)
const INTERVAL = 6 * 60 * 60 * 1000;   // 6시간마다

// "v0.2.0-beta" / "0.2.0-beta" → [0,2,0] + 프리릴리스 여부
function parseVersion(s) {
    const m = String(s || '').match(/(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?/);
    if (!m) return null;
    return { nums: [+m[1], +m[2], +m[3]], pre: m[4] || '' };
}

// a 가 b 보다 새 버전이면 true.
//  프리릴리스 규칙(semver): 숫자가 같으면 프리릴리스가 정식보다 낮다.
//  0.2.0-beta < 0.2.0 < 0.2.1-beta
function isNewer(a, b) {
    const x = parseVersion(a), y = parseVersion(b);
    if (!x || !y) return false;
    for (let i = 0; i < 3; i++) {
        if (x.nums[i] !== y.nums[i]) return x.nums[i] > y.nums[i];
    }
    if (x.pre === y.pre) return false;
    if (!x.pre) return true;    // 정식 > 프리릴리스
    if (!y.pre) return false;
    return x.pre > y.pre;       // beta.2 > beta.1 같은 경우
}

function fetchLatest() {
    return new Promise((resolve, reject) => {
        const req = https.get(CHECK_URL, {
            headers: { 'User-Agent': 'toku-rpc', Accept: 'application/vnd.github+json' },
            timeout: 10000,
        }, (res) => {
            if (res.statusCode !== 200) {
                res.resume();
                return reject(new Error('HTTP ' + res.statusCode));
            }
            let body = '';
            res.setEncoding('utf8');
            res.on('data', (c) => { body += c; });
            res.on('end', () => {
                try {
                    const j = JSON.parse(body);
                    resolve({ tag: j.tag_name || '', name: j.name || '', url: j.html_url || RELEASES_PAGE });
                } catch (e) { reject(e); }
            });
        });
        req.on('timeout', () => { req.destroy(new Error('시간 초과')); });
        req.on('error', reject);
    });
}

class Updater {
    // isEnabled() → 알림을 켰는지, onFound(info) → 새 버전을 찾았을 때
    constructor({ currentVersion, isEnabled, onFound }) {
        this.current = currentVersion;
        this.isEnabled = isEnabled;
        this.onFound = onFound;
        this._timer = null;
        this._firstTimer = null;
        this.latest = null;       // { tag, name, url } — 새 버전을 찾았을 때만
        this.lastCheckedAt = 0;
        this.lastError = '';
    }

    start() {
        this.stop();
        this._firstTimer = setTimeout(() => {
            this._firstTimer = null;
            this.check();
            this._timer = setInterval(() => this.check(), INTERVAL);
        }, FIRST_DELAY);
    }

    stop() {
        if (this._timer) { clearInterval(this._timer); this._timer = null; }
        if (this._firstTimer) { clearTimeout(this._firstTimer); this._firstTimer = null; }
    }

    // manual=true 면 알림을 꺼놨어도 확인한다 (사용자가 직접 눌렀을 때)
    async check(manual = false) {
        if (!manual && !this.isEnabled()) return null;
        try {
            const info = await fetchLatest();
            this.lastCheckedAt = Date.now();
            this.lastError = '';
            if (isNewer(info.tag, this.current)) {
                const isNew = !this.latest || this.latest.tag !== info.tag;
                this.latest = info;
                log.info('[Update] 새 버전:', info.tag, '(현재', this.current + ')');
                // 같은 버전을 계속 알리지 않는다 — 처음 찾았을 때만
                if (isNew && this.onFound) this.onFound(info);
                return info;
            }
            log.info('[Update] 최신 버전 사용 중:', this.current);
            this.latest = null;
            return null;
        } catch (e) {
            // 네트워크가 없거나 GitHub 이 잠깐 죽어도 앱은 아무 영향 없어야 한다
            this.lastError = e.message;
            log.warn('[Update] 확인 실패:', e.message);
            return null;
        }
    }

    status() {
        return {
            current: this.current,
            latest: this.latest ? this.latest.tag : '',
            url: this.latest ? this.latest.url : RELEASES_PAGE,
            checkedAt: this.lastCheckedAt,
            error: this.lastError,
        };
    }
}

module.exports = Updater;
module.exports.isNewer = isNewer;
module.exports.RELEASES_PAGE = RELEASES_PAGE;
