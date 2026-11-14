// 앱 이름표 — exe 하나에 안정(베타)·테스트(알파) 두 모드 (2026-11-08 유빈 제안)
//  · 모드는 설정 testChannel (앱 설정 탭 '테스트 버전 (알파)') — 바꾸면 앱이 스스로 다시 시작해 그 모드로 뜬다
//  · 베타: 전송은 지금 앱과 같은 방식(교차 검증 대상) · 알파: 새 전송 방식(V2, core_set_pacing_v2)을 먼저 써 본다
//  · 설치·자동 시작·하나만 실행은 모드와 상관없이 하나 (아래 이름)
#ifndef TOKU_CHANNEL_H
#define TOKU_CHANNEL_H

#define CH_VERSION_BETA "0.1.1"
#define CH_VERSION_ALPHA "0.2.1"
#define CH_PROG_DIR L"TOKU RPC Beta"
#define CH_EXE L"TOKU-RPC-beta.exe"
#define CH_MUTEX L"Local\\TOKU-RPC-native-beta"
#define CH_CLASS L"TokuRpcNativeBeta"
#define CH_RUN_NAME L"TOKU RPC Beta"
#define CH_TASK_NAME L"TOKU RPC Beta Fast Start"
#define CH_RUN_NAME_A "TOKU RPC Beta"
#define CH_TASK_NAME_A "TOKU RPC Beta Fast Start"

//  예전 알파 설치본(11/8 전·따로 빌드한 테스트 알파)이 돌고 있으면 정상 종료를 부탁하고 넘겨받는다 (7690·Discord 를 같이 못 씀)
#define CH_OTHER_CLASS L"TokuRpcNativeAlpha"
//  예전 알파가 등록한 부팅 항목 — 시작할 때 지우고 위 이름으로 다시 등록한다
#define CH_LEGACY_RUN_NAME L"TOKU RPC Alpha"
#define CH_LEGACY_TASK_NAME L"TOKU RPC Alpha Fast Start"

#endif
