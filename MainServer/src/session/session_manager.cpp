// ============================================================================
// session_manager.cpp — GUI 클라이언트 세션 관리자 구현
// ============================================================================
// 책임:
//   접속한 모든 GUI 클라이언트 세션을 "fd → GuiSession" 맵으로 관리하고,
//   JSON 브로드캐스트/유니캐스트를 제공한다.
//
// 싱글톤 이유:
//   - GuiTcpListener(accept 스레드) 에서 등록/해제
//   - GuiRouter(핸들러 스레드) 에서 사용자 정보 설정 + find_fd_by_username
//   - GuiNotifier(EventBus 워커 스레드) 에서 브로드캐스트
//   - AckSender 등도 간접 접근 가능
//   → 여러 스레드에서 공유되는 프로세스 단일 상태이므로 싱글톤.
//
// 스레드 안전성:
//   - 모든 public 메서드 진입부에서 std::mutex 획득
//   - 예외: broadcast/broadcast_with_binary 는 "스냅샷 후 lock 해제 → send"
//     패턴으로, 느린 클라이언트 때문에 다른 세션이 블로킹되지 않도록 분리
//   - 이전 버그 기록: mutex 보유한 채 send() 하다 한 느린 클라가 로그인 전체 마비
//
// 와이어 포맷:
//   [4바이트 Big-Endian JSON 길이] + [JSON 본문] (+ [바이너리 페이로드])
// ============================================================================
#include "session/session_manager.h"

#include "core/logger.h"
#include "core/tcp_utils.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace factory {

// ---------------------------------------------------------------------------
// instance — Meyers' Singleton (C++11 이후 thread-safe)
// 함수 내부 static 은 초기화가 보장된 1회 원자적 수행 → 경쟁 없음.
// ---------------------------------------------------------------------------
SessionManager& SessionManager::instance() {
    static SessionManager mgr;
    return mgr;
}

// ---------------------------------------------------------------------------
// register_session — 새 TCP 연결이 accept 된 직후 호출
// 이 시점엔 username 을 아직 모르므로 client_name 은 비어있다.
// LOGIN_REQ 처리 후 set_client_info 가 username 을 채운다.
// ---------------------------------------------------------------------------
void SessionManager::register_session(int client_fd, const std::string& remote_addr) {
    std::lock_guard<std::mutex> lock(mutex_);
    GuiSession session{};
    session.client_fd   = client_fd;
    session.remote_addr = remote_addr;
    sessions_[client_fd] = session;
    log_clt("클라이언트 접속 | fd=%d ip=%s | 현재접속=%zu", client_fd,
            remote_addr.c_str(), sessions_.size());
}

// ---------------------------------------------------------------------------
// unregister_session — TCP 연결 종료 시 호출 (handle_client 루프 종료 경로)
// 맵에서만 제거 — 소켓 close 는 호출자 책임 (GuiTcpListener::handle_client).
// 없는 fd 를 호출해도 안전 (find 후 분기).
// ---------------------------------------------------------------------------
void SessionManager::unregister_session(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        log_clt("클라이언트 해제 | fd=%d ip=%s", client_fd,
                it->second.remote_addr.c_str());
        sessions_.erase(it);
    }
}

// ---------------------------------------------------------------------------
// set_client_info — 로그인 성공 후 username/구독 station 기록
// subscribed_station: 0=전체 구독, 1=Station1 만, 2=Station2 만
// 현재 UI 는 station 선택 없음이라 항상 0 이 들어옴 (미래 확장 여지).
// ---------------------------------------------------------------------------
void SessionManager::set_client_info(int client_fd,
                                     const std::string& client_name,
                                     int subscribed_station) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        it->second.client_name       = client_name;
        it->second.subscribed_station = subscribed_station;
        log_clt("사용자 등록 | fd=%d 아이디=%s 스테이션=%d", client_fd,
                client_name.c_str(), subscribed_station);
    }
}

// 대상 세션 스냅샷 구조체 (mutex 해제 후 전송에 사용)
// fd 하나와 주소 문자열만 보관 → GuiSession 전체를 복사하지 않아 메모리 절약
struct BroadcastTarget {
    int         fd;              // 소켓 파일 디스크립터
    std::string remote_addr;     // 로그용 "IP:PORT" 문자열
};

// 필터링된 수신 대상 fd 목록을 mutex 보호 하에 "스냅샷" 복사한다.
// 이후 실제 send()는 mutex 밖에서 수행 → 느린 클라이언트가 다른 세션에 영향을 주지 않음.
// (이전 버그: mutex 보유한 채로 send() 호출 → 느린 클라 1명이 전체 마비)
static std::vector<BroadcastTarget> snapshot_targets(
    std::unordered_map<int, GuiSession>& sessions,
    int station_filter)
{
    std::vector<BroadcastTarget> targets;
    targets.reserve(sessions.size());   // 최대 세션 수만큼 미리 할당 (재할당 방지)

    // 구조적 바인딩(C++17): pair의 first/second를 fd/session으로 분해
    for (const auto& [fd, session] : sessions) {
        // 필터링 규칙:
        //   station_filter==0 → 모든 클라이언트에 전송 (전역 알림)
        //   station_filter!=0 → 해당 station 구독자 + 전체 구독자(subscribed_station==0)에만 전송
        if (station_filter != 0 &&                              // 필터 활성화됐고
            session.subscribed_station != 0 &&                  // 사용자가 "전체 보기" 아니고
            session.subscribed_station != station_filter) {     // 구독 station이 다르면
            continue;                                           // → 건너뛰기
        }
        // 대상에 포함 (초기화 리스트 문법: {fd, remote_addr})
        targets.push_back({fd, session.remote_addr});
    }
    return targets;
}

