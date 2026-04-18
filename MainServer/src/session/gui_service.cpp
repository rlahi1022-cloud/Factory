// ============================================================================
// gui_service.cpp — GUI 클라이언트 요청 처리 서비스 구현
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

GuiService::GuiService(ConnectionPool& pool,
                       const std::string& train_host,
                       uint16_t train_port)
    : user_dao_(pool), model_dao_(pool), stats_dao_(pool),
      train_host_(train_host), train_port_(train_port) {
}

// ── 로그인 ──

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
        log_err_clt("로그인 실패 | 사용자=%s", username.c_str());
    }

    return result;
}

// ── 회원가입 ──

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
        result.message = "DB 오류";
        log_err_clt("회원가입 실패 | 사용자=%s", username.c_str());
    }

    return result;
}

// ── 조회 ──

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

// ── 재학습 요청 → 학습서버 TCP 중계 ──

static std::string make_timestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

// send_json_raw는 tcp_utils.h의 send_json_frame으로 대체

RetrainResult GuiService::request_retrain(int station_id, const std::string& model_type,
                                           const std::string& product_name, int image_count,
                                           const std::string& request_id) {
    RetrainResult result;

    // 동시 학습 방지 — 이미 학습 중이면 거부
    {
        std::lock_guard<std::mutex> lock(train_mutex_);
        if (is_training_) {
            result.message = "이미 학습이 진행 중입니다.";
            log_err_train("재학습 거부 | 이미 진행 중");
            return result;
        }
        is_training_ = true;
    }

    log_train("재학습 요청 접수 | 스테이션=%d 모델=%s 이미지=%d건",
              station_id, model_type.c_str(), image_count);

    int train_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (train_fd < 0) {
        result.message = "소켓 생성 실패";
        return result;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(train_port_);
    inet_pton(AF_INET, train_host_.c_str(), &addr.sin_addr);

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(train_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    if (::connect(train_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        log_err_train("학습서버 연결 실패 | %s:%d", train_host_.c_str(), train_port_);
        ::close(train_fd);
        {
            std::lock_guard<std::mutex> lock(train_mutex_);
            is_training_ = false;
        }
        result.message = "학습서버 연결 실패";
        return result;
    }

    std::ostringstream os;
    os << "{\"protocol_no\":1100"
       << ",\"protocol_version\":\"1.0\""
       << ",\"request_id\":\"" << request_id << "\""
       << ",\"station_id\":" << station_id
       << ",\"model_type\":\"" << model_type << "\""
       << ",\"product_name\":\"" << product_name << "\""
       << ",\"image_count\":" << image_count
       << ",\"timestamp\":\"" << make_timestamp() << "\"}";

    if (send_json_frame(train_fd, os.str())) {
        result.success = true;
        result.message = "재학습 요청 전달 완료";
        log_train("TRAIN_START_REQ → 학습서버 전송 성공");
    } else {
        result.message = "학습서버 전송 실패";
        log_err_train("TRAIN_START_REQ → 학습서버 전송 실패");
    }

    ::close(train_fd);
    return result;
}

} // namespace factory
