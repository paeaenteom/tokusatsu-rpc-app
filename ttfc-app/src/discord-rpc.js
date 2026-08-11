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

const MIN_UPDATE_INTERVAL = 1000; // Discord 안전 간격 (변경 이벤트는 _forceUpdate의 500ms 경로)
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

// dataURL(이미지) → 공개 이미지 호스트 업로드 → 디스코드가 렌더 가능한 URL 반환
//  ※ Node 요청은 Origin 헤더가 없어야 통과한다(확장/브라우저 Origin은 412 거부).
//  ※ 단일 호스트 의존은 위험 — 2026-07-26 catbox가 "Uploads paused"(412)로
//    장기 중단되며 썸네일이 전부 로고로 폴백됐다. 그래서 여러 호스트를 순서대로
//    시도하고, 성공한 호스트를 다음 업로드의 1순위로 기억한다.
const UPLOAD_HOSTS = [
    {
        name: 'litterbox',   // catbox 임시 저장소 (72시간)
        async up(buf, type) {
            const fd = new FormData();
            fd.append('reqtype', 'fileupload');
            fd.append('time', '72h');
            fd.append('fileToUpload', new Blob([buf], { type }), 'thumb.jpg');
            const r = await fetch('https://litterbox.catbox.moe/resources/internals/api.php',
                { method: 'POST', body: fd, headers: { 'User-Agent': 'curl/8.4.0' } });
            const t = (await r.text()).trim();
            if (!r.ok || !/^https?:\/\//.test(t)) throw new Error('HTTP ' + r.status + ' ' + t.slice(0, 50));
            return t;
        },
    },
    {
        name: 'uguu',        // 3시간 보관
        async up(buf, type) {
            const fd = new FormData();
            fd.append('files[]', new Blob([buf], { type }), 'thumb.jpg');
            const r = await fetch('https://uguu.se/upload?output=text',
                { method: 'POST', body: fd, headers: { 'User-Agent': 'curl/8.4.0' } });
            const t = (await r.text()).trim();
            if (!r.ok || !/^https?:\/\//.test(t)) throw new Error('HTTP ' + r.status + ' ' + t.slice(0, 50));
            return t;
        },
    },
    {
        name: 'catbox',      // 영구 — 서비스 복구되면 자동으로 다시 쓰임
        async up(buf, type) {
            const fd = new FormData();
            fd.append('reqtype', 'fileupload');
            fd.append('fileToUpload', new Blob([buf], { type }), 'thumb.jpg');
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
    const order = [_preferredHost, ...UPLOAD_HOSTS.keys()].filter((v, i, a) => a.indexOf(v) === i);
    const errs = [];
    for (const i of order) {
        const host = UPLOAD_HOSTS[i];
        try {
            const url = await host.up(buf, m[1]);
            _preferredHost = i;
            log.info(`[RPC] 업로드 성공 (${host.name})`);
            return url;
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

        // Queue system
        this._lastApiCall = 0;
        this._pendingUpdate = null;
        this._pendingTimer = null;
        this._refreshTimer = null;

        // 5분 비활동 → RPC 끄기
        this._idleTimer = null;
        this._IDLE_TIMEOUT = 5 * 60 * 1000;

        // 미니 창용: 상태 변경 콜백 + 현재 표시 내용
        this.onStatusChange = null;
        this.lastActivity = null;

        // 썸네일 재호스팅 캐시: 원본 URL → { url, at } (호스트 만료 대비 TTL)
        this._thumbCache = new Map();
        this._thumbPending = new Set();
        // 원본 이미지 바이트 보관 (정주행 웹훅에 파일로 직접 첨부용)
        //  외부 호스트는 만료되지만(litterbox 72h·uguu 3h) 디스코드에 첨부하면 영구 보존된다.
        this._thumbBytes = new Map();
    }

    // 확장이 보낸 이미지 바이트를 기억 (업로드 성공 여부와 무관하게 보관)
    rememberThumbBytes(url, dataUrl) {
        const m = /^data:([^;]+);base64,(.+)$/.exec(dataUrl || '');
        if (!url || !m) return;
        if (this._thumbBytes.size >= 20) {
            this._thumbBytes.delete(this._thumbBytes.keys().next().value);
        }
        this._thumbBytes.set(url, { buf: Buffer.from(m[2], 'base64'), type: m[1], at: Date.now() });
    }

    getThumbBytes(url) {
        return this._thumbBytes.get(url) || null;
    }

    // 재호스팅 URL 조회 (만료분은 무효 처리 — litterbox 72h·uguu 3h 등)
    _cachedThumb(url) {
        const hit = this._thumbCache.get(url);
        if (!hit) return '';
        if (Date.now() - hit.at > THUMB_TTL) { this._thumbCache.delete(url); return ''; }
        return hit.url;
    }

    // 이미지 URL → 디스코드가 실제로 그릴 수 있는 URL로 해석
    //  1) 재호스팅 캐시(catbox)에 있으면 그것
    //  2) *.cloudfront.net(TTFC)은 디스코드 프록시가 직접 로드 가능(실증) → 원본 그대로
    //  3) 그 외(imagination CDN·TTFC 메인 도메인 등 외부 차단 호스트)는 로고 폴백
    //     — 재호스팅이 끝나면 cacheThumbnail()이 즉시 갱신해준다
    _resolveImage(url, logo) {
        if (!url) return logo;
        // http가 아니면 Dev Portal 에셋 키로 간주하고 그대로 사용
        // (시리즈 카테고리 로고 등 — 사이트 배너 URL은 외부 접근이 막혀 있음)
        if (!isHttpUrl(url)) return url;
        const cached = this._cachedThumb(url);
        if (cached) return cached;
        if (/\.cloudfront\.net\//i.test(url)) return url;
        return logo;
    }

    // 확장이 보낸 이미지 바이트를 공개 호스트에 올려 캐시 (재호스팅 필요 이미지)
    //  완료 시, 지금 그 이미지를 표시 중이면 즉시 갱신한다.
    //  ⚠ 호스트 전체가 일시 장애일 수 있으므로(catbox 중단 사례) 지수 백오프 재시도.
    //    확장은 1회만 보내므로 여기서 포기하면 그 화면은 영영 로고로 남는다.
    async cacheThumbnail(url, dataUrl, attempt = 0) {
        if (!url) return;
        if (attempt === 0) this.rememberThumbBytes(url, dataUrl);  // 업로드와 무관하게 바이트 확보
        // 재시도 호출은 이미 pending을 점유한 상태이므로 중복 검사에서 제외
        if (attempt === 0 && (this._cachedThumb(url) || this._thumbPending.has(url))) return;
        this._thumbPending.add(url);   // 재시도 대기 동안에도 유지 → 중복 업로드 방지

        let hosted = '';
        try {
            hosted = await uploadImage(dataUrl);
        } catch (e) {
            log.warn(`[RPC] 썸네일 재호스팅 실패(${attempt + 1}/4):`, e.message);
            if (attempt < 3) {
                const wait = 15000 * Math.pow(2, attempt);   // 15s → 30s → 60s
                setTimeout(() => this.cacheThumbnail(url, dataUrl, attempt + 1), wait);
            } else {
                this._thumbPending.delete(url);              // 최종 포기 → 로고 유지
            }
            return;
        }

        this._thumbPending.delete(url);
        // 캐시 상한 (오래된 것부터 제거 — Map은 삽입 순서 유지)
        if (this._thumbCache.size >= 60) {
            this._thumbCache.delete(this._thumbCache.keys().next().value);
        }
        this._thumbCache.set(url, { url: hosted, at: Date.now() });
        log.info('[RPC] 썸네일 재호스팅 OK →', hosted);
        // 프레즌스가 표시 중일 때만 갱신 트리거 (클리어 직후 부활 방지)
        //  즉시 경로(_forceUpdate) — 업로드 끝나는 순간 로고→썸네일 교체
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

    _scheduleReconnect() {
        if (this.reconnectTimer) return;
        this.reconnectTimer = setTimeout(() => { this.reconnectTimer = null; this.connect(); }, 10000);
    }

    // ══════════════════════════════════════
    //  From bridge
    // ══════════════════════════════════════

    updateFromVideoState(data) {
        if (!data) return;

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
            const key = [this.currentState.site, this.currentState.pageTitle,
                         this.currentState.pageDetail, this.currentState.pageBanner,
                         this.currentState.pageUrl].join('|');
            if (key === this._lastBrowsingKey) return;
            this._lastBrowsingKey = key;

            // 페이지 이동은 즉시 반영 (_forceUpdate에 연타 가드 있음)
            this._forceUpdate('browsing');
        }
    }

    // ══════════════════════════════════════
    //  Queue System
    // ══════════════════════════════════════

    _queueUpdate(type) {
        const now = Date.now();
        const elapsed = now - this._lastApiCall;

        if (elapsed >= MIN_UPDATE_INTERVAL) {
            this._flushUpdate(type);
            return;
        }

        this._pendingUpdate = type;

        if (!this._pendingTimer) {
            const waitTime = MIN_UPDATE_INTERVAL - elapsed + 100;
            this._pendingTimer = setTimeout(() => {
                this._pendingTimer = null;
                if (this._pendingUpdate) {
                    const t = this._pendingUpdate;
                    this._pendingUpdate = null;
                    this._flushUpdate(t);
                }
            }, waitTime);
        }
    }

    _forceUpdate(type) {
        if (this._pendingTimer) {
            clearTimeout(this._pendingTimer);
            this._pendingTimer = null;
        }
        this._pendingUpdate = null;

        // 연타 보호: 직전 API 호출 후 500ms 미만이면 그 차이만큼만 대기
        // (일반 큐의 2초 대신 0.5초 — "즉시"에 가깝게, rate limit은 안 넘게)
        const elapsed = Date.now() - this._lastApiCall;
        const MIN_FORCE_GAP = 500;
        if (elapsed >= MIN_FORCE_GAP) {
            this._flushUpdate(type);
        } else {
            this._pendingUpdate = type;
            this._pendingTimer = setTimeout(() => {
                this._pendingTimer = null;
                const t = this._pendingUpdate;
                this._pendingUpdate = null;
                if (t) this._flushUpdate(t);
            }, MIN_FORCE_GAP - elapsed);
        }
    }

    _flushUpdate(type) {
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
        if (this._idleTimer) clearTimeout(this._idleTimer);
        this._idleTimer = setTimeout(() => {
            log.info('[RPC] 5분 비활동 → Activity 제거');
            this._stopRefreshTimer();
            try { if (this.client) this.client.clearActivity(); } catch (e) {}
            this.lastActivity = null;
            // _lastBrowsingKey는 유지 — 동일 페이지 하트비트로 되살아나지 않게
            this._notifyStatus();
        }, this._IDLE_TIMEOUT);
    }

    clearActivity() {
        if (!this.connected || !this.client) return;
        this._stopRefreshTimer();
        if (this._idleTimer) { clearTimeout(this._idleTimer); this._idleTimer = null; }
        // 죽은 프레즌스 부활 방지: 대기 중인 큐 발화·비동기 재호스팅 완료가
        // clear 이후 stale activity를 재전송하지 못하게 상태까지 리셋
        if (this._pendingTimer) { clearTimeout(this._pendingTimer); this._pendingTimer = null; }
        this._pendingUpdate = null;
        this.currentState.isWatching = false;
        try { this.client.clearActivity(); } catch (e) {}
        this.lastActivity = null;
        this._lastBrowsingKey = '';
        this._notifyStatus();
    }
}

module.exports = DiscordRichPresence;
