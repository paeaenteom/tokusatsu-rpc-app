// ============================================================
//  TTFC Viewer - Watch History (시청 기록)
//
//  가챠(뽑기)로 뽑힌 작품의 정주행 기록
//  N차 정주행 시작/종료 시간 + 에피소드별 시청 시간 기록
//
//  데이터 구조:
//  {
//    "shows": {
//      "50815": {
//        "name": "仮面ライダーゼッツ",
//        "thumbnail": "...",
//        "runs": [
//          {
//            "run": 1,
//            "startedAt": "2026/03/15 14:30:00",
//            "endedAt": "2026/04/02 22:15:00",  // null이면 진행 중
//            "episodes": [
//              { "title": "Case1「始まる」", "number": "Case1", "watchedAt": "2026/03/15 14:30:00", "thumbnail": "" },
//              ...
//            ]
//          }
//        ]
//      }
//    }
//  }
// ============================================================

const path = require('path');
const fs = require('fs');
const { app } = require('electron');
const log = require('electron-log');

class WatchHistory {
    constructor() {
        this._filePath = path.join(app.getPath('userData'), 'watch-history.json');
        this._data = { shows: {} };
        this._activeShowId = null;   // 현재 정주행 중인 작품 ID
        this._activeRunIndex = -1;   // 현재 run 인덱스
        this._lastRecordedEp = '';   // 중복 기록 방지
        this._load();
    }

    // ══════════════════════════════════
    //  로드 / 저장
    // ══════════════════════════════════

    _load() {
        try {
            if (fs.existsSync(this._filePath)) {
                const raw = fs.readFileSync(this._filePath, 'utf8');
                this._data = JSON.parse(raw);
                if (!this._data.shows) this._data.shows = {};
                log.info(`[WatchHistory] 로드: ${Object.keys(this._data.shows).length}개 작품`);
            }
        } catch (e) {
            log.warn('[WatchHistory] 로드 실패:', e.message);
            this._data = { shows: {} };
        }
    }

    _save() {
        try {
            fs.writeFileSync(this._filePath, JSON.stringify(this._data, null, 2), 'utf8');
        } catch (e) {
            log.warn('[WatchHistory] 저장 실패:', e.message);
        }
    }

    _now() {
        const d = new Date();
        const pad = (n) => String(n).padStart(2, '0');
        return `${d.getFullYear()}/${pad(d.getMonth() + 1)}/${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}.${pad(d.getSeconds())}`;
    }

    // ══════════════════════════════════
    //  정주행 시작 (가챠에서 호출)
    // ══════════════════════════════════

    startRun(showId, showName, thumbnail) {
        if (!showId) return null;

        // 작품 데이터 초기화
        if (!this._data.shows[showId]) {
            this._data.shows[showId] = {
                name: showName || '',
                thumbnail: thumbnail || '',
                runs: []
            };
        }

        const show = this._data.shows[showId];
        // 이름/썸네일 업데이트
        if (showName) show.name = showName;
        if (thumbnail) show.thumbnail = thumbnail;

        // 새 run 생성
        const runNumber = show.runs.length + 1;
        const now = this._now();
        show.runs.push({
            run: runNumber,
            startedAt: now,
            endedAt: null,
            episodes: []
        });

        this._activeShowId = showId;
        this._activeRunIndex = show.runs.length - 1;
        this._lastRecordedEp = '';

        this._save();
        log.info(`[WatchHistory] ★ ${runNumber}차 정주행 시작: ${show.name} (${now})`);

        return { showId, showName: show.name, run: runNumber, startedAt: now };
    }

    // ══════════════════════════════════
    //  에피소드 기록 (영상 재생 감지 시)
    // ══════════════════════════════════

    recordEpisode(showId, episodeTitle, episodeNumber, thumbnail) {
        // 활성 정주행이 없으면 무시
        if (!this._activeShowId) return false;

        // 다른 작품이면 무시 (가챠로 뽑힌 작품만 기록)
        // 단, showId가 비어있으면 activeShowId 기준으로 기록 시도
        const targetId = showId || this._activeShowId;
        if (targetId !== this._activeShowId) return false;

        const show = this._data.shows[targetId];
        if (!show || this._activeRunIndex < 0) return false;

        const run = show.runs[this._activeRunIndex];
        if (!run) return false;

        // 제목으로 중복 체크
        const epKey = `${episodeNumber || ''}:${episodeTitle || ''}`;
        if (!epKey || epKey === ':') return false;
        if (epKey === this._lastRecordedEp) return false;

        // 이미 이 run에 같은 에피소드가 있으면 스킵
        const exists = run.episodes.some(e =>
            e.title === episodeTitle && e.number === (episodeNumber || '')
        );
        if (exists) return false;

        const now = this._now();
        run.episodes.push({
            title: episodeTitle || '',
            number: episodeNumber || '',
            watchedAt: now,
            thumbnail: thumbnail || ''
        });

        // 종료 시간 업데이트 (마지막 에피소드 시간)
        run.endedAt = now;

        this._lastRecordedEp = epKey;
        this._save();

        log.info(`[WatchHistory] 📺 ${show.name} ${run.run}차: ${episodeNumber || ''} ${episodeTitle} (${now})`);
        return true;
    }

    // ══════════════════════════════════
    //  정주행 종료
    // ══════════════════════════════════

    endRun() {
        if (!this._activeShowId) return null;

        const show = this._data.shows[this._activeShowId];
        if (!show) return null;

        const run = show.runs[this._activeRunIndex];
        if (run && !run.endedAt && run.episodes.length > 0) {
            run.endedAt = this._now();
        }

        const result = {
            showId: this._activeShowId,
            showName: show.name,
            run: run ? run.run : 0,
            episodes: run ? run.episodes.length : 0,
            startedAt: run ? run.startedAt : '',
            endedAt: run ? run.endedAt : ''
        };

        log.info(`[WatchHistory] 정주행 종료: ${show.name} ${run ? run.run : '?'}차 (${run ? run.episodes.length : 0}화)`);

        this._activeShowId = null;
        this._activeRunIndex = -1;
        this._lastRecordedEp = '';
        this._save();

        return result;
    }

    // ══════════════════════════════════
    //  조회
    // ══════════════════════════════════

    // 현재 활성 정주행 정보
    getActiveRun() {
        if (!this._activeShowId) return null;
        const show = this._data.shows[this._activeShowId];
        if (!show) return null;
        const run = show.runs[this._activeRunIndex];
        return {
            showId: this._activeShowId,
            showName: show.name,
            run: run ? run.run : 0,
            startedAt: run ? run.startedAt : '',
            episodes: run ? run.episodes.length : 0
        };
    }

    // 전체 데이터
    getData() { return this._data; }

    // 작품별 기록
    getShowHistory(showId) {
        return this._data.shows[showId] || null;
    }

    // 전체 작품 목록 (요약)
    getShowList() {
        return Object.entries(this._data.shows).map(([id, show]) => ({
            id,
            name: show.name,
            thumbnail: show.thumbnail || '',
            totalRuns: show.runs.length,
            totalEpisodes: show.runs.reduce((s, r) => s + r.episodes.length, 0),
            lastRun: show.runs.length > 0 ? show.runs[show.runs.length - 1] : null
        }));
    }

    // GitHub 동기화용 데이터
    getForSync() { return this._data; }

    // GitHub에서 로드
    loadFromSync(data) {
        if (data && data.shows) {
            this._data = data;
            this._save();
            log.info(`[WatchHistory] GitHub에서 로드: ${Object.keys(data.shows).length}개 작품`);
        }
    }
}

module.exports = WatchHistory;
