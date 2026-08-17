// ============================================================
//  TOKU RPC — 정주행 모드 웹훅 로거
//
//  확장이 VIDEO 상태에 binge:true 를 실어 보내면, 에피소드가 바뀔 때마다
//  Discord 웹훅으로 "시청 시작" 기록을 남긴다. (일시정지/재개는 에피소드
//  키가 그대로라 중복 전송되지 않음 — 정주행은 오직 ended로만 진행)
//  마지막 화 완주 시 확장이 BINGE_END 를 보내면 "정주행 끝" 기록.
//
//  썸네일: TTFC는 cloudfront 원본, imagination은 재호스팅(catbox) URL을
//  discordRPC._resolveImage 로 얻는다 (재호스팅 시간 확보를 위해 6초 지연 전송).
// ============================================================

const log = require('electron-log');
const secrets = require('./secrets');
const i18n = require('./i18n');

// ── 웹훅 주소는 저장소·배포본에 넣지 않는다 ──
//  공개되면 누구나 이 채널에 글을 쓸 수 있다(도배·스팸 위험).
//  실제 값은 개인 설정 파일에서 읽는다 (src/secrets.js 참고).
//  값이 없으면 정주행 기록만 꺼지고 나머지 기능은 그대로 동작한다.
const WEBHOOK_URL = secrets.webhookUrl();
if (!WEBHOOK_URL) log.info('[Binge] 웹훅 주소 미설정 — 정주행 기록 비활성화 (' + secrets.configPath() + ')');

const SITE_NAMES = {
    ttfc: '東映特撮ファンクラブ',
    imagination: 'TSUBURAYA IMAGINATION',
};

