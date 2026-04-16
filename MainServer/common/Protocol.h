#pragma once
// Protocol.h
// AI 추론 서버 <-> 메인 운영 서버 간 통신 프로토콜 정의
//
// 패킷 구조:
//   [4byte length(JSON size, big-endian)] + [JSON payload] + [Image binary (있을 때만)]
//
// JSON 본문 공통 필드 (요구사항 분석서 "공통 패킷 구조" 기준):
//   - protocol_no    : int      (필수, 메시지 번호)
//   - protocol_version : string (필수)
//   - inspection_id  : string   (검사 결과 계열에서 필수)
//   - request_id     : string   (요청/응답 매칭용, optional)
//   - station_id     : int
//   - timestamp      : string   (ISO8601)
//   - image_size     : int      (NG 이미지 동봉 시)
//
// 외부(MFC ↔ 운용)는 100~199, 내부(운용 ↔ 추론/학습)는 1000~1999.

#include <cstdint>

namespace factory {

constexpr std::size_t HEADER_SIZE      = 4;
constexpr uint16_t    MAIN_SERVER_PORT = 9000;
constexpr const char* FACTORY_PROTOCOL_VERSION = "1.0";

enum class StationId : int {
    INBOUND  = 1,
    ASSEMBLY = 2,
};

// 메시지 번호 enum (정수값 그대로 wire에 실림)
enum class ProtocolNo : int {
    // ===== 외부 100~199 (MFC ↔ 운용) — 추후 구현, 번호만 예약 =====
    LOGIN_REQ              = 100,
    LOGIN_RES              = 101,
    LOGOUT_REQ             = 102,
    LOGOUT_RES             = 103,
    INSPECT_NG_PUSH        = 110,
    INSPECT_NG_ACK_EXT     = 111,
    INSPECT_OK_COUNT_PUSH  = 112,
    INSPECT_HISTORY_REQ    = 114,
    INSPECT_HISTORY_RES    = 115,
    STATS_REQ              = 130,
    STATS_RES              = 131,
    MODEL_LIST_REQ         = 150,
    MODEL_LIST_RES         = 151,
    RETRAIN_REQ            = 152,
    RETRAIN_RES            = 153,
    RETRAIN_PROGRESS_PUSH  = 154,
    MODEL_DEPLOY_NOTIFY    = 156,
    MODEL_DEPLOY_ACK_EXT   = 157,
    SERVER_HEALTH_PUSH     = 170,
    EXT_ACK                = 190,
    EXT_NACK               = 191,
    EXT_ERROR              = 192,

    // ===== 내부 1000~1999 (운용 ↔ 추론/학습) — 본 단계 구현 대상 =====
    STATION1_NG            = 1000,  // 추론#1 → 운용 (ACK 필수)
    STATION1_NG_ACK        = 1001,  // 운용 → 추론#1
    STATION2_NG            = 1002,  // 추론#2 → 운용 (ACK 필수)
    STATION2_NG_ACK        = 1003,  // 운용 → 추론#2
    STATION_OK_COUNT       = 1004,  // 추론 → 운용 (손실 허용)
    INSPECT_META           = 1006,  // 추론 → 운용 (DB 기록용, ACK 미사용)

    MODEL_RELOAD_CMD       = 1010,
    MODEL_RELOAD_RES       = 1011,

    // 학습서버 채널 1100~1199 — 번호만 예약
    TRAIN_START_REQ        = 1100,
    TRAIN_START_RES        = 1101,
    TRAIN_PROGRESS         = 1102,
    TRAIN_COMPLETE         = 1104,
    TRAIN_COMPLETE_ACK     = 1105,
    TRAIN_FAIL             = 1106,
    TRAIN_FAIL_ACK         = 1107,

    // 헬스체크 1200~
    HEALTH_PING            = 1200,
    HEALTH_PONG            = 1201,
    QUEUE_STATUS           = 1210,
    INFERENCE_TIMEOUT      = 1212,

    // Arduino 1300~ (Edge ↔ Arduino, 본 서버 미사용)
    ARDUINO_REJECT         = 1300,
    ARDUINO_ALERT          = 1302,

    // 내부 공통 1900~
    INTERNAL_ACK           = 1900,
    INTERNAL_NACK          = 1901,
    INTERNAL_RETRY_REQ     = 1902,
    INTERNAL_RETRY_DATA    = 1903,
    INTERNAL_ERROR         = 1904,
};

// ACK 필수 여부 판정
inline bool requires_ack(ProtocolNo no) {
    switch (no) {
        case ProtocolNo::STATION1_NG:
        case ProtocolNo::STATION2_NG:
        case ProtocolNo::MODEL_RELOAD_CMD:
        case ProtocolNo::TRAIN_COMPLETE:
        case ProtocolNo::TRAIN_FAIL:
        case ProtocolNo::INSPECT_NG_PUSH:
        case ProtocolNo::MODEL_DEPLOY_NOTIFY:
            return true;
        default:
            return false;
    }
}

// NG 패킷 → 대응 ACK 번호
inline ProtocolNo ack_no_for(ProtocolNo ng_no) {
    switch (ng_no) {
        case ProtocolNo::STATION1_NG: return ProtocolNo::STATION1_NG_ACK;
        case ProtocolNo::STATION2_NG: return ProtocolNo::STATION2_NG_ACK;
        default:                      return ProtocolNo::INTERNAL_ACK;
    }
}

} // namespace factory
