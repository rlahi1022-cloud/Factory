// ============================================================================
// gui_service.cpp — GUI 클라이언트 요청 비즈니스 로직 구현
// ============================================================================
// 목적:
//   GuiRouter 가 파싱해서 넘겨주는 클라이언트 요청(로그인/회원가입/이력/통계/
//   모델목록/재학습)을 실제로 처리한다. DB 접근은 DAO 3종(User/Model/Stats)에
//   위임하고, 학습서버로의 전달은 별도 TCP 연결을 맺어 원샷으로 수행한다.
//
// 책임 분리:
//   GuiRouter → (프로토콜 파싱/응답 JSON 조립)
//   GuiService → (비즈니스 규칙 + DAO 호출 + 학습서버 중계)
//   DAO       → (SQL 실행)
//
// 멀티스레드 주의:
//   - `is_training_` 은 mutex 로 보호 — 동시 재학습 요청 중복 수락 방지.
//   - DAO 는 ConnectionPool 을 통해 스레드당 커넥션을 acquire 하므로 락 불필요.
// ============================================================================
#include "session/gui_service.h"
#include "session/session_manager.h"
#include "core/logger.h"
#include "core/tcp_utils.h"
#include "Protocol.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace factory {

// ---------------------------------------------------------------------------
// 생성자 — DAO 3종을 ConnectionPool 로 초기화하고 학습서버 주소를 보관한다.
//   train_host / train_port : config.json 의 network.training_server_host/port
//   (환경변수 TRAIN_HOST 가 있으면 main.cpp 에서 이미 치환되어 들어온다)
// ---------------------------------------------------------------------------
GuiService::GuiService(ConnectionPool& pool,
                       const std::string& train_host,
                       uint16_t train_port)
    : user_dao_(pool), model_dao_(pool), stats_dao_(pool),
      train_host_(train_host), train_port_(train_port) {
}

// ---------------------------------------------------------------------------
// login — username/password 검증 후 LoginResult 반환
//   1) UserDao::find_by_username 으로 DB 에서 해시/권한 조회
//   2) PasswordHash::verify(평문, 해시) — bcrypt 비교 (상수시간 비교)
//   3) 성공 시 last_login 타임스탬프 갱신
//
//   주의: 실패한 경우에도 "사용자 없음" / "비밀번호 불일치" 를 구분하지 않는다.
//         사용자 열거 공격(user enumeration) 방지를 위한 의도된 동작.
// ---------------------------------------------------------------------------
LoginResult GuiService::login(const std::string& username, const std::string& password) {
    LoginResult result;
    auto user = user_dao_.find_by_username(username);

    if (user.found && PasswordHash::verify(password, user.password_hash)) {
        result.success     = true;
        result.username    = username;
        result.role        = user.role;
        result.employee_id = user.employee_id;
        user_dao_.update_last_login(username);
        log_clt("로그인 성공 | 사용자=%s 권한=%s", username.c_str(), user.role.c_str());
    } else {
        // 실패 경로: 일부러 사유를 구분하지 않음 (enumeration 방어)
        log_err_clt("로그인 실패 | 사용자=%s", username.c_str());
    }

    return result;
}

// ---------------------------------------------------------------------------
// register_user — 신규 사용자 등록
//   선검증(중복) → insert(내부에서 bcrypt 해싱) 순서.
//   UserDao::insert 는 성공 시 true, DB 오류/중복 등은 false.
// ---------------------------------------------------------------------------
RegisterResult GuiService::register_user(const std::string& employee_id,
                                          const std::string& username,
                                          const std::string& password,
                                          const std::string& role) {
    RegisterResult result;

    if (user_dao_.exists(username)) {
        result.message = "이미 존재하는 사용자입니다.";
    } else if (user_dao_.insert(employee_id, username, password, role)) {
        result.success = true;
        result.message = "회원가입 성공";
        log_clt("회원가입 성공 | 사용자=%s", username.c_str());
    } else {
        // DAO 내부에서 구체적 에러는 이미 로그로 남김
        result.message = "DB 오류";
        log_err_clt("회원가입 실패 | 사용자=%s", username.c_str());
    }

    return result;
}

// ---------------------------------------------------------------------------
// 조회(read-only) 함수들 — StatsDao/ModelDao 에 단순 위임
// 재학습 요청과 달리 상태 변경이 없으므로 mutex 불필요.
// ---------------------------------------------------------------------------
std::vector<StatsDao::InspectionRecord> GuiService::get_history(
    int station_filter, const std::string& from,
    const std::string& to, int limit) {
    return stats_dao_.get_history(station_filter, from, to, limit);
}

StatsDao::StatsResult GuiService::get_stats(
    int station_filter, const std::string& from, const std::string& to) {
    return stats_dao_.get_stats(station_filter, from, to);
}

std::vector<ModelDao::ModelInfo> GuiService::get_models() {
    return model_dao_.list_all();
}

