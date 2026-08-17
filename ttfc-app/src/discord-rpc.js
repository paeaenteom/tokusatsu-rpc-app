// ============================================================
//  TOKU RPC — Discord Rich Presence (Queue System)
//
//  Discord rate limit: ~5 setActivity per 20s
//  → Queue + debounce + 우선순위 (재생/일시정지 변경은 즉시)
//
//  ── 표시 형식 (영상 시청 중) ──
//   1줄: "TOKU RPC"                      (앱 이름 — Discord Dev Portal에서 설정)
//   2줄: "{사이트명} 시청 중"              (smallImageText)
//   3줄: 작품명                           (details)
//   4줄: "{에피소드번호} / {제목}"          (state)
//   5줄: 00:06 ━━━━━━ 24:01              (start/endTimestamp — 재생 중일 때만)
//
//  ── 표시 형식 (브라우징) ──
//   details: 페이지 타입 (作品ページ / シリーズ一覧 / ホーム ...)
//   state:   페이지 상세 (작품명/시리즈명) — 없으면 생략
//   largeImage: 시리즈 배너(TTFC) / 키 아트(IMAGINATION) / 사이트 로고
//   startTimestamp: 페이지 진입 시점 (URL 바뀌기 전까지 유지)
// ============================================================

const RPC = require('discord-rpc');
const log = require('electron-log');
const secrets = require('./secrets');
const i18n = require('./i18n');

// ── Discord 전송 속도 (2026-08-14 전면 교체) ──
//  예전: 변경이 생길 때마다 무조건 기다렸다 — 일반 1000ms, 변경 이벤트도 500ms.
//        확장 부스터를 켜도 체감이 그대로였던 진짜 이유가 이것이다.
//        확장만 빨라지고 앱이 매번 같은 시간을 다시 깔아버렸다.
//  지금: 토큰 버킷. Discord RPC 는 SET_ACTIVITY 를 20초에 5번까지 받는데,
//        평소(가끔 재생/일시정지/화 넘김)엔 토큰이 남아 있으므로 **대기 0ms 로 즉시** 나간다.
//        진짜 연타일 때만 한도에 맞춰 간격을 벌린다 — 한도를 넘지 않는 건 그대로다.
const RPC_BURST = 5;         // 몰아서 즉시 보낼 수 있는 횟수
const RPC_REFILL_MS = 4000;  // 토큰 1개 회복 시간 (5개 / 20초)
const THUMB_TTL = 2 * 60 * 60 * 1000; // 재호스팅 URL 유효기간 2시간 (uguu 3h·litterbox 72h 만료 대비)

// ── 사이트별 설정 (Discord Art Assets 키는 Dev Portal에 업로드 필요) ──
//  ⚠ 사이트마다 Discord 애플리케이션이 다르다 (에셋을 각 앱에 나눠 올림).
//    시청 사이트가 바뀌면 해당 앱 ID로 다시 연결한다.
//    ・TTFC 앱 에셋:  ttfc_logo / play / pause /
//                    kamen_rider_logo · super_sentai_series_logo ·
//                    metal_hero_series_logo · project_r_e_d_logo
//    ・IMAGINATION 앱 에셋: tsuburaya_imagination_logo / play / pause
const SITES = {
    ttfc: {
        name: '東映特撮ファンクラブ',
        homeUrl: 'https://pc.tokusatsu-fc.jp/',
        appId: '946694629506555955',
        logo: 'ttfc_logo',
        play: 'play',
        pause: 'pause',
    },
    imagination: {
        name: 'TSUBURAYA IMAGINATION',
        homeUrl: 'https://imagination.m-78.jp/',
        appId: '1532808121276305418',
        logo: 'tsuburaya_imagination_logo',
        play: 'play',
        pause: 'pause',
    },
    // ── 디즈니+ (실험) ──
    //  아직 검증 중이라 전용 테스트 애플리케이션을 쓴다. 기존 두 사이트와 앱이
    //  분리돼 있어 여기서 뭘 잘못 띄워도 TTFC/IMAGINATION 표시에는 영향이 없다.
    //  안정화되면 정식 앱으로 옮긴다.
    //  에셋: disney__logo(밑줄 2개) / marvel_logo / play / pause
    disneyplus: {
        name: 'Disney+',
        homeUrl: 'https://www.disneyplus.com/',
        appId: '1536675829914931290',
        logo: 'disney__logo',
        play: 'play',
        pause: 'pause',
    },
};

// 애플리케이션 ID — 기본값은 위 상수, 개인 설정이 있으면 그쪽을 우선한다
// (설치만 하면 바로 동작하고, 자기 앱을 쓰고 싶으면 설정으로 덮어쓸 수 있게)
function appIdOf(siteId) {
    return secrets.appId(siteId) || (siteOf(siteId).appId) || SITES.ttfc.appId;
}

function siteOf(id) {
    return SITES[id] || SITES.ttfc;
}

// Discord 필드 길이 제한 (128자) 대응
function clamp(str, max = 128) {
    if (!str) return str;
    return str.length > max ? str.slice(0, max - 1) + '…' : str;
}

// 디스코드의 이미지 프록시가 인증 없이 직접 불러올 수 있는 호스트.
//  ⚠ "재호스팅을 건너뛴다"는 뜻이 아니다(2026-08-14 변경). 이제 모든 썸네일은
//    확장에서 정사각으로 다듬어 재호스팅한다 — 디스코드가 large_image 를 정사각으로
//    잘라 그려서, 16:9 원본을 그대로 주면 좌우가 잘리기 때문이다.
//    이 목록은 "정사각본이 올라오기 전까지 원본이라도 띄워둘 수 있는가"에만 쓴다.
const DIRECT_IMAGE_HOSTS = [
    /\.cloudfront\.net\//i,                  // TTFC 에피소드 썸네일
    /\bdisney\.images\.edge\.bamgrid\.com\//i, // 디즈니+ 아트워크 (쿠키 없이 200 확인)
];
function canDiscordLoad(url) {
    return DIRECT_IMAGE_HOSTS.some((re) => re.test(url));
}

