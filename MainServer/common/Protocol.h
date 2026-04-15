#pragma once
// Protocol.h
// AI 추론 서버 <-> 메인 운영 서버 간 통신 프로토콜 정의
// 패킷 형식: [4byte length(JSON size, big-endian)] + [JSON payload] + [Image binary (NG시에만)]

#include <cstdint>
#include <string>

namespace factory {

// 패킷 헤더 길이 (JSON 크기를 나타내는 4바이트)
constexpr std::size_t HEADER_SIZE = 4;

// 메인 서버 리스너 포트
constexpr uint16_t MAIN_SERVER_PORT = 9000;

// 스테이션 ID
enum class StationId : int {
    INBOUND  = 1,  // 입고 검사
    ASSEMBLY = 2,  // 조립 검사
};

// 검사 결과 (JSON "result" 필드 값)
// NG 시에만 메인 서버로 전송되므로 사실상 NG만 다루지만 OK 정의는 둠
namespace ResultStr {
    constexpr const char* OK = "OK";
    constexpr const char* NG = "NG";
}

// 메시지 타입
namespace MsgType {
    constexpr const char* INSPECT    = "inspect";    // 검사 결과
    constexpr const char* HEARTBEAT  = "heartbeat";  // 헬스체크 ping/pong
    constexpr const char* MODEL_INFO = "model_info"; // 모델 메타 정보
}

} // namespace factory
