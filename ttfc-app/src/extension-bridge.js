// ============================================================
//  TOKU RPC — Chrome Extension WebSocket Bridge
//
//  크롬 확장에서 사이트(TTFC / TSUBURAYA IMAGINATION) 시청 정보를
//  받아 Discord RPC로 전달. ws 패키지 사용. Port: 7690
//
//  수신 포맷 (확장 v2.0+):
//   { type:'VIDEO',    site, siteName, seriesName, episodeTitle,
//     episodeNumber, currentTime, duration, thumbnail, isPlaying, url }
//   { type:'BROWSING', site, siteName, page, detail, banner, url }
//   { type:'PING' }  → { type:'PONG' } 응답
//   { type:'CLEAR' }
//
//  구버전 호환: { type:'SET_ACTIVITY', ... } → site:'ttfc' VIDEO로 처리
// ============================================================

const { WebSocketServer } = require('ws');
const log = require('electron-log');
const { canDiscordLoad } = require('./discord-rpc');

const PORT = 7690;

class ExtensionBridge {
    constructor(discordRPC, rpcSettings, bingeLogger) {
        this.discordRPC = discordRPC;
        this.rpcSettings = rpcSettings;
        this.bingeLogger = bingeLogger || null;
        this.wss = null;
        this._started = false;
        this.onConnectionChange = null;  // 미니 창용 콜백
        this.lang = 'ko';                // 확장에 알려줄 앱 언어
        this.booster = false;            // 반응 우선 모드 (확장이 더 자주 살핀다)
        // 로그 중복 제거는 사이트별로 해야 한다. 하나로 두면 두 사이트를 같이 볼 때
        // 키가 번갈아 바뀌어 모든 메시지가 로그를 남긴다(초당 2줄).
        this._videoLogKeys = new Map();
        this._browsingLogKeys = new Map();
    }

    // 앱 언어를 확장에 알린다. 확장 언어 설정이 '시스템 언어'면 이 값을 따라간다.
    setLang(lang) {
        this.lang = lang;
        this._broadcast({ type: 'LANG', lang });
    }

    // 부스터 켜짐/꺼짐만 알린다. 실제 주기 값은 확장이 정한다.
    setBooster(on) {
        this.booster = !!on;
        this._broadcast({ type: 'BOOSTER', on: this.booster });
    }

    _broadcast(obj) {
        if (!this.wss) return;
        const data = JSON.stringify(obj);
        for (const ws of this.wss.clients) {
            try { ws.send(data); } catch (e) { /* 끊긴 클라이언트 */ }
        }
    }

    get clientCount() {
        return this.wss ? this.wss.clients.size : 0;
    }

    _notifyConnection() {
        if (typeof this.onConnectionChange === 'function') {
            try { this.onConnectionChange(this.clientCount); } catch (e) {}
        }
    }

    start() {
        if (this._started) return;
        this._started = true;

        try {
            this.wss = new WebSocketServer({ port: PORT, host: '127.0.0.1', maxPayload: 16 * 1024 * 1024 });

            this.wss.on('listening', () => {
                log.info(`[Bridge] WebSocket 서버 시작: ws://127.0.0.1:${PORT}`);
            });

            this.wss.on('connection', (ws) => {
                log.info(`[Bridge] 크롬 확장 연결됨 (${this.wss.clients.size}개)`);
                this._notifyConnection();

                ws.send(JSON.stringify({ type: 'CONNECTED', lang: this.lang, booster: this.booster }));

                ws.on('message', (data) => {
                    try {
                        const msg = JSON.parse(data.toString());
                        this._handleMessage(msg, ws);
                    } catch (e) {
                        log.warn('[Bridge] 메시지 파싱 에러:', e.message);
                    }
                });

                ws.on('close', () => {
                    // stop() 이후에도 close가 늦게 발화 → this.wss가 null일 수 있음
                    // (실제로 앱 종료 시 uncaughtException 발생했음)
                    if (!this.wss) return;
                    log.info(`[Bridge] 크롬 확장 연결 해제 (${this.wss.clients.size}개)`);
                    this._notifyConnection();
                    if (this.wss.clients.size === 0 && this.discordRPC) {
                        this.discordRPC.clearActivity();
                    }
                });

                ws.on('error', (e) => {
                    log.warn('[Bridge] 클라이언트 에러:', e.message);
                });
            });

            this.wss.on('error', (e) => {
                if (e.code === 'EADDRINUSE') {
                    log.warn(`[Bridge] 포트 ${PORT} 이미 사용 중`);
                } else {
                    log.warn('[Bridge] 서버 에러:', e.message);
                }
            });
        } catch (e) {
            log.warn('[Bridge] 서버 시작 실패:', e.message);
        }
    }

    stop() {
        if (this.wss) {
            for (const client of this.wss.clients) {
                try { client.close(); } catch (e) {}
            }
            this.wss.close();
            this.wss = null;
        }
        this._started = false;
        log.info('[Bridge] WebSocket 서버 종료');
    }

