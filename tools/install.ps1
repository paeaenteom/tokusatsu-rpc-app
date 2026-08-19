<#
  TOKU RPC 자동 설치
  ------------------
  GitHub 최신 릴리스에서 앱과 크롬 확장을 받아 한 번에 설치한다.

  하는 일
   1. 최신 릴리스 조회 (paeaenteom/tokusatsu-rpc-app)
   2. 앱 설치 프로그램 내려받아 자동 설치 (무인)
   3. 크롬 확장을 Chrome 정책(HKCU)에 등록 → 크롬 재시작 시 자동 설치
      · 관리자 권한이 필요 없다 (사용자 레지스트리만 사용)
      · 확장 ID가 고정돼 있어 갱신도 자동으로 따라간다
   4. 앱 실행

  수동 설치용 확장 폴더도 함께 풀어둔다(자동 등록이 막힌 환경 대비).
#>

$ErrorActionPreference = 'Stop'
$REPO    = 'paeaenteom/tokusatsu-rpc-app'
$EXT_ID  = 'dciaobllfdcegjcdmimclgglapnhggjm'
$UPDATE_URL = "https://github.com/$REPO/releases/latest/download/update.xml"
$WORK    = Join-Path $env:LOCALAPPDATA 'TOKU RPC'
$EXT_DIR = Join-Path $WORK 'extension'

function Say([string]$m, [string]$c = 'Gray') { Write-Host $m -ForegroundColor $c }
function Step([string]$m) { Write-Host ""; Write-Host "▶ $m" -ForegroundColor Cyan }

Write-Host ""
Write-Host "  TOKU RPC 설치" -ForegroundColor White
Write-Host "  ────────────────────────────" -ForegroundColor DarkGray

# ── 1. 최신 릴리스 조회 ──────────────────────────────
Step "최신 버전 확인 중..."
try {
    $rel = Invoke-RestMethod "https://api.github.com/repos/$REPO/releases/latest" -Headers @{ 'User-Agent' = 'toku-rpc-installer' }
} catch {
    Say "  릴리스를 불러오지 못했습니다: $($_.Exception.Message)" 'Red'
    Read-Host "`n엔터를 누르면 종료"; exit 1
}
Say "  최신 버전: $($rel.tag_name)" 'Green'

function Get-Asset([string]$pattern) {
    $rel.assets | Where-Object { $_.name -like $pattern } | Select-Object -First 1
}

New-Item -ItemType Directory -Force $WORK | Out-Null
$tmp = Join-Path $env:TEMP ("tokurpc-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $tmp | Out-Null

# ── 2. 앱 설치 ──────────────────────────────────────
Step "앱 설치 중..."
$APP_DIR = Join-Path $env:LOCALAPPDATA 'Programs\TOKU RPC'
$appExe  = Join-Path $APP_DIR 'TOKU RPC.exe'

# 실행 중이면 먼저 종료 (파일 잠금 방지)
Get-Process -Name 'TOKU RPC' -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

$setupAsset = Get-Asset 'TOKU-RPC-Setup-*.exe'
$zipAsset   = Get-Asset 'TOKU-RPC-*-win.zip'

if ($setupAsset) {
    $exe = Join-Path $tmp $setupAsset.name
    Say "  내려받는 중: $($setupAsset.name) ($([math]::Round($setupAsset.size/1MB,1))MB)"
    Invoke-WebRequest $setupAsset.browser_download_url -OutFile $exe -UseBasicParsing
    Say "  설치 중... (잠시 걸립니다)"
    $p = Start-Process $exe -ArgumentList '/S' -Wait -PassThru
    if ($p.ExitCode -eq 0) { Say "  앱 설치 완료" 'Green' }
    else { Say "  설치 프로그램이 코드 $($p.ExitCode) 로 종료됨" 'Yellow' }
}
elseif ($zipAsset) {
    $zip = Join-Path $tmp $zipAsset.name
    Say "  내려받는 중: $($zipAsset.name) ($([math]::Round($zipAsset.size/1MB,1))MB)"
    Invoke-WebRequest $zipAsset.browser_download_url -OutFile $zip -UseBasicParsing
    Say "  압축 푸는 중..."
    New-Item -ItemType Directory -Force $APP_DIR | Out-Null
    Expand-Archive $zip -DestinationPath $APP_DIR -Force
    # 시작 메뉴 바로가기
    try {
        $lnk = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\TOKU RPC.lnk'
        $sh = New-Object -ComObject WScript.Shell
        $s = $sh.CreateShortcut($lnk)
        $s.TargetPath = $appExe
        $s.WorkingDirectory = $APP_DIR
        $s.Description = 'TOKU RPC'
        $s.Save()
        Say "  시작 메뉴에 등록됨" 'DarkGray'
    } catch { }
    Say "  앱 설치 완료 ($APP_DIR)" 'Green'
}
else {
    Say "  앱 파일을 릴리스에서 찾지 못했습니다 (건너뜀)" 'Yellow'
}

# ── 3. 크롬 확장 ────────────────────────────────────
Step "크롬 확장 설치 중..."
$extAsset = Get-Asset '*extension*.zip'
if ($extAsset) {
    $zip = Join-Path $tmp $extAsset.name
    Invoke-WebRequest $extAsset.browser_download_url -OutFile $zip -UseBasicParsing
    Remove-Item $EXT_DIR -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive $zip -DestinationPath $EXT_DIR -Force
    Say "  확장 파일 준비: $EXT_DIR"
}

# Chrome 정책으로 자동 설치 등록 (사용자 레지스트리 — 관리자 권한 불필요)
$ok = $false
try {
    foreach ($vendor in @('Google\Chrome', 'Microsoft\Edge')) {
        $base = "HKCU:\SOFTWARE\Policies\$vendor\ExtensionSettings\$EXT_ID"
        New-Item -Path $base -Force | Out-Null
        New-ItemProperty -Path $base -Name 'installation_mode' -Value 'normal_installed' -PropertyType String -Force | Out-Null
        New-ItemProperty -Path $base -Name 'update_url' -Value $UPDATE_URL -PropertyType String -Force | Out-Null
    }
    $ok = $true
} catch {
    Say "  정책 등록 실패: $($_.Exception.Message)" 'Yellow'
}

if ($ok) {
    Say "  확장 자동 설치 등록 완료" 'Green'
    Say "  → 크롬을 완전히 껐다 켜면 자동으로 설치됩니다" 'DarkGray'
} else {
    Say "  자동 등록에 실패했습니다. 아래 방법으로 직접 추가해주세요:" 'Yellow'
    Say "   1) chrome://extensions 열기  2) 개발자 모드 켜기" 'DarkGray'
    Say "   3) '압축해제된 확장 프로그램 로드' → $EXT_DIR" 'DarkGray'
}

# ── 4. 앱 실행 ──────────────────────────────────────
Step "앱 실행"
if (Test-Path $appExe) {
    Start-Process $appExe
    Say "  실행됨 (트레이 아이콘 확인)" 'Green'
} else {
    Say "  실행 파일을 찾지 못했습니다: $appExe" 'Yellow'
}

Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "  ────────────────────────────" -ForegroundColor DarkGray
Write-Host "  설치 완료" -ForegroundColor White
Write-Host ""
Say "  남은 단계: 크롬을 완전히 종료했다 다시 실행하세요."
Say "  (트레이의 크롬 아이콘까지 닫아야 정책이 적용됩니다)"
Write-Host ""
Read-Host "엔터를 누르면 창을 닫습니다" | Out-Null
