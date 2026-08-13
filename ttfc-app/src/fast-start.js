// ============================================================
//  빠른 시작 (Windows)
//
//  Run 레지스트리로 부팅 자동 시작을 하면 Windows 가 일부러 늦게 띄운다.
//  탐색기가 바탕화면을 그린 뒤 10초쯤 기다렸다가, 등록된 앱들을 하나씩
//  간격을 두고 실행한다. 다른 시작 프로그램이 많으면 한참 뒤에 뜬다.
//
//  작업 스케줄러의 "로그온 시" 트리거는 그 대기열을 타지 않는다.
//  지연 0 으로 등록하면 로그온과 거의 동시에 뜬다.
//  관리자 권한도 필요 없다 (현재 사용자 작업, 실측 확인).
//
//  Run 키와 동시에 켜두면 두 번 실행될 수 있으므로, 빠른 시작을 켜면
//  Run 키는 지운다. 앱은 단일 인스턴스 잠금이 있어 두 번 떠도 하나만
//  살아남지만, 애초에 중복으로 띄우지 않는 편이 낫다.
// ============================================================

const { execFile } = require('child_process');
const log = require('electron-log');

const TASK_NAME = 'TOKU RPC Fast Start';

function runPs(script) {
    return new Promise((resolve) => {
        execFile('powershell.exe',
            ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', script],
            { windowsHide: true, timeout: 20000 },
            (err, stdout, stderr) => {
                resolve({ ok: !err, out: String(stdout || '').trim(), err: String(stderr || err && err.message || '').trim() });
            });
    });
}

// 등록돼 있나
async function isEnabled() {
    if (process.platform !== 'win32') return false;
    const r = await runPs(
        `if (Get-ScheduledTask -TaskName '${TASK_NAME}' -ErrorAction SilentlyContinue) { 'yes' } else { 'no' }`
    );
    return r.out === 'yes';
}

// exePath 로 작업을 등록한다 (이미 있으면 덮어쓴다 — 설치 경로가 바뀌었을 수 있다)
async function enable(exePath) {
    if (process.platform !== 'win32') return false;
    // 경로에 작은따옴표가 들어갈 일은 없지만, PowerShell 문자열로 넣으므로 이스케이프한다
    const p = String(exePath).replace(/'/g, "''");
    const script = [
        `$a = New-ScheduledTaskAction -Execute '${p}' -Argument '--hidden'`,
        `$t = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\\$env:USERNAME"`,
        // 지연 없음 · 배터리에서도 실행 · 시간 제한 없음 · 놓쳤으면 나중에라도
        `$s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries ` +
        `-ExecutionTimeLimit ([TimeSpan]::Zero) -StartWhenAvailable -MultipleInstances IgnoreNew`,
        // 우선순위 4 (기본 7보다 높게). 0~1 은 실시간급이라 과하다.
        `$s.Priority = 4`,
        `$pr = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\\$env:USERNAME" -LogonType Interactive -RunLevel Limited`,
        `$task = New-ScheduledTask -Action $a -Trigger $t -Settings $s -Principal $pr -Description 'TOKU RPC 를 로그온 직후 바로 띄운다'`,
        `Register-ScheduledTask -TaskName '${TASK_NAME}' -InputObject $task -Force | Out-Null`,
        `'ok'`,
    ].join('; ');
    const r = await runPs(script);
    if (r.ok && r.out.endsWith('ok')) {
        log.info('[FastStart] 작업 등록:', exePath);
        return true;
    }
    log.warn('[FastStart] 등록 실패:', r.err || r.out);
    return false;
}

async function disable() {
    if (process.platform !== 'win32') return false;
    const r = await runPs(
        `Unregister-ScheduledTask -TaskName '${TASK_NAME}' -Confirm:$false -ErrorAction SilentlyContinue; 'ok'`
    );
    log.info('[FastStart] 작업 해제');
    return r.ok;
}

module.exports = { isEnabled, enable, disable, TASK_NAME };