    // 재호스팅이 필요한데 캐시에 없는 이미지 → 확장에 바이트 요청 (pull)
    //  push 단독 구조는 앱 재시작·업로드 실패 시 영영 복구되지 않았다.
    _requestThumbIfNeeded(url, ws) {
        if (!url || !/^https?:/.test(url)) return;
        // 디스코드가 직접 불러올 수 있는 호스트는 바이트를 받을 이유가 없다.
        //  ⚠ 예전엔 여기 cloudfront 만 하드코딩돼 있어서, 디즈니+ 아트워크가
        //    매초 NEED_THUMB 요청을 타고 로그를 도배했다. 판정을 한곳으로 모았다.
        if (canDiscordLoad(url)) return;
        const rpc = this.discordRPC;
        if (!rpc || rpc._cachedThumb(url) || rpc._thumbPending.has(url)) return;
        const now = Date.now();
        this._thumbAsks = this._thumbAsks || new Map();
        if (now - (this._thumbAsks.get(url) || 0) < 20000) return;  // 20초 쿨다운
        this._thumbAsks.set(url, now);
        if (this._thumbAsks.size > 50) this._thumbAsks.clear();
        try { ws.send(JSON.stringify({ type: 'NEED_THUMB', url })); } catch (e) {}
    }

    _handleMessage(msg, ws) {
        // keepalive PING → 즉시 PONG (확장 서비스 워커 수명 연장용)
        if (msg.type === 'PING') {
            try { ws.send(JSON.stringify({ type: 'PONG' })); } catch (e) {}
            return;
        }

        // 정주행 마지막 화 완주 → "정주행 끝" 웹훅
        if (msg.type === 'BINGE_END') {
            log.info('[Bridge] BINGE_END 수신:', msg.seriesName || '');
            if (this.bingeLogger) this.bingeLogger.end(msg);
            return;
        }

        if (!this.discordRPC) return;

        // 썸네일 바이트 → 앱이 catbox 업로드 후 캐시 (imagination 재호스팅)
        if (msg.type === 'THUMB_BYTES') {
            this.discordRPC.cacheThumbnail(msg.url, msg.dataUrl);
            return;
        }

        if (msg.type === 'CLEAR') {
            // 사이트를 지정해 오면 그 사이트만 지운다.
            //  여러 사이트를 동시에 띄우므로, 한 사이트를 닫았다고 나머지까지
            //  내려가면 안 된다. (옛 확장은 site 없이 보내므로 그때는 전부 지운다)
            log.info('[Bridge] CLEAR 수신', msg.site ? '[' + msg.site + ']' : '(전체)');
            this.discordRPC.clearActivity(msg.site || undefined);
            return;
        }

        const settings = this.rpcSettings ? this.rpcSettings() : {};

        // ── 영상 시청 중 ──
        if (msg.type === 'VIDEO' || (msg.type === 'SET_ACTIVITY' && msg.seriesName)) {
            // 로그는 내용이 바뀔 때만 (시간 경과만으로 2초마다 찍히던 스팸 제거)
            const logKey = [msg.site, msg.seriesName, msg.episodeNumber, msg.episodeTitle, msg.isPlaying].join('|');
            if (logKey !== this._videoLogKeys.get(msg.site)) {
                this._videoLogKeys.set(msg.site, logKey);
                log.info(`[Bridge] VIDEO ← ${msg.site || 'ttfc'} | ${msg.seriesName} ${msg.episodeNumber || ''} ${msg.episodeTitle || ''} (${msg.currentTime || 0}/${msg.duration || 0}s ${msg.isPlaying ? '▶' : '⏸'})`);
            }
            this._requestThumbIfNeeded(msg.thumbnail, ws);
            // 정주행 기록 (binge 플래그가 켜진 상태에서 에피소드 변경 시 웹훅)
            if (this.bingeLogger) {
                this.bingeLogger.onVideoState({
                    binge: !!msg.binge,
                    site: msg.site || 'ttfc',
                    seriesName: msg.seriesName || '',
                    episodeTitle: msg.episodeTitle || '',
                    episodeNumber: msg.episodeNumber || '',
                    thumbnail: msg.thumbnail || '',
                    url: msg.url || '',
                });
            }
            this.discordRPC.updateFromVideoState({
                isWatching: true,
                site: msg.site || 'ttfc',
                siteName: msg.siteName || '',
                isPlaying: !!msg.isPlaying,
                seriesName: msg.seriesName || '',
                episodeTitle: msg.episodeTitle || '',
                episodeNumber: msg.episodeNumber || '',
                currentTime: msg.currentTime || 0,
                duration: msg.duration || 0,
                thumbnail: msg.thumbnail || '',
                pageUrl: msg.url || '',
                _settings: settings,
            });
            return;
        }

        // ── 브라우징 중 ──
        if (msg.type === 'BROWSING') {
            const logKey = [msg.site, msg.page, msg.detail, msg.url].join('|');
            if (logKey !== this._browsingLogKeys.get(msg.site)) {
                this._browsingLogKeys.set(msg.site, logKey);
                log.info(`[Bridge] BROWSING ← ${msg.site || 'ttfc'} | ${msg.page}${msg.detail ? ' / ' + msg.detail : ''}`);
            }
            this._requestThumbIfNeeded(msg.banner, ws);
            this.discordRPC.updateFromNavigation({
                site: msg.site || 'ttfc',
                siteName: msg.siteName || '',
                url: msg.url || '',
                title: msg.page || '',
                detail: msg.detail || '',
                banner: msg.banner || '',
                isVideoPage: false,
                _settings: settings,
            });
        }
    }
}

module.exports = ExtensionBridge;
