// ============================================================================
// json_safety.h — JSON 문자열 안전 처리 유틸
// ============================================================================
// 목적:
//   JSON 응답 생성 시 사용자 입력/DB 데이터를 안전하게 이스케이프한다.
//   JSON injection 공격과 파싱 오류를 방지한다.
//
// 처리 대상:
//   - 쌍따옴표     -> 백슬래시 + "
//   - 역슬래시     -> 백슬래시 2개
//   - 개행/CR/탭   -> \n / \r / \t
//   - 백스페이스/폼피드 -> \b / \f
//   - 제어문자(0x00~0x1F) -> \uXXXX
//
// 사용 원칙:
//   - 모든 JSON 응답의 문자열 필드에 반드시 적용
//   - 서버 ↔ 클라이언트, 서버 ↔ AI서버 전송 JSON 동일하게 적용
// ============================================================================
#pragma once

#include <string>

namespace factory::security {

/// JSON 문자열 이스케이프 — injection 방지 + 파싱 안전성 보장
/// @param s 원본 문자열 (DB 데이터, 사용자 입력 등)
/// @return JSON에 삽입해도 안전한 이스케이프된 문자열
inline std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);  // 이스케이프 여유 예약

    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                // 제어문자(0x00 ~ 0x1F)는 \uXXXX 형식으로 이스케이프
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    // UTF-8 멀티바이트(0x80 이상) 포함 일반 문자
                    out += c;
                }
                break;
        }
    }
    return out;
}

} // namespace factory::security
