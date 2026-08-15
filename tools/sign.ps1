<#
  코드 서명
  ---------
  Windows 11 의 Smart App Control(SAC)은 **서명이 없는 실행 파일을 무조건 차단**한다.
  메타데이터를 아무리 채워도, 평판이 쌓여도 통과하지 못한다.
  Microsoft 신뢰 루트 프로그램에 있는 CA 가 발급한 RSA 인증서로 서명해야만 열린다
  (ECC 는 SAC 가 아직 지원하지 않는다).

  인증서가 없으면 이 스크립트는 아무 일도 하지 않고 조용히 넘어간다.
  → 인증서를 마련하는 날, 환경 변수만 채우면 빌드가 그대로 서명본을 낸다.

  쓰는 법 (셋 중 하나만 설정)
  ---------------------------
   ① 로컬 인증서 저장소에 있는 인증서
        $env:TOKU_SIGN_THUMBPRINT = "AB12...."      (인증서 지문)

   ② PFX 파일
        $env:TOKU_SIGN_PFX      = "C:\path\cert.pfx"
        $env:TOKU_SIGN_PFX_PASS = "..."             (없으면 물어본다)

   ③ Azure Artifact Signing (구 Trusted Signing)
        $env:TOKU_SIGN_AZURE_METADATA = "C:\path\metadata.json"
        (Azure.CodeSigning.Dlib 설치 필요 — signtool 의 /dlib 경로를 함께 지정)
        $env:TOKU_SIGN_AZURE_DLIB     = "C:\path\Azure.CodeSigning.Dlib.dll"

  타임스탬프는 항상 넣는다. 없으면 인증서 만료와 동시에 서명이 무효가 된다.
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Path,
    [string]$Description = 'TOKU RPC'
)

$ErrorActionPreference = 'Stop'
$TIMESTAMP = 'http://timestamp.digicert.com'

function Find-SignTool {
    $cands = @()
    foreach ($root in @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "$env:ProgramFiles\Windows Kits\10\bin")) {
        if (Test-Path $root) {
            $cands += Get-ChildItem $root -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
                      Where-Object { $_.FullName -match '\\x64\\' } |
                      Sort-Object FullName -Descending
        }
    }
    if ($cands.Count) { return $cands[0].FullName }
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# ── 어떤 방식으로 서명할지 결정 ──
$mode = $null
if ($env:TOKU_SIGN_THUMBPRINT)     { $mode = 'thumbprint' }
elseif ($env:TOKU_SIGN_PFX)        { $mode = 'pfx' }
elseif ($env:TOKU_SIGN_AZURE_METADATA) { $mode = 'azure' }

if (-not $mode) {
    Write-Host "  서명 안 함 — 인증서가 설정되지 않았습니다" -ForegroundColor DarkYellow
    Write-Host "    Windows 11 Smart App Control 이 켜진 PC 에서는 실행이 차단됩니다." -ForegroundColor DarkGray
    Write-Host "    설정 방법은 tools\sign.ps1 상단 주석 참고." -ForegroundColor DarkGray
    exit 0
}

$signtool = Find-SignTool
if (-not $signtool) {
    Write-Host "  ★ signtool.exe 를 찾지 못했습니다 (Windows SDK 필요) — 서명을 건너뜁니다" -ForegroundColor Red
    exit 0
}

$files = @()
foreach ($p in $Path) { if (Test-Path $p) { $files += (Get-Item $p).FullName } }
if (-not $files.Count) { Write-Host "  서명할 파일이 없습니다" -ForegroundColor DarkYellow; exit 0 }

# fd sha256 = 파일 다이제스트, td sha256 = 타임스탬프 다이제스트. 둘 다 SHA-256 이어야 한다.
$sa = @('sign', '/fd', 'sha256', '/td', 'sha256', '/tr', $TIMESTAMP, '/d', $Description)
switch ($mode) {
    'thumbprint' { $sa += @('/sha1', $env:TOKU_SIGN_THUMBPRINT) }
    'pfx' {
        $sa += @('/f', $env:TOKU_SIGN_PFX)
        if ($env:TOKU_SIGN_PFX_PASS) { $sa += @('/p', $env:TOKU_SIGN_PFX_PASS) }
    }
    'azure' {
        if (-not $env:TOKU_SIGN_AZURE_DLIB) { throw "TOKU_SIGN_AZURE_DLIB 도 설정해야 합니다" }
        $sa += @('/v', '/dlib', $env:TOKU_SIGN_AZURE_DLIB, '/dmdf', $env:TOKU_SIGN_AZURE_METADATA)
    }
}

Write-Host "  서명 중 ($mode) — $($files.Count)개 파일" -ForegroundColor Cyan
& $signtool ($sa + $files)
if ($LASTEXITCODE -ne 0) { throw "서명 실패 (코드 $LASTEXITCODE)" }

# 실제로 유효한 서명이 붙었는지 확인한다 — 조용히 실패하면 SAC 에서 그대로 막힌다
foreach ($f in $files) {
    $sig = Get-AuthenticodeSignature $f
    $ok = ($sig.Status -eq 'Valid')
    Write-Host ("    {0} {1}  [{2}]" -f $(if ($ok) { 'OK ' } else { '★  ' }),
                [IO.Path]::GetFileName($f), $sig.Status) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
