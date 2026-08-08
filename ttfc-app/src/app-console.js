// ============================================================
//  TOKU RPC — App Console (미니 창 콘솔용 로그 버퍼)
//
//  electron-log 훅으로 앱 전체 로그(info/warn/error)를 가로채고,
//  uncaughtException / unhandledRejection 까지 수집해서
//  미니 창 콘솔 패널에 실시간 표시한다.
// ============================================================

const log = require('electron-log');

class AppConsole {
    constructor(maxLines = 600) {
        this.lines = [];
        this.max = maxLines;
        this.onLine = null; // (line) => void — 미니 창 push용
        this._hooked = false;
    }

    push(level, msg) {
        const line = {
            t: Date.now(),
            level: level || 'info',
            msg: String(msg).slice(0, 500),
        };
        this.lines.push(line);
        if (this.lines.length > this.max) this.lines.shift();
        if (typeof this.onLine === 'function') {
            try { this.onLine(line); } catch (e) {}
        }
        return line;
    }

    info(msg) { return this.push('info', msg); }
    warn(msg) { return this.push('warn', msg); }
    error(msg) { return this.push('error', msg); }

    getAll() { return this.lines; }
    clear() { this.lines = []; }

    // electron-log 전체 + 프로세스 에러 후킹
    hookAll() {
        if (this._hooked) return;
        this._hooked = true;

        const self = this;
        log.hooks.push((message) => {
            try {
                const text = (message.data || [])
                    .map(d => {
                        if (typeof d === 'string') return d;
                        if (d instanceof Error) return d.stack || d.message;
                        try { return JSON.stringify(d); } catch (e) { return String(d); }
                    })
                    .join(' ');
                self.push(message.level || 'info', text);
            } catch (e) { /* 콘솔 수집 실패는 무시 */ }
            return message;
        });

        // log.error는 위 훅을 거치므로 콘솔 패널 + 파일 로그에 모두 남는다
        process.on('uncaughtException', (err) => {
            log.error('[uncaughtException]', err && (err.stack || err.message || err));
        });
        process.on('unhandledRejection', (reason) => {
            log.error('[unhandledRejection]', reason && (reason.stack || reason.message || reason));
        });
    }
}

module.exports = AppConsole;
