// ============================================================================
// tcp_utils.h — TCP 전송 유틸리티
// ============================================================================
// send_all: partial send를 처리하는 안전한 전송 함수
// send_frame: [4바이트 BE 길이 헤더] + [데이터]를 원자적으로 전송
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <sys/socket.h>
  #include <cerrno>
#endif

namespace factory {

/// 지정된 바이트를 모두 전송할 때까지 재시도. 실패 시 false.
inline bool send_all(int fd, const void* data, std::size_t len) {
    const char* ptr = static_cast<const char*>(data);
    std::size_t remaining = len;
    while (remaining > 0) {
        ssize_t sent = ::send(fd, ptr, static_cast<int>(remaining), MSG_NOSIGNAL);
        if (sent <= 0) {
            if (sent < 0 && errno == EINTR) continue;  // 인터럽트 → 재시도
            return false;  // 연결 끊김 또는 오류
        }
        ptr += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

/// [4바이트 BE 헤더] + [JSON 본문] 전송
inline bool send_json_frame(int fd, const std::string& json_body) {
    uint32_t json_size = static_cast<uint32_t>(json_body.size());
    uint8_t header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),
        static_cast<uint8_t>((json_size >> 16) & 0xFF),
        static_cast<uint8_t>((json_size >>  8) & 0xFF),
        static_cast<uint8_t>( json_size        & 0xFF),
    };
    if (!send_all(fd, header, 4)) return false;
    return send_all(fd, json_body.c_str(), json_body.size());
}

} // namespace factory