// ---------------------------------------------------------------------------
// make_timestamp — 현재 로컬 시각을 ISO8601(초 단위)로 생성
//   학습서버로 넘기는 TRAIN_START_REQ 패킷의 `timestamp` 필드에 사용.
//   밀리초 미포함: 학습 요청은 초 단위 해상도로 충분.
// ---------------------------------------------------------------------------
static std::string make_timestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);  // localtime_r: thread-safe (localtime 은 TLS 공유)
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

// send_json_raw 는 tcp_utils.h 의 send_json_frame 으로 대체됨 (partial send 재시도 포함)

// ---------------------------------------------------------------------------
// request_retrain — RETRAIN_REQ(152) 를 학습서버로 TRAIN_START_REQ(1100) 포워딩
//
// 설계 이유 (별도 TCP 커넥션을 맺는 구조):
//   학습서버는 AI 추론서버와 달리 메인에 상시 접속해 있지 않고, 필요 시점에만
//   짧게 통신한다. 한 번 연결 → JSON 1회 송신 → 즉시 close 하는 원샷 패턴.
//   완료/진행률 알림은 학습서버가 "다른 방향"으로 메인에 접속하여 송신한다
//   (AckSender 쪽 비동기 수신 경로).
//
// 동시성 정책:
//   is_training_ 플래그로 동시에 하나만 허용. 두 번째 요청은 즉시 거부하여
//   GPU 메모리/학습 파이프라인 충돌을 방지한다.
//   성공 응답 후 플래그 해제는 TRAIN_COMPLETE/TRAIN_FAIL 수신 핸들러가 담당
//   (여기서는 해제하지 않음 — 학습 진행 중 상태 유지).
// ---------------------------------------------------------------------------
RetrainResult GuiService::request_retrain(int station_id, const std::string& model_type,
                                           const std::string& product_name, int image_count,
                                           const std::string& request_id) {
    RetrainResult result;

    // ── 1) 동시 학습 방지 — mutex 로 is_training_ 원자 검사/설정 ──
    {
        std::lock_guard<std::mutex> lock(train_mutex_);
        if (is_training_) {
            result.message = "이미 학습이 진행 중입니다.";
            log_err_train("재학습 거부 | 이미 진행 중");
            return result;
        }
        is_training_ = true;  // 이후 실패 경로에서 반드시 해제해야 함
    }

    log_train("재학습 요청 접수 | 스테이션=%d 모델=%s 이미지=%d건",
              station_id, model_type.c_str(), image_count);

    // ── 2) 학습서버 TCP 소켓 생성 ──
    int train_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (train_fd < 0) {
        // 소켓 생성 실패 — 학습은 시작조차 못 함. 플래그 해제 필수.
        std::lock_guard<std::mutex> lock(train_mutex_);
        is_training_ = false;
        result.message = "소켓 생성 실패";
        return result;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(train_port_);
    inet_pton(AF_INET, train_host_.c_str(), &addr.sin_addr);

    // 송신 타임아웃 3초 — 학습서버가 멈춰있을 때 클라이언트 응답 지연 최소화
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(train_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    // ── 3) 학습서버 접속 시도 — 실패 시 is_training_ 해제 필수 ──
    if (::connect(train_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        log_err_train("학습서버 연결 실패 | %s:%d", train_host_.c_str(), train_port_);
        ::close(train_fd);
        {
            std::lock_guard<std::mutex> lock(train_mutex_);
            is_training_ = false;  // 재시도 가능 상태로 되돌림
        }
        result.message = "학습서버 연결 실패";
        return result;
    }

    // ── 4) TRAIN_START_REQ(1100) JSON 조립 ──
    // 수동 조립 이유: 외부 JSON 라이브러리 의존성 회피 + 필드 고정 스키마.
    // 이스케이프가 필요한 필드는 현재 product_name 정도이나 입력 검증이 상위 단
    // (GuiRouter)에서 이루어지므로 여기서는 단순 concat.
    std::ostringstream os;
    os << "{\"protocol_no\":1100"
       << ",\"protocol_version\":\"1.0\""
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"station_id\":" << station_id
       << ",\"model_type\":\"" << model_type << "\""
       << ",\"product_name\":\"" << product_name << "\""
       << ",\"image_count\":" << image_count
       << ",\"timestamp\":\"" << make_timestamp() << "\"}";

    // ── 5) 원샷 송신 후 close ──
    // send_json_frame: [4바이트 BE 길이] + [JSON] 프레이밍 + partial send 재시도.
    if (send_json_frame(train_fd, os.str())) {
        result.success = true;
        result.message = "재학습 요청 전달 완료";
        log_train("TRAIN_START_REQ → 학습서버 전송 성공");
        // is_training_ 유지 — TRAIN_COMPLETE/FAIL 수신 핸들러가 해제
    } else {
        result.message = "학습서버 전송 실패";
        log_err_train("TRAIN_START_REQ → 학습서버 전송 실패");
        // 전송 실패는 학습 시작 자체가 안 된 것이므로 플래그 해제
        std::lock_guard<std::mutex> lock(train_mutex_);
        is_training_ = false;
    }

    ::close(train_fd);
    return result;
}

} // namespace factory