// Discord 는 details/state 가 2글자 미만이면 활동 전체를 거부한다.
//  (실제로 겪음: 페이지 이름을 한국어로 바꾸자 '홈' 1글자가 되어 브라우징 표시가 통째로 막혔다.
//   조용히 거부되고 프로필에는 아무것도 안 뜬다.)
//  번역이 짧아져도 표시가 죽지 않도록, 짧으면 사이트 이름을 덧붙여 늘린다.
function atLeast2(str, pad) {
    const s = (str || '').trim();
    if (s.length >= 2) return s;
    if (!s) return undefined;                 // 빈 값은 아예 안 보내는 게 맞다
    const suffix = (pad || '').trim();
    return suffix ? `${s} · ${suffix}` : `${s} `.padEnd(2, ' ');
}

function isHttpUrl(u) {
    return typeof u === 'string' && /^https?:\/\//i.test(u);
}

// ── 재호스팅 결과를 디스크에 남긴다 (2026-08-15) ──
//  앱을 껐다 켤 때마다 캐시가 통째로 비어, 보던 이미지를 전부 다시 올리고 있었다.
//  업로드 호스트마다 실제 보관 기간이 다르므로(litterbox 72h · uguu 3h · catbox 영구)
//  항목마다 유효기간을 같이 적어 두고, 지난 것은 읽을 때 버린다.
//  ※ Electron 밖(테스트)에서는 저장하지 않는다 — require('electron') 이 앱 API 를 주지 않는다.
const HOSTED_CACHE_MAX = 300;
function hostedCachePath() {
    try {
        const { app } = require('electron');
        if (app && typeof app.getPath === 'function') {
            return require('path').join(app.getPath('userData'), 'thumb-cache.json');
        }
    } catch (e) { /* Electron 아님 */ }
    return '';
}

function loadHostedCache() {
    const p = hostedCachePath();
    const m = new Map();
    if (!p) return m;
    try {
        const raw = JSON.parse(require('fs').readFileSync(p, 'utf8'));
        const now = Date.now();
        let dropped = 0;
        for (const [url, e] of Object.entries(raw || {})) {
            if (!e || !e.url || !e.at) continue;
            if (now - e.at > (e.keepMs || THUMB_TTL)) { dropped++; continue; }
            m.set(url, e);
        }
        log.info(`[RPC] 썸네일 캐시 복원: ${m.size}건 (만료 ${dropped}건 버림)`);
    } catch (e) { /* 없거나 깨졌으면 빈 캐시로 시작 */ }
    return m;
}

function saveHostedCache(map) {
    const p = hostedCachePath();
    if (!p) return;
    try {
        // 오래된 것부터 잘라 파일이 무한히 커지지 않게 (Map 은 삽입 순서 유지)
        const entries = [...map.entries()].slice(-HOSTED_CACHE_MAX);
        require('fs').writeFileSync(p, JSON.stringify(Object.fromEntries(entries)), 'utf8');
    } catch (e) {
        log.warn('[RPC] 썸네일 캐시 저장 실패:', e.message);
    }
}