void SessionManager::broadcast(const std::string& json_message, int station_filter) {
    // 1) mutex 짧게 잡고 수신자 fd 목록만 스냅샷 (< 1ms)
    std::vector<BroadcastTarget> targets;
    {
        // 중괄호 스코프로 lock_guard 수명을 제한
        // 이 블록 끝나면 자동 unlock → 다른 스레드의 register/login 대기 시간 최소
        std::lock_guard<std::mutex> lock(mutex_);
        targets = snapshot_targets(sessions_, station_filter);
    }   // ← 여기서 mutex 해제됨

    // 2) mutex 해제 후 전송 → 한 클라이언트가 느려도 다른 세션 블로킹 없음
    //    한 클라 send()가 30초 블록되도 다른 세션 접속/로그인은 즉시 처리 가능
    for (const auto& t : targets) {
        if (!send_json(t.fd, json_message)) {           // 실패 시 로그만 남기고 계속 진행
            log_err_push("브로드캐스트 실패 | fd=%d ip=%s", t.fd,
                         t.remote_addr.c_str());
        }
    }
}

void SessionManager::broadcast_with_binary(const std::string& json_message,
                                            const std::vector<uint8_t>& binary_data,
                                            int station_filter) {
    // 동일 스냅샷 패턴: fd 목록만 복사 후 mutex 해제
    std::vector<BroadcastTarget> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        targets = snapshot_targets(sessions_, station_filter);
    }
    for (const auto& t : targets) {
        // [4바이트 헤더] + [JSON] + [이미지 바이너리]
        if (!send_json(t.fd, json_message)) {
            log_err_push("브로드캐스트 실패 | fd=%d ip=%s", t.fd,
                         t.remote_addr.c_str());
            continue;
        }
        if (!binary_data.empty()) {
            if (!send_all(t.fd, binary_data.data(), binary_data.size())) {
                log_err_push("이미지 전송 실패 | fd=%d size=%zu", t.fd, binary_data.size());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// send_to — 특정 fd 에만 유니캐스트 전송 (broadcast 의 특수 케이스)
// 현재 사용처: 로그인 직후 서버 상태(HEALTH_PUSH) 초기 동기화.
// 주의: mutex 보유 상태로 send_json 호출 → 일반적으로 비권장이나,
//       초기 동기화 메시지는 짧고 송신 대상이 방금 로그인한 한 명이라 영향 미미.
// ---------------------------------------------------------------------------
bool SessionManager::send_to(int client_fd, const std::string& json_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    if (it == sessions_.end()) return false;
    return send_json(client_fd, json_message);
}

// GuiTcpListener::run_accept_loop 에서 최대 접속 수 제한 검사용
std::size_t SessionManager::session_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

// ---------------------------------------------------------------------------
// find_fd_by_username — 중복 로그인 방지용: 같은 사용자명의 기존 fd 찾기
// 발견 시 GuiRouter 가 force_close 로 끊고 새 세션을 승리시키는 "last-login-wins"
// 정책을 구현한다. (VPN/IP 변경, 멀티 PC 로그인 시 새 세션 우선)
// ---------------------------------------------------------------------------
int SessionManager::find_fd_by_username(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [fd, session] : sessions_) {
        if (session.client_name == username) return fd;
    }
    return -1;
}

// fd → "IP:PORT" 조회. 로그용/진단용.
std::string SessionManager::get_remote_addr(int client_fd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(client_fd);
    return (it != sessions_.end()) ? it->second.remote_addr : std::string{};
}

// ---------------------------------------------------------------------------
// force_close — 소켓을 shutdown(SHUT_RDWR) 으로 깨워 자연 종료 유도
//
// 왜 close 가 아니라 shutdown 인가:
//   handle_client 스레드가 recv 에서 블로킹 중일 때 close() 하면 fd 번호가
//   재사용되어 다른 fd 와 충돌할 수 있음. shutdown 은 fd 를 닫지 않고 I/O 방향만
//   차단 → recv 가 0/에러 반환 → handle_client 루프 탈출 → unregister → close
//   순서로 안전하게 정리됨.
// 사용처:
//   - 중복 로그인 감지 시 (기존 세션 강제 정리)
// ---------------------------------------------------------------------------
void SessionManager::force_close(int client_fd) {
    ::shutdown(client_fd, SHUT_RDWR);
    log_clt("세션 강제 종료 | fd=%d (중복 로그인)", client_fd);
}

// 4바이트 BE 길이 + JSON 본문 프레이밍 송신 (partial send 재시도 포함)
bool SessionManager::send_json(int fd, const std::string& json_body) {
    return send_json_frame(fd, json_body);
}

} // namespace factory
