// ============================================================================
// gui_router.h — GUI 클라이언트 요청 라우터
// ============================================================================
// 책임: protocol_no별로 GuiService 메서드를 호출하고 JSON 응답을 전송한다.
// TCP 수신/세션 관리와 분리되어 요청 처리 로직만 담당한다.
// ============================================================================
#pragma once

#include "session/gui_service.h"

#include <string>

namespace factory {

class GuiRouter {
public:
    explicit GuiRouter(GuiService& service);

    // protocol_no에 따라 적절한 핸들러 호출
    void route(int client_fd, const std::string& remote_addr,
               const std::string& json_request);

private:
    void handle_login(int fd, const std::string& json);
    void handle_register(int fd, const std::string& json);
    void handle_logout(int fd, const std::string& json);
    void handle_inspect_history(int fd, const std::string& json);
    void handle_stats(int fd, const std::string& json);
    void handle_model_list(int fd, const std::string& json);
    void handle_retrain(int fd, const std::string& json);

    // 유틸리티
    static std::string extract_str(const std::string& json, const std::string& key);
    static int extract_int(const std::string& json, const std::string& key);
    static bool send_json(int fd, const std::string& json_body);
    static std::string get_timestamp();
    static std::string escape_json(const std::string& s);

    GuiService& service_;
};

} // namespace factory
