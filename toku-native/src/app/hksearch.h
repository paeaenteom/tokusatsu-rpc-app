#pragma once
//  웹훅 폴더 검색 (2026-11-13 유빈 요청) — 작품명·캐릭터 이름을 그대로, 또는 한글 발음으로 찾는다
//  예: '히비키' · '響鬼' · 'ひびき' · 'ヒビキ' → 仮面ライダー響鬼 (한자 읽기는 Windows 일본어 읽기 분석기)

//  query 의 띄어쓰기로 나눈 낱말이 모두 hay[] 중 어딘가에 맞으면 1. 빈 검색어 = 1
int hks_match(const char *query, const char *const *hay, int nhay);

//  글자의 히라가나 읽기 (캐시, 해제 X) — 분석기가 없거나 실패하면 NULL
const char *hks_reading(const char *s);
//  시험용: 읽기를 바꿔 끼운다 (NULL = Windows 분석기)
extern const char *(*hks_read_hook)(const char *s);

//  시험·로그용: 비교 열쇠 (발음 키 · 정규화 글) — 돌려준 것은 free
char *hks_debug_key(const char *s);
char *hks_debug_norm(const char *s);