// "2026 07/20 20:15" 형식 (유빈 예시 포맷)
function fmtNow() {
    const d = new Date();
    const p = (n) => String(n).padStart(2, '0');
    return `${d.getFullYear()} ${p(d.getMonth() + 1)}/${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

const RELOG_COOLDOWN = 10 * 60 * 1000;  // 같은 화 재기록 금지 시간
const SETTLE_DELAY = 6000;              // 안정화 대기(썸네일 재호스팅 시간 겸용)

class BingeLogger {
    constructor(discordRPC) {
        this.rpc = discordRPC;
        this._lastKey = '';
        this._curKey = '';       // 가장 최근에 받은 상태의 키
        this._curKeySince = 0;   // 그 키가 "연속으로" 유지된 시작 시각
        this._curState = null;
        this._logged = new Map();  // key → 기록 시각
        this._lastSeries = '';
    }

    // 웹훅 문구는 앱 언어를 따른다 (RPC 와 같은 언어를 쓴다)
    _t(key, vars) { return i18n.t((this.rpc && this.rpc.lang) || 'ko', key, vars); }

    // extension-bridge가 VIDEO 상태마다 호출
    //  ⚠ 여러 탭(TTFC+IMAGINATION)이 동시에 열려 있으면 상태가 번갈아 도착한다.
    //    그때마다 "새 에피소드"로 보고 웹훅을 쏘면 채널이 폭발한다(실제 발생).
    //    → 6초 뒤에도 여전히 같은 에피소드일 때만 기록하고, 같은 화는 쿨다운을 둔다.
    onVideoState(s) {
        // 정주행이 아닌 상태(다른 탭/사이트)는 그냥 무시한다.
        //  ⚠ 예전엔 여기서 _lastKey/_curKey를 초기화했는데, 그러면 정주행 아닌 상태가
        //    하나만 끼어들어도 같은 화가 "새 에피소드"로 되살아나 매초 재감지되고,
        //    안정화 검사가 영원히 실패해 웹훅이 아예 안 나갔다(실측 확인).
        if (!s.binge) return;
        if (!s.seriesName) return;
        const key = [s.site, s.seriesName, s.episodeNumber, s.episodeTitle].join('|');
        if (key !== this._curKey) { this._curKey = key; this._curKeySince = Date.now(); }
        this._curState = { ...s };
        if (key === this._lastKey) return;  // 같은 에피소드(일시정지/재개 포함) → 무시

        const prev = this._logged.get(key) || 0;
        if (Date.now() - prev < RELOG_COOLDOWN) { this._lastKey = key; return; }
        this._lastKey = key;
        this._lastSeries = s.seriesName;

        log.info(`[Binge] 새 에피소드 감지: ${s.seriesName} ${s.episodeNumber || ''} — ${SETTLE_DELAY / 1000}초 후 확인`);
        setTimeout(() => {
            // 그 사이 다른 탭/화로 바뀌었으면 기록하지 않는다.
            //  ※ "지금 일치"만 보면 두 탭이 번갈아 올 때 우연히 일치해 통과한다(실측).
            //    → 대기 시간 내내 끊기지 않고 유지됐는지까지 확인한다.
            if (this._curKey !== key || Date.now() - this._curKeySince < SETTLE_DELAY - 500) {
                log.info('[Binge] 에피소드가 안정되지 않아 기록 생략:', key.slice(0, 40));
                return;
            }
            if (this._logged.size > 200) this._logged.clear();
            this._logged.set(key, Date.now());
            this._sendStart(this._curState || s);
        }, SETTLE_DELAY);
    }

    // 썸네일 준비 대기 — 확장이 바이트를 보내고 앱이 재호스팅할 시간을 준다.
    //  (고정 6초로는 imagination 재호스팅이 끝나기 전에 발사돼 이미지가 비었다)
    async _waitThumb(rawUrl, maxMs = 20000) {
        if (!rawUrl || !this.rpc) return { bytes: null, url: '' };
        const t0 = Date.now();
        for (;;) {
            const bytes = this.rpc.getThumbBytes ? this.rpc.getThumbBytes(rawUrl) : null;
            const url = this.rpc._resolveImage(rawUrl, '');
            if (bytes || /^https?:\/\//.test(url)) return { bytes, url };
            if (Date.now() - t0 >= maxMs) return { bytes: null, url: '' };
            await new Promise(r => setTimeout(r, 1000));
        }
    }

    async _sendStart(s) {
        const epLabel = [s.episodeNumber, s.episodeTitle].filter(Boolean).join(' / ') || this._t('hook.episode');
        const siteName = SITE_NAMES[s.site] || s.site;
        const { bytes, url: hosted } = await this._waitThumb(s.thumbnail);

        const embed = {
            title: s.seriesName,                                     // 큰 타이틀: 시리즈명
            description: s.url ? `[${epLabel}](${s.url})` : epLabel, // 작은 타이틀: 에피소드 하이퍼링크
            color: s.site === 'imagination' ? 0xd32f5a : 0x2ea6ff,
            footer: { text: `${siteName} · ${this._t('hook.bingeMode')}` },
        };
        const content = this._t('hook.start', { time: fmtNow(), ep: epLabel });

        // 1순위: 이미지를 디스코드에 직접 첨부 (외부 호스트 만료와 무관하게 영구 보존)
        if (bytes && bytes.buf) {
            // 파일명 확장자를 실제 형식에 맞춘다 — PNG 를 .jpg 로 붙이면 클라이언트에
            // 따라 미리보기가 깨질 수 있다 (원본은 사이트에 따라 png/webp 로 온다)
            const ext = /png/i.test(bytes.type || '') ? 'png'
                      : /webp/i.test(bytes.type || '') ? 'webp' : 'jpg';
            const fname = 'thumb.' + ext;
            embed.image = { url: 'attachment://' + fname };
            const fd = new FormData();
            fd.append('payload_json', JSON.stringify({ content, embeds: [embed] }));
            fd.append('files[0]', new Blob([bytes.buf], { type: bytes.type || 'image/jpeg' }), fname);
            return this._postForm(fd);
        }
        // 2순위: 재호스팅/원본 URL (TTFC cloudfront 등)
        if (/^https?:\/\//.test(hosted)) embed.image = { url: hosted };
        return this._post({ content, embeds: [embed] });
    }

    // 마지막 화 완주 (확장 BINGE_END)
    end(msg) {
        const name = (msg && msg.seriesName) || this._lastSeries || '';
        this._lastKey = '';
        return this._post({
            content: this._t('hook.end', { time: fmtNow(), name }),
            embeds: [{
                title: this._t('hook.endTitle', { name }),
                description: this._t('hook.endDesc'),
                color: 0xffc233,
                footer: { text: `${SITE_NAMES[(msg && msg.site)] || ''} · ${fmtNow()}` },
            }],
        });
    }

    async _post(payload) {
        if (!WEBHOOK_URL) return;
        try {
            const r = await fetch(WEBHOOK_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
            });
            if (!r.ok) throw new Error('HTTP ' + r.status);
            log.info('[Binge] 웹훅 전송 OK (URL 이미지)');
        } catch (e) {
            log.warn('[Binge] 웹훅 실패:', e.message);
        }
    }

    // 이미지 파일을 함께 올리는 멀티파트 전송
    async _postForm(fd) {
        if (!WEBHOOK_URL) return;
        try {
            const r = await fetch(WEBHOOK_URL, { method: 'POST', body: fd });
            if (!r.ok) throw new Error('HTTP ' + r.status + ' ' + (await r.text()).slice(0, 80));
            log.info('[Binge] 웹훅 전송 OK (이미지 첨부)');
        } catch (e) {
            log.warn('[Binge] 웹훅(첨부) 실패:', e.message);
        }
    }
}

module.exports = BingeLogger;
