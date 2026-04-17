// ============================================================================
// ack_sender.cpp — 추론서버 ACK 응답 송신기
// ============================================================================
// 목적:
//   검사 결과 처리가 완료되면 추론서버에 ACK를 회신하여
//   요청-응답 사이클을 완결시킨다.
//
// 패킷 흐름:
//   StationHandler → DB_WRITE_REQUESTED → DbManager → DB_WRITE_COMPLETED
//     → [AckSender] → TCP send → 추론서버
//
// 프레임 포맷:
//   [4바이트 Big-Endian JSON 길이] + [JSON 본문]
//   추론서버 측 PacketReader와 동일한 length-prefix 프로토콜을 사용한다.
// ============================================================================
#include "handler/ack_sender.h"
#include "monitor/connection_registry.h"
#include "Protocol.h"

#include "core/logger.h"

#include <cstdint>
#include <iostream>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
#else
  #include <sys/socket.h>
#endif

namespace factory {

AckSender::AckSender(EventBus& bus)
    : event_bus_(bus) {
}

// ---------------------------------------------------------------------------
// register_handlers — 두 가지 ACK 트리거 이벤트를 구독한다.
//   1) DB_WRITE_COMPLETED : 정상 흐름 — DB 기록 성공 후 ack_ok=true 전송
//   2) ACK_SEND_REQUESTED : 예외 흐름 — 파이프라인 도중 오류 발생 시 즉시 전송
// ---------------------------------------------------------------------------
void AckSender::register_handlers() {
    event_bus_.subscribe(EventType::DB_WRITE_COMPLETED,
                         [this](const std::any& p) { this->on_db_write_completed(p); });
    event_bus_.subscribe(EventType::ACK_SEND_REQUESTED,
                         [this](const std::any& p) { this->on_ack_send_requested(p); });
    event_bus_.subscribe(EventType::MODEL_RELOAD_REQUESTED,
                         [this](const std::any& p) { this->on_model_reload_requested(p); });
}

// ---------------------------------------------------------------------------
// on_db_write_completed — DB 기록 성공 콜백.
// 원래 검사 요청의 protocol_no(예: STATION1_NG=1000)를 ACK 번호(1001)로 변환하여
// 추론서버에 성공 ACK를 보낸다.
// ---------------------------------------------------------------------------
void AckSender::on_db_write_completed(const std::any& payload) {
    const auto& ev = std::any_cast<const InspectionEvent&>(payload);
    // ack_no_for()는 Protocol.h에 정의된 유틸 — 요청 번호 → ACK 번호 매핑
    int ack_no = static_cast<int>(
        ack_no_for(static_cast<ProtocolNo>(ev.protocol_no)));
    send_ack(ev.sender_addr, ack_no, ev.inspection_id, true, "");
}

// ---------------------------------------------------------------------------
// on_ack_send_requested — 명시적 ACK 요청 콜백.
// 이미지 저장 실패, 검증 오류 등 비정상 상황에서 ack_ok=false와 함께
// error_message를 추론서버에 전달한다.
// ---------------------------------------------------------------------------
void AckSender::on_ack_send_requested(const std::any& payload) {
    const auto& ev = std::any_cast<const AckSendEvent&>(payload);
    send_ack(ev.sender_addr, ev.protocol_no, ev.inspection_id,
             ev.ack_ok, ev.error_message);
}

// ---------------------------------------------------------------------------
// send_ack — 실제 소켓 전송.
//
// 1) ConnectionRegistry에서 sender_addr("ip:port")로 파일 디스크립터(fd)를 조회
//    → 추론서버가 연결한 바로 그 소켓을 통해 응답하기 위함
// 2) JSON 본문을 수동으로 구성 (외부 JSON 라이브러리 미사용)
// 3) [4바이트 BE 길이 헤더] + [JSON 본문]을 순서대로 전송
// ---------------------------------------------------------------------------
bool AckSender::send_ack(const std::string& sender_addr,
                         int protocol_no,
                         const std::string& inspection_id,
                         bool ack_ok,
                         const std::string& error_message) {
    // sender_addr로 ConnectionRegistry에서 소켓 fd 조회
    int fd = ConnectionRegistry::instance().find_fd(sender_addr);
    if (fd < 0) {
        log_err_ack("연결 없음 | addr=%s", sender_addr.c_str());
        return false;
    }

    // ACK JSON 빌드 — 외부 라이브러리 없이 ostringstream으로 직접 구성
    std::ostringstream os;
    os << "{"
       << "\"protocol_no\":" << protocol_no << ","
       << "\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\","
       << "\"inspection_id\":\"" << inspection_id << "\","
       << "\"ack\":" << (ack_ok ? "true" : "false");
    // 실패 시에만 error_message 필드를 추가하여 페이로드를 최소화
    if (!ack_ok) {
        os << ",\"error_message\":\"" << error_message << "\"";
    }
    // image_size=0 : ACK에는 이미지가 없음을 명시 (추론서버 파서 호환용)
    os << ",\"image_size\":0"
       << "}";
    std::string json_body = os.str();

    // 4바이트 Big-Endian 길이 헤더 구성
    uint32_t json_size = static_cast<uint32_t>(json_body.size());
    uint8_t  header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),
        static_cast<uint8_t>((json_size >> 16) & 0xFF),
        static_cast<uint8_t>((json_size >>  8) & 0xFF),
        static_cast<uint8_t>( json_size        & 0xFF),
    };

    // 헤더 → 본문 순서로 전송, 둘 다 완전히 전송되었는지 검증
    int sent_h = static_cast<int>(::send(fd, reinterpret_cast<const char*>(header), 4, 0));
    int sent_b = static_cast<int>(::send(fd, json_body.c_str(),
                                         static_cast<int>(json_body.size()), 0));
    if (sent_h != 4 || sent_b != static_cast<int>(json_body.size())) {
        log_err_ack("전송 실패 | fd=%d addr=%s", fd, sender_addr.c_str());
        return false;
    }
    log_ack("ACK 전송 | no=%d id=%s → %s", protocol_no,
            inspection_id.c_str(), sender_addr.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// on_model_reload_requested — 학습 완료 후 추론서버에 새 모델 전송
// ConnectionRegistry에서 해당 station의 추론서버 연결을 찾아 전송한다.
// ---------------------------------------------------------------------------
void AckSender::on_model_reload_requested(const std::any& payload) {
    const auto& ev = std::any_cast<const ModelReloadEvent&>(payload);

    log_train("MODEL_RELOAD_CMD 전송 시작 | 스테이션=%d 버전=%s 크기=%zu bytes",
              ev.station_id, ev.version.c_str(), ev.model_bytes.size());

    if (!send_model_reload(ev.station_id, ev.model_path, ev.version, ev.model_bytes)) {
        log_err_train("MODEL_RELOAD_CMD 전송 실패 | 스테이션=%d", ev.station_id);
    }
}

// ---------------------------------------------------------------------------
// send_model_reload — MODEL_RELOAD_CMD(1010) + 모델 바이너리 전송
// 패킷 구조: [4바이트 헤더] + [JSON(image_size=N)] + [모델 바이너리(N bytes)]
// ConnectionRegistry에서 모든 연결을 순회하여 해당 station의 추론서버를 찾는다.
// ---------------------------------------------------------------------------
bool AckSender::send_model_reload(int station_id,
                                   const std::string& model_path,
                                   const std::string& version,
                                   const std::vector<uint8_t>& model_bytes) {
    // ConnectionRegistry에서 연결된 추론서버 fd를 찾는다.
    // 추론서버는 메인서버(9000)에 접속하므로 ConnectionRegistry에 등록되어 있다.
    auto& registry = ConnectionRegistry::instance();
    auto connections = registry.get_all_connections();

    if (connections.empty()) {
        log_err_train("연결된 추론서버 없음");
        return false;
    }

    // JSON 본문 구성
    std::ostringstream os;
    os << "{"
       << "\"protocol_no\":" << static_cast<int>(ProtocolNo::MODEL_RELOAD_CMD) << ","
       << "\"protocol_version\":\"" << FACTORY_PROTOCOL_VERSION << "\","
       << "\"station_id\":" << station_id << ","
       << "\"model_path\":\"" << model_path << "\","
       << "\"version\":\"" << version << "\","
       << "\"image_size\":" << model_bytes.size()
       << "}";
    std::string json_body = os.str();

    // [4바이트 BE 헤더] + [JSON] + [모델 바이너리]
    uint32_t json_size = static_cast<uint32_t>(json_body.size());
    uint8_t header[4] = {
        static_cast<uint8_t>((json_size >> 24) & 0xFF),
        static_cast<uint8_t>((json_size >> 16) & 0xFF),
        static_cast<uint8_t>((json_size >>  8) & 0xFF),
        static_cast<uint8_t>( json_size        & 0xFF),
    };

    bool sent_any = false;
    for (const auto& [addr, fd] : connections) {
        int sent_h = static_cast<int>(::send(fd, reinterpret_cast<const char*>(header), 4, 0));
        int sent_j = static_cast<int>(::send(fd, json_body.c_str(),
                                             static_cast<int>(json_body.size()), 0));
        int sent_m = 0;
        if (!model_bytes.empty()) {
            sent_m = static_cast<int>(::send(fd, reinterpret_cast<const char*>(model_bytes.data()),
                                             static_cast<int>(model_bytes.size()), 0));
        }

        if (sent_h == 4 && sent_j == static_cast<int>(json_body.size()) &&
            sent_m == static_cast<int>(model_bytes.size())) {
            log_train("MODEL_RELOAD_CMD 전송 성공 | → %s fd=%d (%zu bytes)",
                      addr.c_str(), fd, model_bytes.size());
            sent_any = true;
        } else {
            log_err_train("MODEL_RELOAD_CMD 전송 실패 | → %s fd=%d", addr.c_str(), fd);
        }
    }

    return sent_any;
}

} // namespace factory
