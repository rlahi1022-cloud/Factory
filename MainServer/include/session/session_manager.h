// ============================================================================
// session_manager.h — GUI 클라이언트 세션 관리자
// ============================================================================
// 목적:
//   MFC 클라이언트의 TCP 접속 세션을 관리한다.
//   접속한 클라이언트를 fd(파일 디스크립터) 기반으로 등록/해제하고,
//   특정 station 또는 전체 클라이언트에 JSON 메시지를 브로드캐스트한다.
//
// 사용 흐름:
//   1. GuiTcpListener가 클라이언트 접속 수락 → register_session() 호출
//   2. 로그인 성공 시 set_client_info()로 사용자명·구독 station 설정
//   3. GuiNotifier가 이벤트 수신 → broadcast()로 실시간 푸시
//   4. 클라이언트 연결 종료 → unregister_session() 호출
//
// 스레드 안전:
//   모든 public 메서드는 내부 mutex로 보호되어 멀티스레드 환경에서 안전하다.
// ============================================================================
#pragma once

#include "core/event_bus.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace factory {

// 개별 GUI 클라이언트 세션 정보
struct GuiSession {
    int         client_fd    = -1;
    std::string remote_addr;          // "IP:PORT"
    std::string client_name;          // 클라이언트 식별 (예: "operator_1")
    int         subscribed_station = 0; // 0이면 전체 구독, 1/2면 해당 station만
};

class SessionManager {
public:
    static SessionManager& instance();

    // 세션 등록/해제
    void register_session(int client_fd, const std::string& remote_addr);
    void unregister_session(int client_fd);

    // 클라이언트 이름/구독 station 설정 (로그인 후)
    void set_client_info(int client_fd,
                         const std::string& client_name,
                         int subscribed_station);

    // 연결된 모든 클라이언트에 JSON broadcast
    // station_filter: 0이면 전체, 1/2이면 해당 station 구독자만
    void broadcast(const std::string& json_message, int station_filter = 0);

    // JSON + 바이너리(이미지) broadcast
    void broadcast_with_binary(const std::string& json_message,
                               const std::vector<uint8_t>& binary_data,
                               int station_filter = 0);

    // 특정 클라이언트에 JSON 전송
    bool send_to(int client_fd, const std::string& json_message);

    // 현재 연결된 세션 수
    std::size_t session_count() const;

    /// 같은 username으로 이미 로그인된 세션의 fd를 반환 (없으면 -1)
    /// 동시 로그인 방지를 위해 기존 세션을 찾아 강제 종료할 때 사용
    int find_fd_by_username(const std::string& username) const;

    /// 지정된 fd의 세션을 강제 종료 — 연결 끊고 세션 제거
    void force_close(int client_fd);

private:
    SessionManager() = default;

    // 4바이트 빅엔디안 길이 헤더 + JSON 본문을 소켓으로 전송
    bool send_json(int fd, const std::string& json_body);

    mutable std::mutex                       mutex_;      // 세션 맵 동시 접근 보호
    std::unordered_map<int, GuiSession>      sessions_;   // key: client_fd
};

} // namespace factory
