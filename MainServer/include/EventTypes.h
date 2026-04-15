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
    GUI_PUSH_REQUESTED,       // MFC 클라이언트 푸시 요청

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
    int          station_id      = 0;
    std::string  result;             // "NG"
    std::string  defect_type;        // 결함 유형
    double       score            = 0.0;
    int          latency_ms       = 0;
    std::string  timestamp;          // ISO8601
    std::vector<uint8_t> image_bytes; // NG 이미지
    std::string  raw_json;            // 원본 JSON (assemblies 등 부가정보 파싱용)
};

// HEALTH_CHECK_TICK 페이로드 (없음)

// SERVER_DOWN / SERVER_RECOVERED 페이로드
struct ServerStatusEvent {
    std::string server_name;   // "ai_inference_1" 등
    std::string ip;
    uint16_t    port = 0;
};

} // namespace factory
