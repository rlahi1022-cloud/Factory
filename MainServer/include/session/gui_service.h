// ============================================================================
// gui_service.h — GUI 클라이언트 요청 처리 서비스
// ============================================================================
// 책임: DAO를 통해 DB 조회/저장을 수행하고 결과를 반환한다.
// JSON 응답 생성이나 TCP 전송은 하지 않는다 (GuiRouter가 담당).
// ============================================================================
#pragma once

#include "storage/dao.h"
#include "storage/password_hash.h"

#include <mutex>
#include <string>
#include <vector>

namespace factory {

// ── 응답 구조체들 ──

struct LoginResult {
    bool success = false;
    std::string username;
    std::string role;
    std::string employee_id;
};

struct RegisterResult {
    bool success = false;
    std::string message;
};

struct RetrainResult {
    bool success = false;
    std::string message;
};

class GuiService {
public:
    /// @param pool        DB 커넥션 풀
    /// @param train_host  학습서버 IP (의존성 주입 — 환경별 변경 가능)
    /// @param train_port  학습서버 포트
    GuiService(ConnectionPool& pool,
               const std::string& train_host,
               uint16_t train_port);

    // 인증
    LoginResult login(const std::string& username, const std::string& password);
    RegisterResult register_user(const std::string& employee_id,
                                  const std::string& username,
                                  const std::string& password,
                                  const std::string& role);

    // 조회
    std::vector<StatsDao::InspectionRecord> get_history(
        int station_filter, const std::string& from,
        const std::string& to, int limit);

    // 단건 조회 — 이력 이미지 on-demand 로드용
    StatsDao::InspectionRecord get_inspection_by_id(int id) {
        return stats_dao_.get_by_id(id);
    }

    StatsDao::StatsResult get_stats(
        int station_filter, const std::string& from, const std::string& to);

    std::vector<ModelDao::ModelInfo> get_models();

    // 재학습 요청 → 학습서버 TCP 중계
    RetrainResult request_retrain(int station_id, const std::string& model_type,
                                   const std::string& product_name, int image_count,
                                   const std::string& request_id);

    /// 학습 완료 시 플래그 해제 (TrainHandler에서 호출)
    void set_training_done() {
        std::lock_guard<std::mutex> lock(train_mutex_);
        is_training_ = false;
    }

private:
    UserDao  user_dao_;
    ModelDao model_dao_;
    StatsDao stats_dao_;

    // 학습서버 주소 (생성자에서 주입)
    std::string train_host_;
    uint16_t    train_port_;

    // 동시 학습 요청 방지
    std::mutex train_mutex_;
    bool       is_training_ = false;
};

} // namespace factory