// dataURL(이미지) → 공개 이미지 호스트 업로드 → 디스코드가 렌더 가능한 URL 반환
//  ※ Node 요청은 Origin 헤더가 없어야 통과한다(확장/브라우저 Origin은 412 거부).
//  ※ 단일 호스트 의존은 위험 — 2026-07-26 catbox가 "Uploads paused"(412)로
//    장기 중단되며 썸네일이 전부 로고로 폴백됐다. 그래서 여러 호스트를 순서대로
//    시도하고, 성공한 호스트를 다음 업로드의 1순위로 기억한다.
const UPLOAD_HOSTS = [
    {
        name: 'litterbox',   // catbox 임시 저장소 (72시간)
        keepMs: 60 * 60 * 60 * 1000,   // 72h 중 60h 만 믿는다 (여유)
        async up(buf, type, name) {
            const fd = new FormData();
            fd.append('reqtype', 'fileupload');
            fd.append('time', '72h');
            fd.append('fileToUpload', new Blob([buf], { type }), name);
            const r = await fetch('https://litterbox.catbox.moe/resources/internals/api.php',
                { method: 'POST', body: fd, headers: { 'User-Agent': 'curl/8.4.0' } });
            const t = (await r.text()).trim();
            if (!r.ok || !/^https?:\/\//.test(t)) throw new Error('HTTP ' + r.status + ' ' + t.slice(0, 50));
            return t;
        },
    },
    {
        name: 'uguu',        // 3시간 보관
        keepMs: 2 * 60 * 60 * 1000,
        async up(buf, type, name) {
            const fd = new FormData();
            fd.append('files[]', new Blob([buf], { type }), name);
            const r = await fetch('https://uguu.se/upload?output=text',
                { method: 'POST', body: fd, headers: { 'User-Agent': 'curl/8.4.0' } });
            const t = (await r.text()).trim();
            if (!r.ok || !/^https?:\/\//.test(t)) throw new Error('HTTP ' + r.status + ' ' + t.slice(0, 50));
            return t;
        },
    },
    {
        name: 'catbox',      // 영구 — 서비스 복구되면 자동으로 다시 쓰임
        keepMs: 30 * 24 * 60 * 60 * 1000,   // 영구지만 30일로 끊는다
        async up(buf, type, name) {
            const fd = new FormData();
            fd.append('reqtype', 'fileupload');
            fd.append('fileToUpload', new Blob([buf], { type }), name);
            const r = await fetch('https://catbox.moe/user/api.php',
                { method: 'POST', body: fd, headers: { 'User-Agent': 'curl/8.4.0' } });
            const t = (await r.text()).trim();
            if (!r.ok || !/^https?:\/\//.test(t)) throw new Error('HTTP ' + r.status + ' ' + t.slice(0, 50));
            return t;
        },
    },
];
let _preferredHost = 0;  // 최근 성공한 호스트 인덱스

async function uploadImage(dataUrl) {
    const m = /^data:([^;]+);base64,(.+)$/.exec(dataUrl || '');
    if (!m) throw new Error('잘못된 dataURL');
    const buf = Buffer.from(m[2], 'base64');
    // 확장자를 실제 형식에 맞춘다 — 정사각 패딩본은 투명이 필요해 PNG 로 온다
    const ext = /png/i.test(m[1]) ? 'png' : /webp/i.test(m[1]) ? 'webp' : 'jpg';
    const fileName = 'thumb.' + ext;
    const order = [_preferredHost, ...UPLOAD_HOSTS.keys()].filter((v, i, a) => a.indexOf(v) === i);
    const errs = [];
    for (const i of order) {
        const host = UPLOAD_HOSTS[i];
        try {
            const url = await host.up(buf, m[1], fileName);
            _preferredHost = i;
            log.info(`[RPC] 업로드 성공 (${host.name})`);
            return { url, keepMs: host.keepMs || THUMB_TTL };
        } catch (e) {
            errs.push(`${host.name}: ${e.message}`);
        }
    }
    throw new Error('모든 호스트 실패 — ' + errs.join(' / '));
}

class DiscordRichPresence {
    constructor(clientId) {
        this.clientId = clientId;
        this.client = null;
        this.connected = false;
        this.reconnectTimer = null;
        this.lang = 'ko';   // main.js 가 setLang 으로 실제 언어를 넣어준다

        this.currentState = {
            isWatching: false,
            isPlaying: false,
            site: 'ttfc',
            siteName: '',
            seriesName: '',
            episodeTitle: '',
            episodeNumber: '',
            currentTime: 0,
            duration: 0,
            thumbnail: '',
            pageTitle: '',
            pageDetail: '',
            pageBanner: '',
            pageUrl: '',
        };

        this._lastSent = {
            episode: '',
            playing: null,
            currentTime: 0,
            duration: 0,
        };

        // 브라우징 진입 시점 (URL 단위로 유지)
        this._browsingStart = 0;
        this._browsingUrl = '';
        this._lastBrowsingKey = '';
        this._shownType = null;        // 지금 띄운 카드 종류: 'watching' | 'browsing' | null

        // Queue system
        this._lastApiCall = 0;
        this._pendingUpdate = null;
        this._pendingTimer = null;
        this._refreshTimer = null;

        // 5분 비활동 → RPC 끄기
        this._idleTimer = null;
        // 0 = 자동 해제 안 함. 미니 창에서 정할 수 있다 (기본 5분).
        this._IDLE_TIMEOUT = 5 * 60 * 1000;

        // 미니 창용: 상태 변경 콜백 + 현재 표시 내용
        this.onStatusChange = null;
        this.lastActivity = null;

        // 썸네일 재호스팅 캐시: 원본 URL → { url, at } (호스트 만료 대비 TTL)
        //  ※ 업로드와 이미지 바이트 보관은 허브가 한 벌만 맡는다.
        //    클라이언트마다 두면 사이트 수만큼 같은 사진을 중복으로 쥔다.
        this._thumbCache = new Map();
    }

    // 재호스팅 URL 조회 (만료분은 무효 처리 — litterbox 72h·uguu 3h 등)
    _cachedThumb(url) {
        const hit = this._thumbCache.get(url);
        if (!hit) return '';
        if (Date.now() - hit.at > (hit.keepMs || THUMB_TTL)) { this._thumbCache.delete(url); return ''; }
        return hit.url;
    }

    // 이미지 URL → 디스코드가 실제로 그릴 수 있는 URL로 해석
    //  1) 재호스팅 캐시(catbox)에 있으면 언제나 그것 — 확장이 정사각으로 만든 판본이다
    //  2) 아직 없으면, 디스코드가 직접 불러올 수 있는 호스트는 원본이라도 즉시 띄운다
    //     (*.cloudfront.net·bamgrid — 정사각이 아니라 좌우가 잘려 보인다. 임시 표시용)
    //  3) 그 외(imagination CDN·TTFC 메인 도메인 등 외부 차단 호스트)는 로고 폴백
    //     — 재호스팅이 끝나면 cacheThumbnail()이 즉시 갱신해준다
    //
    //  ⚠ 1)과 2)의 순서가 중요하다. 예전엔 2)가 먼저라 cloudfront 원본이 항상 이겼고,
    //    디스코드가 정사각으로 잘라 그려서 16:9 썸네일의 좌우 43%가 날아갔다.
    _resolveImage(url, logo) {
        if (!url) return logo;
        // http가 아니면 Dev Portal 에셋 키로 간주하고 그대로 사용
        // (시리즈 카테고리 로고 등 — 사이트 배너 URL은 외부 접근이 막혀 있음)
        if (!isHttpUrl(url)) return url;
        const cached = this._cachedThumb(url);
        if (cached) return cached;
        if (canDiscordLoad(url)) return url;   // 정사각본이 준비될 때까지의 임시 표시
        return logo;
    }

    // 이미 올라간 재호스팅 URL 을 캐시에 꽂고, 지금 그걸 보여주는 중이면 즉시 갱신.
    //  업로드는 허브가 한 번만 하고(중복 제거), 결과만 각 클라이언트가 받아 간다.
    adoptThumb(url, hosted, keepMs) {
        if (!url || !hosted) return;
        if (this._thumbCache.size >= 60) {
            this._thumbCache.delete(this._thumbCache.keys().next().value);
        }
        this._thumbCache.set(url, { url: hosted, at: Date.now(), keepMs: keepMs || THUMB_TTL });
        const s = this.currentState;
        if (this.lastActivity) {
            if (s.isWatching && s.thumbnail === url) this._forceUpdate('watching');
            else if (!s.isWatching && s.pageBanner === url) this._forceUpdate('browsing');
        }
    }

    _notifyStatus() {
        if (typeof this.onStatusChange === 'function') {
            try { this.onStatusChange(); } catch (e) {}
        }
    }

    getStatus() {
        return {
            connected: this.connected,
            user: (this.client && this.client.user) ? this.client.user.username : '',
            activity: this.lastActivity,
        };
    }

    connect() {
        if (this.connected) return;
        // ⚠ 2026-08-18: 이전 클라이언트를 정리하지 않고 새로 만들면 IPC 핸들이 그대로 남는다.
        //   10초마다 재시도 × 사이트 클라이언트 3개면 핸들이 계속 쌓여, 결국 Discord 가
        //   전부 거부하고 'connection closed' 가 무한 반복된다 — 재시도할수록 더 안 되는
        //   구조였다(실측: 2분 만에 회복 불가). 새로 만들기 전에 반드시 버린다.
        if (this.client) {
            try { this.client.removeAllListeners(); } catch (e) {}
            try { this.client.destroy(); } catch (e) {}
            this.client = null;
        }
        this.client = new RPC.Client({ transport: 'ipc' });

        const origRequest = this.client.request.bind(this.client);
        this.client.request = (cmd, args, evt) => {
            if (cmd === 'SET_ACTIVITY' && args && args.activity) {
                args.activity.type = 3;  // Watching (시청 중)
                args.activity.status_display_type = 2;  // 멤버 목록에 details(작품명) 표시
            }
            return origRequest(cmd, args, evt);
        };

        this.client.on('ready', () => {
            this.connected = true;
            this._reconnectWait = 10000;   // 붙었으니 다음 재시도 간격을 원위치
            this._switchingTo = null;
            log.info('[RPC] Connected:', this.client.user?.username);
            this._notifyStatus();
            // 앱 전환 중 보류된 액티비티가 있으면 지금 보낸다
            if (this._afterSwitch) {
                const a = this._afterSwitch;
                this._afterSwitch = null;
                setTimeout(() => this._sendNow(a), 300);
            }
            // 초기 placeholder는 보내지 않음 — 사이트를 실제로 볼 때만 표시
        });

        this.client.on('disconnected', () => {
            this.connected = false;
            this._stopRefreshTimer();
            this._notifyStatus();
            this._scheduleReconnect();
        });

        this.client.login({ clientId: this.clientId }).catch((err) => {
            log.warn('[RPC] Connect failed:', err.message);
            this.connected = false;
            this._scheduleReconnect();
        });
    }

    disconnect() {
        if (this.reconnectTimer) { clearTimeout(this.reconnectTimer); this.reconnectTimer = null; }
        this._stopRefreshTimer();
        if (this._pendingTimer) { clearTimeout(this._pendingTimer); this._pendingTimer = null; }
        if (this._idleTimer) { clearTimeout(this._idleTimer); this._idleTimer = null; }
        if (this.client) {
            try { this.client.clearActivity(); this.client.destroy(); } catch (e) {}
            this.client = null;
        }
        this.connected = false;
    }

    //  계속 실패하면 간격을 늘린다 (10s → 20s → 40s … 최대 2분).
    //  고정 10초로 두드리면 Discord 가 막고 있는 상황을 더 악화시킬 뿐이다.
    //  한 번이라도 붙으면 _resetBackoff 로 다시 10초부터 시작한다.
    _scheduleReconnect() {
        if (this.reconnectTimer) return;
        const wait = this._reconnectWait || 10000;
        this._reconnectWait = Math.min(wait * 2, 120000);
        this.reconnectTimer = setTimeout(() => { this.reconnectTimer = null; this.connect(); }, wait);
    }

    // ══════════════════════════════════════
    //  From bridge
    // ══════════════════════════════════════

    updateFromVideoState(data) {
        if (!data) return;
        this._lastInputAt = Date.now();   // 확장이 실제로 보내온 시각 (자체 갱신과 구분)

        const settings = data._settings;
        delete data._settings;

        Object.assign(this.currentState, data);
        this.currentState._settings = settings || {};
        const s = this.currentState;

        if (!s.isWatching) {
            this._lastSent = { episode: '', playing: null, currentTime: 0, duration: 0 };
            this._stopRefreshTimer();
            this._queueUpdate('browsing');
            return;
        }

        // ── 변경 감지 ──
        const newEp = s.site + s.episodeNumber + s.episodeTitle + s.seriesName;
        const playChanged = s.isPlaying !== this._lastSent.playing;
        const episodeChanged = newEp !== this._lastSent.episode;
        const seeked = Math.abs(s.currentTime - this._lastSent.currentTime) > 3;
        const durationLoaded = s.duration > 0 && this._lastSent.duration === 0;
        const firstWatch = this._lastSent.playing === null;
        // 썸네일은 비동기 재호스팅(IMAGINATION)으로 나중에 채워질 수 있음 → 변경 감지에 포함
        const thumbChanged = (s.thumbnail || '') !== (this._lastSent.thumbnail || '');

        if (!playChanged && !episodeChanged && !seeked && !durationLoaded && !firstWatch && !thumbChanged) {
            // 재생 중 자연 경과 — _lastSent.currentTime만 따라가게 갱신
            this._lastSent.currentTime = s.currentTime;
            return;
        }

        this._lastSent.episode = newEp;
        this._lastSent.playing = s.isPlaying;
        this._lastSent.currentTime = s.currentTime;
        this._lastSent.duration = s.duration;
        this._lastSent.thumbnail = s.thumbnail || '';

        // 여기 도달 = 변경 이벤트(재생상태/에피소드/탐색/로드/썸네일) → 전부 즉시 반영
        // (_forceUpdate의 500ms 연타 가드가 rate limit 보호)
        this._forceUpdate('watching');

        // 재생 시작 → 주기적 리프레시 (타임스탬프 보정)
        if (s.isPlaying) {
            this._startRefreshTimer();
        } else {
            this._stopRefreshTimer();
        }
    }

    updateFromNavigation(data) {
        if (!data) return;
        this._lastInputAt = Date.now();   // 확장이 실제로 보내온 시각 (자체 갱신과 구분)

        const settings = data._settings;
        this.currentState.site = data.site || this.currentState.site || 'ttfc';
        this.currentState.siteName = data.siteName || '';
        this.currentState.pageTitle = data.title || '';
        this.currentState.pageDetail = data.detail || '';
        this.currentState.pageBanner = data.banner || '';
        this.currentState.pageUrl = data.url || '';
        if (settings) this.currentState._settings = settings;

        if (!data.isVideoPage) {
            this.currentState.isWatching = false;
            this._lastSent = { episode: '', playing: null, currentTime: 0, duration: 0 };
            this._stopRefreshTimer();

            // 페이지 진입 시점: URL이 바뀔 때만 리셋
            if (this._browsingUrl !== this.currentState.pageUrl) {
                this._browsingUrl = this.currentState.pageUrl;
                this._browsingStart = Math.floor(Date.now() / 1000);
            }

            // 내용이 같으면 재전송 생략 (10초 하트비트로 중복 수신되므로)
            // ※ 5분 유휴로 꺼진 뒤에도 하트비트로는 되살리지 않음 — 페이지 이동 시에만
            //
            // ⚠ 2026-08-15: "키가 같으면 건너뛴다"만으로는 부족하다. 지금 화면에 무엇이
            //   떠 있는지도 봐야 한다. 예전엔 이랬다:
            //     ① 작품 페이지 둘러봄 → 브라우징 카드 표시, 키 기억
            //     ② 영상 재생 → 영상 카드로 바뀜 (키는 그대로 남아 있음)
            //     ③ 뒤로 가기로 ①번 페이지 복귀 → 키가 같다고 갱신을 통째로 건너뜀
            //     → 프로필에는 아까 보던 **영상 카드가 그대로** 남았다.
            //       (실측: 뒤로 간 뒤 44초 동안 안 바뀌었고, 탭을 옮겨 CLEAR 가 나가서야 풀렸다)
            //   그래서 "그 브라우징 카드를 지금 실제로 띄우고 있을 때"만 건너뛴다.
            const key = [this.currentState.site, this.currentState.pageTitle,
                         this.currentState.pageDetail, this.currentState.pageBanner,
                         this.currentState.pageUrl].join('|');
            if (key === this._lastBrowsingKey && this._shownType === 'browsing') return;
            this._lastBrowsingKey = key;

            // 페이지 이동은 즉시 반영 (_forceUpdate에 연타 가드 있음)
            this._forceUpdate('browsing');
        }
    }

    // ══════════════════════════════════════
    //  Queue System
    // ══════════════════════════════════════

    // ── 토큰 버킷 ──
    //  _refill 은 흐른 시간만큼 토큰을 채운다. 가득 차면 기준 시각을 현재로 당겨
    //  오래 안 쓴 시간이 무한히 쌓이지 않게 한다.
    _refill() {
        const now = Date.now();
        if (this._tokens == null) { this._tokens = RPC_BURST; this._tokenAt = now; }
        const gained = Math.floor((now - this._tokenAt) / RPC_REFILL_MS);
        if (gained > 0) {
            this._tokens = Math.min(RPC_BURST, this._tokens + gained);
            this._tokenAt += gained * RPC_REFILL_MS;
        }
        if (this._tokens >= RPC_BURST) { this._tokens = RPC_BURST; this._tokenAt = now; }
    }

    // 지금 보내도 되면 0, 아니면 기다려야 하는 ms
    _msUntilSlot() {
        this._refill();
        if (this._tokens > 0) return 0;
        return Math.max(0, RPC_REFILL_MS - (Date.now() - this._tokenAt));
    }

    _spendSlot() {
        this._refill();
        if (this._tokens > 0) this._tokens--;
    }

    // 실제 전송 예약 — 토큰이 있으면 대기 없이 그 자리에서 보낸다.
    //  뒤늦게 보낼 때도 "마지막 상태 하나"만 보낸다(중간 상태는 어차피 낡았다).
    _schedule(type) {
        if (this._pendingTimer) { clearTimeout(this._pendingTimer); this._pendingTimer = null; }
        this._pendingUpdate = null;

        const wait = this._msUntilSlot();
        if (wait <= 0) { this._flushUpdate(type); return; }   // ← 평소 경로: 0ms

        this._pendingUpdate = type;
        this._pendingTimer = setTimeout(() => {
            this._pendingTimer = null;
            const t = this._pendingUpdate;
            this._pendingUpdate = null;
            if (t) this._flushUpdate(t);
        }, wait + 20);
    }

    // 호출부 이름은 그대로 둔다 — 둘 다 같은 버킷을 쓰므로 이제 우선순위 차이가 없다.
    // (예전엔 1000ms / 500ms 로 갈렸다. 지금은 둘 다 토큰이 있으면 즉시다)
    _queueUpdate(type) { this._schedule(type); }

    _forceUpdate(type) { this._schedule(type); }

    _flushUpdate(type) {
        // 지금 프로필에 어떤 종류의 카드를 띄웠는지 기억한다.
        //  브라우징 중복 생략 판정이 이걸 본다 — 영상 카드를 띄운 채로는
        //  "같은 브라우징 페이지"라도 반드시 다시 보내야 한다.
        this._shownType = type === 'watching' ? 'watching' : 'browsing';
        if (type === 'watching') {
            this._buildAndSendWatching();
        } else {
            this._buildAndSendBrowsing();
        }
    }

    // 재생 중 30초마다 타임스탬프 보정
    _startRefreshTimer() {
        this._stopRefreshTimer();
        this._refreshTimer = setInterval(() => {
            // ⚠ 2026-08-14: 확장이 이 사이트 소식을 끊었으면 되살리지 말고 거둔다.
            //   이게 없으면 30초 갱신이 스스로 _resetIdleTimer 를 다시 감아
            //   5분 유휴 해제가 영원히 발화하지 않는다(실측 확인 — 안전망이 스스로를 무력화).
            //   그 결과 닫힌 탭의 마지막 화면이 프로필에 무한히 남았다.
            if (this._IDLE_TIMEOUT && Date.now() - (this._lastInputAt || 0) > this._IDLE_TIMEOUT) {
                log.info(`[RPC] 확장 소식 ${Math.round(this._IDLE_TIMEOUT / 60000)}분 끊김 → Activity 제거`);
                this.clearActivity();
                return;
            }
            if (this.currentState.isWatching && this.currentState.isPlaying) {
                this._queueUpdate('watching');
            }
        }, 30000);
    }

    _stopRefreshTimer() {
        if (this._refreshTimer) {
            clearInterval(this._refreshTimer);
            this._refreshTimer = null;
        }
    }

    // ══════════════════════════════════════
    //  Activity Builders
    // ══════════════════════════════════════

    // 공통: "사이트에서 보기" 버튼 (showButtons 설정으로 토글)
    _buildButtons(settings) {
        if (settings.showButtons === false) return undefined;
        const site = siteOf(this.currentState.site);
        const url = isHttpUrl(this.currentState.pageUrl) ? this.currentState.pageUrl : site.homeUrl;
        return [{ label: this._t('rpc.viewOnSite'), url }];
    }

    _t(key, vars) { return i18n.t(this.lang, key, vars); }

    // 언어가 바뀌면 지금 떠 있는 표시도 바로 갈아끼운다
    setLang(lang) {
        if (lang === this.lang) return;
        this.lang = lang;
        if (this.connected && this.currentState.isWatching !== undefined) this._forceUpdate();
    }

    _buildAndSendWatching() {
        const s = this.currentState;
        const settings = s._settings || {};
        const site = siteOf(s.site);
        const siteName = s.siteName || site.name;

        // 3줄(details): 작품명
        const seriesName = (settings.showSeries !== false && s.seriesName)
            ? s.seriesName : siteName;

        // 4줄(state): "{번호} / {제목}" — 한쪽만 있으면 그것만
        let episodeText = '';
        if (settings.showEpisode !== false) {
            if (s.episodeNumber && s.episodeTitle) {
                episodeText = `${s.episodeNumber} / ${s.episodeTitle}`;
            } else if (s.episodeNumber) {
                episodeText = s.episodeNumber;
            } else if (s.episodeTitle) {
                episodeText = s.episodeTitle;
            }
        }

        // 큰 이미지: 에피소드 썸네일 → 사이트 로고
        const largeImage = (settings.showThumbnail === false)
            ? site.logo
            : this._resolveImage(s.thumbnail, site.logo);

        const activity = {
            details: atLeast2(clamp(seriesName), siteName),                    // 3줄: 작품명 (큰 글씨)
            state: episodeText ? atLeast2(clamp(episodeText), siteName) : undefined,  // 4줄: 에피소드
            largeImageKey: largeImage,
            largeImageText: clamp(seriesName),
            smallImageKey: s.isPlaying ? site.play : site.pause,
            smallImageText: s.isPlaying
                ? this._t('rpc.watching', { site: siteName })       // 2줄: 사이트명
                : this._t('rpc.paused', { site: siteName }),
            instance: false,
        };

        const buttons = this._buildButtons(settings);
        if (buttons) activity.buttons = buttons;

        // 5줄: 시간바 — 재생 중일 때만 (일시정지 시 timestamps 제거)
        const timeMode = settings.timeMode || 'progress';
        if (timeMode !== 'none' && settings.showTime !== false && s.duration > 0 && s.isPlaying) {
            const now = Math.floor(Date.now() / 1000);
            if (timeMode === 'progress') {
                activity.startTimestamp = now - s.currentTime;
                activity.endTimestamp = activity.startTimestamp + s.duration;
            } else if (timeMode === 'remaining') {
                activity.endTimestamp = now + (s.duration - s.currentTime);
            }
        }

        this._sendNow(activity);
    }

    _buildAndSendBrowsing() {
        const s = this.currentState;
        const settings = s._settings || {};
        const site = siteOf(s.site);
        const siteName = s.siteName || site.name;

        const pageTitle = s.pageTitle || '';
        const pageDetail = s.pageDetail || '';
        const pageBanner = s.pageBanner || '';

        // details: 페이지 타입 / state: 페이지 상세 (없으면 생략)
        // 확장이 페이지 종류를 못 보냈을 때만 쓰는 대체값 (보통은 확장 쪽 번역이 온다)
        const details = atLeast2(pageTitle || this._t('rpc.browsing'), siteName);
        const state = pageDetail ? atLeast2(clamp(pageDetail), siteName) : undefined;

        // 큰 이미지: 페이지 배너(작품 비주얼/시리즈 배너/프라네트 아이콘) → 사이트 로고
        const largeImage = this._resolveImage(pageBanner, site.logo);

        // 브라우징 중엔 작은 이미지(play/pause) 없음 — 영상 시청 시에만 표시 (유빈 요청)
        const activity = {
            details: clamp(details),
            state,
            largeImageKey: largeImage,
            largeImageText: clamp(pageDetail || siteName),
            // 페이지 진입 시점부터 경과 (URL 바뀔 때만 리셋)
            startTimestamp: this._browsingStart || Math.floor(Date.now() / 1000),
            instance: false,
        };

        const buttons = this._buildButtons(settings);
        if (buttons) activity.buttons = buttons;

        this._sendNow(activity);
    }

    // ══════════════════════════════════════
    //  Discord API (단일 호출 지점)
    // ══════════════════════════════════════

    // 현재 사이트에 맞는 Discord 앱으로 연결돼 있는지 확인 (아니면 전환 시작)
    //  전환은 비동기라, 준비되면 ready 핸들러가 보류된 액티비티를 다시 보낸다.
    _ensureApp() {
        const want = appIdOf(this.currentState.site);
        if (!want || want === this.clientId) return true;
        log.info(`[RPC] 앱 전환: ${this.clientId} → ${want} (${this.currentState.site})`);
        this._switchingTo = want;
        this.disconnect();
        this.clientId = want;
        this.connect();
        return false;
    }

    _sendNow(activity) {
        if (!this._ensureApp()) { this._afterSwitch = activity; return; }
        if (!this.connected || !this.client) return;
        this._lastApiCall = Date.now();
        this._spendSlot();               // 토큰 1개 소모 (한도 초과 방지)
        this._resetIdleTimer();
        // 진단 로그는 큰 이미지가 바뀔 때만 (로그 파일 비대 방지)
        const imgKey = String(activity.largeImageKey || '');
        if (imgKey !== this._lastLoggedImage) {
            this._lastLoggedImage = imgKey;
            log.info('[RPC] setActivity largeImageKey =', imgKey.slice(0, 110));
        }
        try {
            // setActivity는 Promise 반환 — 미처리 시 unhandledRejection으로 터진다
            // (Discord 재시작·rate limit 때 'Unknown Error'가 로그를 도배했음)
            const p = this.client.setActivity(activity);
            if (p && typeof p.catch === 'function') {
                p.catch((err) => log.warn('[RPC] setActivity 거부:', err && err.message));
            }
            this.lastActivity = activity;
            this._notifyStatus();
        } catch (err) {
            log.error('[RPC] Failed:', err.message);
        }
    }

    _resetIdleTimer() {
        if (this._idleTimer) { clearTimeout(this._idleTimer); this._idleTimer = null; }
        if (!this._IDLE_TIMEOUT) return;   // '안 사라짐' — 스스로 내리지 않는다
        this._idleTimer = setTimeout(() => {
            log.info(`[RPC] ${Math.round(this._IDLE_TIMEOUT / 60000)}분 비활동 → Activity 제거`);
            this._stopRefreshTimer();
            try { if (this.client) this.client.clearActivity(); } catch (e) {}
            this.lastActivity = null;
            this._shownType = null;   // 내려갔으니 같은 페이지라도 다시 올릴 수 있어야 한다
            // _lastBrowsingKey는 유지 — 동일 페이지 하트비트로 되살아나지 않게
            this._notifyStatus();
        }, this._IDLE_TIMEOUT);
    }

    // 자동 해제까지의 시간(ms). 0 이면 스스로 내리지 않는다.
    //  이미 걸려 있는 타이머는 새 값으로 다시 건다 — 설정을 바꾼 즉시 반영되게.
    setIdleTimeout(ms) {
        this._IDLE_TIMEOUT = Math.max(0, Number(ms) || 0);
        if (this._idleTimer) { clearTimeout(this._idleTimer); this._idleTimer = null; }
        if (this._IDLE_TIMEOUT && this.lastActivity) this._resetIdleTimer();
    }

    clearActivity() {
        // ⚠ 조기 반환을 여기 두면 안 된다. Discord 가 잠깐 끊긴 사이에 들어온 CLEAR 가
        //   통째로 무시돼, 다시 붙는 순간 낡은 상태가 되살아난다.
        //   → 내부 상태 정리는 무조건, Discord 호출만 연결돼 있을 때.
        this._stopRefreshTimer();
        if (this._idleTimer) { clearTimeout(this._idleTimer); this._idleTimer = null; }
        // 죽은 프레즌스 부활 방지: 대기 중인 큐 발화·비동기 재호스팅 완료가
        // clear 이후 stale activity를 재전송하지 못하게 상태까지 리셋
        if (this._pendingTimer) { clearTimeout(this._pendingTimer); this._pendingTimer = null; }
        this._pendingUpdate = null;
        this.currentState.isWatching = false;
        // _lastSent 도 비운다 — 안 그러면 지운 뒤 똑같은 상태가 다시 와도
        // 변경 감지에서 "바뀐 게 없다"고 걸러져 프레즌스가 안 돌아온다
        // (일시정지 중이면 currentTime 도 안 늘어 영영 복구되지 않았다)
        this._lastSent = { episode: '', playing: null, currentTime: 0, duration: 0 };
        if (this.connected && this.client) {
            try { this.client.clearActivity(); } catch (e) {}
        }
        this.lastActivity = null;
        this._lastBrowsingKey = '';
        this._shownType = null;
        this._notifyStatus();
    }
}

// ══════════════════════════════════════════════════════════
//  여러 사이트를 동시에 표시하기 위한 허브
//
//  예전에는 클라이언트 하나를 돌려쓰며 사이트가 바뀔 때마다 Discord
//  애플리케이션을 갈아끼웠다. 그래서 아무리 여러 사이트를 켜 둬도 한 번에
//  하나만 떴고, 어느 쪽을 보여줄지 정하느라 계속 깜빡였다.
//
//  사이트마다 애플리케이션이 따로 있으니 클라이언트도 따로 두면 된다.
//  Discord 는 서로 다른 애플리케이션의 활동을 동시에 받는다(실측 확인).
//  → 켜 둔 사이트가 전부 프로필에 뜬다.
//
//  바깥에서 보는 메서드 이름은 예전 그대로라 부르는 쪽은 고칠 게 거의 없다.
// ══════════════════════════════════════════════════════════
class DiscordRpcHub {
    constructor(defaultClientId) {
        this.defaultClientId = defaultClientId;
        this.clients = new Map();     // siteId → DiscordRichPresence
        this.lang = 'ko';
        this.onStatusChange = null;
        this._enabled = true;
        this._uploading = new Set();  // 업로드 진행 중인 원본 URL (중복 업로드 차단)
        this._bytes = new Map();      // 원본 URL → 이미지 바이트 (허브에 한 벌만)
        // 재호스팅 결과: 원본 URL → { url, at, keepMs }. 디스크에 남겨 재시작을 견딘다.
        this._hosted = loadHostedCache();
        this._saveTimer = null;
    }

    // 디스크 저장은 몰아서 (업로드가 연달아 끝날 때 파일을 계속 쓰지 않게)
    _saveHosted() {
        if (this._saveTimer) return;
        this._saveTimer = setTimeout(() => {
            this._saveTimer = null;
            saveHostedCache(this._hosted);
        }, 2000);
        if (this._saveTimer.unref) this._saveTimer.unref();
    }

    // 새로 만든 클라이언트에 이미 아는 재호스팅 결과를 심어 준다.
    //  이게 없으면 사이트 클라이언트가 생길 때마다 캐시가 빈 채로 시작해
    //  같은 이미지를 또 올리게 된다.
    _seedHosted(client) {
        const now = Date.now();
        for (const [url, e] of this._hosted) {
            if (now - e.at > (e.keepMs || THUMB_TTL)) continue;
            client._thumbCache.set(url, { url: e.url, at: e.at, keepMs: e.keepMs });
        }
    }

    // 그 사이트 전용 클라이언트를 (없으면 만들어) 돌려준다
    _for(siteId) {
        const id = SITES[siteId] ? siteId : 'ttfc';
        let c = this.clients.get(id);
        if (!c) {
            c = new DiscordRichPresence(appIdOf(id));
            this._seedHosted(c);   // 이미 올려 둔 이미지는 다시 올리지 않게
            c.setLang(this.lang);
            if (this.idleTimeout != null) c.setIdleTimeout(this.idleTimeout);
            c.onStatusChange = () => { if (this.onStatusChange) this.onStatusChange(); };
            this.clients.set(id, c);
            log.info(`[RPC] 사이트 클라이언트 생성: ${id} (${appIdOf(id)})`);
            if (this._enabled) c.connect();
        }
        return c;
    }

    updateFromVideoState(s) { this._for(s && s.site).updateFromVideoState(s); }
    updateFromNavigation(s) { this._for(s && s.site).updateFromNavigation(s); }

    // 사이트를 주면 그 사이트만, 안 주면 전부 지운다
    clearActivity(siteId) {
        if (siteId && this.clients.has(siteId)) { this.clients.get(siteId).clearActivity(); return; }
        if (siteId) return;                       // 아직 만든 적 없는 사이트 → 지울 것도 없다
        for (const c of this.clients.values()) c.clearActivity();
    }

    // 사이트 상태가 오기 전에도 Discord 에는 붙어 있어야 한다.
    //  클라이언트를 사이트별로 만들게 바꾸면서, 지원 사이트를 열기 전까지는
    //  클라이언트가 하나도 없어 이 순회가 빈 채로 끝났다. 그래서 미니 창이
    //  "Discord 연결 안 됨" 으로 보였다 — 실제로는 연결을 시도조차 안 한 것이다.
    //  기본 클라이언트를 하나 띄워 예전 동작(켜면 바로 연결)을 되돌린다.
    //  활동은 올리지 않으므로 프로필에는 아무것도 안 뜬다.
    connect() {
        this._enabled = true;
        if (this.clients.size === 0) this._for('ttfc');   // 만들면서 connect 까지 한다
        for (const c of this.clients.values()) c.connect();
    }
    disconnect() { this._enabled = false; for (const c of this.clients.values()) c.disconnect(); }

    setLang(lang) { this.lang = lang; for (const c of this.clients.values()) c.setLang(lang); }

    // 자동 해제 시간 — 나중에 생기는 클라이언트에도 같은 값이 가도록 기억해 둔다
    setIdleTimeout(ms) {
        this.idleTimeout = Math.max(0, Number(ms) || 0);
        for (const c of this.clients.values()) c.setIdleTimeout(this.idleTimeout);
    }

    // 썸네일 바이트 → 공개 호스트에 올려 전 클라이언트 캐시에 꽂는다.
    //  ⚠ 2026-08-14: 예전엔 클라이언트마다 각자 올렸다(c.cacheThumbnail 을 순회).
    //    사이트 클라이언트가 3개면 **똑같은 이미지를 3번** 업로드해, 네트워크를 3배 쓰고
    //    화면에 뜨는 것도 그만큼 늦어졌다 (로그에서 재호스팅 OK 가 매번 3줄씩 찍혔다).
    //    이제 허브에서 한 번만 올리고 결과 URL 만 나눠준다.
    //  rawDataUrl: 원본(비정사각). 정주행 알림 첨부용으로 따로 보관한다.
    //   업로드/RPC 카드는 정사각본(dataUrl)을 쓰지만, 웹훅 임베드는 이미지를 그대로
    //   비율대로 그리므로 정사각본을 붙이면 좌우가 빈 띠가 되어 좁아 보인다.
    async cacheThumbnail(url, dataUrl, attempt = 0, rawDataUrl) {
        if (!url) return;
        if (attempt === 0) {
            this.rememberThumbBytes(url, rawDataUrl || dataUrl);   // 알림 첨부는 원본 우선
            if (this._cachedThumb(url) || this._uploading.has(url)) return;
        }
        this._uploading.add(url);   // 재시도 대기 중에도 유지 → 중복 업로드 방지

        let hosted = '', keepMs = THUMB_TTL;
        try {
            const r = await uploadImage(dataUrl);
            hosted = r.url; keepMs = r.keepMs;
        } catch (e) {
            log.warn(`[RPC] 썸네일 재호스팅 실패(${attempt + 1}/4):`, e.message);
            if (attempt < 3) {
                const wait = 15000 * Math.pow(2, attempt);      // 15s → 30s → 60s
                setTimeout(() => this.cacheThumbnail(url, dataUrl, attempt + 1, rawDataUrl), wait);
            } else {
                this._uploading.delete(url);                    // 최종 포기 → 로고 유지
            }
            return;
        }

        this._uploading.delete(url);
        log.info('[RPC] 썸네일 재호스팅 OK →', hosted);
        // 디스크에도 남긴다 — 앱을 껐다 켜도 같은 이미지를 다시 올리지 않는다
        // 상한까지 줄인다 (한 번에 하나만 지우면 이미 넘친 상태에서 영영 안 줄어든다)
        while (this._hosted.size >= HOSTED_CACHE_MAX) this._hosted.delete(this._hosted.keys().next().value);
        this._hosted.set(url, { url: hosted, at: Date.now(), keepMs });
        this._saveHosted();
        for (const c of this.clients.values()) c.adoptThumb(url, hosted, keepMs);
    }
    // 이미지 바이트는 허브에 딱 한 벌만 둔다.
    //  ⚠ 예전엔 클라이언트마다 각자 20장씩 들고 있었다. 사이트가 3개면 **같은 사진을
    //    3벌** 쥐는 셈이라, 정주행 웹훅용 바이트 하나 때문에 수 MB 를 그냥 낭비했다.
    //    (getThumbBytes 를 쓰는 곳은 binge-webhook 한 군데뿐인데 거기도 허브로 묻는다)
    rememberThumbBytes(url, dataUrl) {
        const m = /^data:([^;]+);base64,(.+)$/.exec(dataUrl || '');
        if (!url || !m) return;
        if (this._bytes.size >= 20) {                       // 상한은 예전과 같다
            this._bytes.delete(this._bytes.keys().next().value);
        }
        this._bytes.set(url, { buf: Buffer.from(m[2], 'base64'), type: m[1], at: Date.now() });
    }
    getThumbBytes(url) {
        return this._bytes.get(url) || null;
    }
    _resolveImage(url, logo) {
        // 캐시를 가진 클라이언트가 있으면 그 결과를 쓴다 (재호스팅 URL 확보용)
        for (const c of this.clients.values()) {
            const r = c._resolveImage(url, '');
            if (r) return r;
        }
        return logo;
    }
    _cachedThumb(url) {
        const e = this._hosted.get(url);
        if (e) {
            if (Date.now() - e.at <= (e.keepMs || THUMB_TTL)) return e.url;
            this._hosted.delete(url);   // 만료 — 다음 번에 다시 올린다
        }
        for (const c of this.clients.values()) { const r = c._cachedThumb(url); if (r) return r; }
        return '';
    }
    // 어느 클라이언트든 올리는 중이면 "진행 중"으로 본다 (중복 요청 방지)
    // 업로드가 도는 동안엔 앱이 NEED_THUMB 을 또 보내지 않게 한다.
    //  extension-bridge._requestThumbIfNeeded 가 이걸 본다.
    get _thumbPending() {
        const uploading = this._uploading;
        return { has: (url) => uploading.has(url) };
    }

    // 미니 창에는 대표 하나만 보여준다 — 재생 중인 쪽을 우선한다
    getStatus() {
        let connected = false, user = '', activity = null, watching = false;
        for (const c of this.clients.values()) {
            const s = c.getStatus();
            if (s.connected) connected = true;
            if (!user && s.user) user = s.user;
            const isWatching = !!(c.currentState && c.currentState.isWatching);
            if (s.activity && (!activity || (isWatching && !watching))) { activity = s.activity; watching = isWatching; }
        }
        return { connected, user, activity };
    }
}

module.exports = DiscordRpcHub;
module.exports.DiscordRichPresence = DiscordRichPresence;
module.exports.canDiscordLoad = canDiscordLoad;
