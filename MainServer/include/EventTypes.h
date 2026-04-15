#pragma once
// EventTypes.h
// 이벤트 버스에서 사용하는 이벤트 종류와 이벤트 데이터 구조 정의
// EventBus는 EventType을 key로 핸들러를 라우팅함

#include <string>
#include <vector>
#include <cstdint>

namespace factory {

// 이벤트 종류
enum class EventType : int {
    // TCP 수신 계층
    PACKET_RECEIVED = 0,      // 추론서버로부터 패킷 수신 (raw)

    // 라우팅 후
    INSPECTION_INBOUND,       // 입고 검사 결과 도착
    INSPECTION_ASSEMBLY,      // 조립 검사 결과 도착

    // 후처리
    INSPECTION_VALIDATED,     // Manager 검증 통과 (DB 저장 대상)
    IMAGE_SAVE_REQUESTED,     // NG 이미지 저장 요청
    DB_WRITE_REQUESTED,       // DB 기록 요청
    DB_WRITE_COMPLETED,       // DB 기록 성공 → ACK 송신 트리거
    GUI_PUSH_REQUESTED,       // MFC 클라이언트 푸시 요청

    // ACK / 메타
    ACK_SEND_REQUESTED,       // 추론서버로 ACK 송신 요청
    OK_COUNT_RECEIVED,        // 추론서버 OK 카운트 수신
    INSPECT_META_RECEIVED,    // 추론서버 검사 메타(OK/NG 공통) 수신

    // 헬스체크
    HEALTH_CHECK_TICK,        // 5초 주기 헬스체크 트리거
    SERVER_DOWN,              // 서버 장애 감지
    SERVER_RECOVERED,         // 서버 복구 감지

    // 시스템
    SYSTEM_SHUTDOWN,
};

// PACKET_RECEIVED 이벤트 페이로드
struct PacketReceivedEvent {
    std::string  json_payload;       // JSON 본문
    std::vector<uint8_t> image_bytes; // 이미지 바이너리 (없으면 빈 vector)
    std::string  remote_addr;        // 송신자 IP
};

// INSPECTION_INBOUND / INSPECTION_ASSEMBLY 공통 페이로드
struct InspectionEvent {
    int          protocol_no     = 0;     // 1000 / 1002
    std::string  inspection_id;            // 추론서버 발급 ID (필수)
    int          station_id      = 0;
    std::string  result;                   // "NG"
    std::string  defect_type;
    double       score           = 0.0;
    int          latency_ms      = 0;
    std::string  timestamp;                // ISO8601
    std::vector<uint8_t> image_bytes;      // NG 이미지
    std::string  raw_json;                  // assemblies 부가정보 추출용
    std::string  sender_addr;               // ACK 회신용 (호스트:포트)
};

// ACK_SEND_REQUESTED 페이로드
struct AckSendEvent {
    int         protocol_no = 0;          // 송신할 ACK 번호 (1001/1003 등)
    std::string inspection_id;
    std::string sender_addr;               // 회신 대상
    bool        ack_ok       = true;       // false면 NACK
    std::string error_message;             // NACK 시
};

// OK_COUNT_RECEIVED 페이로드 (1004)
struct OkCountEvent {
    int         station_id    = 0;
    int         ok_count      = 0;
    int         ng_count      = 0;
    double      latency_avg   = 0.0;
    std::string period;
};

// INSPECT_META_RECEIVED 페이로드 (1006)
struct InspectMetaEvent {
    std::string inspection_id;
    int         station_id  = 0;
    std::string timestamp;
    int         latency_ms  = 0;
    int         model_id    = 0;
    std::string result;     // ok / ng — DB inspections 기본 행 기록용
};

// HEALTH_CHECK_TICK 페이로드 (없음)

// SERVER_DOWN / SERVER_RECOVERED 페이로드
struct ServerStatusEvent {
    std::string server_name;   // "ai_inference_1" 등
    std::string ip;
    uint16_t    port = 0;
};

} // namespace factory
