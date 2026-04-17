#pragma once
// ============================================================================
// logger.h — 상태 중심 + 흐름 인식 로그 유틸리티
// ============================================================================
// 설계 기준:
//   1. TAG = 서버 역할 (MAIN / AI / DB 등)
//   2. 이모지 = 상태 / 행동
//   3. 메시지 = 사람이 읽는 정보
//
// 이모지 규칙:
//   ➕ CONNECT        — 연결 생성
//   ➖ DISCONNECT     — 연결 종료
//   👤 LOGIN          — 로그인
//   📋 REGISTER       — 등록
//   🧠 INFER          — AI 추론
//   📊 RESULT         — 결과 출력
//   🟥 NG / 🟩 OK     — 결과 상태
//   🚀 START          — 시작
//   🔄 ROUTE          — 라우팅
//   🔁 RETRY          — 재시도
//   ➡️ SEND           — 데이터 전송
//   ⬅️ RECEIVE        — 데이터 수신
//   ⚠️ WARN           — 경고
//   ⏱ TIMEOUT        — 타임아웃
//   ❌ FAIL           — 최종 실패만 사용
// ============================================================================

#include <cstdio>
#include <cstdarg>

// ── 공통 출력 ──
inline void log_impl(const char* emoji, const char* tag, const char* fmt, va_list args) {
    fprintf(stdout, "%s [%-5s] ", emoji, tag);
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    fflush(stdout);
}

inline void log_err_impl(const char* tag, const char* fmt, va_list args) {
    fprintf(stderr, "❌ [%-5s] ", tag);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

// ============================================================================
// 역할별 로그 (TAG 고정)
// ============================================================================

// ── MAIN ──
inline void log_main(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🔄", "MAIN", fmt, args);
    va_end(args);
}
inline void log_err_main(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("MAIN", fmt, args);
    va_end(args);
}

// ── AI ──
inline void log_ai(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🧠", "AI", fmt, args);
    va_end(args);
}
inline void log_err_ai(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("AI", fmt, args);
    va_end(args);
}

// ── DB ──
inline void log_db(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("📊", "DB", fmt, args);
    va_end(args);
}
inline void log_err_db(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("DB", fmt, args);
    va_end(args);
}

// ── CLIENT ──
inline void log_clt(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("👤", "CLT", fmt, args);
    va_end(args);
}
inline void log_err_clt(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("CLT", fmt, args);
    va_end(args);
}

// ── TRAIN ──
inline void log_train(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🚀", "TRAIN", fmt, args);
    va_end(args);
}
inline void log_err_train(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("TRAIN", fmt, args);
    va_end(args);
}

// ── IMG ──
inline void log_img(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🟩", "IMG", fmt, args);
    va_end(args);
}
inline void log_err_img(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("IMG", fmt, args);
    va_end(args);
}

// ── PUSH ──
inline void log_push(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("➡️", "PUSH", fmt, args);
    va_end(args);
}
inline void log_err_push(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("PUSH", fmt, args);
    va_end(args);
}

// ── ACK ──
inline void log_ack(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🟩", "ACK", fmt, args);
    va_end(args);
}
inline void log_err_ack(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("ACK", fmt, args);
    va_end(args);
}

// ============================================================================
// 행동 기반 로그 (TAG를 인자로 받음)
// ============================================================================

// 전송 / 수신
inline void log_send(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("➡️", tag, fmt, args);
    va_end(args);
}

inline void log_recv(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("⬅️", tag, fmt, args);
    va_end(args);
}

// 재시도 / 라우팅
inline void log_retry(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🔁", tag, fmt, args);
    va_end(args);
}

inline void log_route(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🔄", "ROUTE", fmt, args);
    va_end(args);
}
inline void log_err_route(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_err_impl("ROUTE", fmt, args);
    va_end(args);
}

// 상태
inline void log_warn(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("⚠️", tag, fmt, args);
    va_end(args);
}

inline void log_timeout(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("⏱", tag, fmt, args);
    va_end(args);
}

// 결과
inline void log_ok(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🟩", tag, fmt, args);
    va_end(args);
}

inline void log_ng(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log_impl("🟥", tag, fmt, args);
    va_end(args);
}
