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
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>

// ── 파일 로거 — 날짜별 로테이션 ──
// logs/YYYY-MM-DD.log 파일에 stdout과 동일한 내용을 tee한다.
// 서버 재시작/크래시 시에도 로그가 보존되어 사후 포렌식 가능.
inline FILE* log_file_get() {
    static FILE* cur_file = nullptr;
    static std::string cur_date;
    static std::mutex mtx;

    std::lock_guard<std::mutex> lock(mtx);

    // 현재 날짜 계산 (로컬타임)
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char date_buf[16];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm);

    // 날짜가 바뀌었으면 파일 교체 (자정 로테이션)
    if (cur_date != date_buf) {
        if (cur_file) std::fclose(cur_file);
        ::mkdir("logs", 0755);  // 디렉터리 생성 (이미 있으면 무시)
        std::string path = std::string("logs/") + date_buf + ".log";
        cur_file = std::fopen(path.c_str(), "a");
        cur_date = date_buf;
    }
    return cur_file;
}

// 로그 1줄을 파일에 타임스탬프 prefix와 함께 기록
inline void log_file_write(const char* prefix, const char* tag, const char* fmt, va_list args) {
    FILE* f = log_file_get();
    if (!f) return;

    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

    std::fprintf(f, "[%s] %s [%-5s] ", ts, prefix, tag);
    std::vfprintf(f, fmt, args);
    std::fprintf(f, "\n");
    std::fflush(f);
}

// ── 공통 출력 ──
inline void log_impl(const char* emoji, const char* tag, const char* fmt, va_list args) {
    // 파일용 복사본 생성 (va_list는 한 번만 사용 가능)
    va_list args_copy;
    va_copy(args_copy, args);

    fprintf(stdout, "%s [%-5s] ", emoji, tag);
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    fflush(stdout);

    log_file_write(emoji, tag, fmt, args_copy);
    va_end(args_copy);
}

inline void log_err_impl(const char* tag, const char* fmt, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);

    fprintf(stderr, "❌ [%-5s] ", tag);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);

    log_file_write("❌", tag, fmt, args_copy);
    va_end(args_copy);
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
