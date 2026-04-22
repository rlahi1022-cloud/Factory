#pragma once
// ============================================================================
// ClientProtocol.h
// ============================================================================
// 목적:
//   MFC 클라이언트에서 사용할 통신 프로토콜 상수를 정의합니다.
//   메인서버(Serv/MainServer/common/Protocol.h)와 동일한 프로토콜 번호를 사용하여
//   양쪽이 같은 "언어"로 대화할 수 있도록 합니다.
//
// 프로토콜 구조 (패킷 포맷):
//   [4바이트 길이 헤더(Big-Endian)] + [JSON 본문] + [이미지 바이너리(NG일 때만)]
//
// 메시지 번호 범위:
//   100~199  : MFC 클라이언트 ↔ 운용서버 (외부 채널)
//   1000~1999: 운용서버 ↔ 추론/학습 서버 (내부 채널, 참고용)
// ============================================================================

#include <cstdint>  // uint16_t, uint32_t 등 정수 타입

namespace factory_client {

// ── 상수 ──────────────────────────────────────────────────────────────────
// HEADER_SIZE: 패킷 맨 앞 4바이트는 뒤따라올 JSON 본문의 크기(바이트)를 담음
constexpr int HEADER_SIZE = 4;

// GUI_PORT: MFC 클라이언트가 접속할 메인서버 포트 (추론서버용 9000과 별개)
constexpr uint16_t GUI_PORT = 9010;

// DEFAULT_SERVER_IP: 메인서버(운용 서버)의 기본 IP 주소
// ※ 서버가 다른 PC에서 실행 중이면 해당 PC의 IP로 변경하세요!
//    예) 같은 PC → "127.0.0.1"
//        다른 PC → "10.10.10.130" 등
constexpr const wchar_t* DEFAULT_SERVER_IP = L"10.10.10.130";

// PROTOCOL_VERSION: 프로토콜 버전 문자열 (서버와 일치해야 함)
constexpr const char* PROTOCOL_VERSION = "1.0";

// RECV_BUF_SIZE: 수신 버퍼 최대 크기 (현재 미사용 — 실제 한도는 PacketBuilder::ParseHeader 가 결정, v0.14.5 부터 1MB)
constexpr int RECV_BUF_SIZE = 65536;

// ── 프로토콜 메시지 번호 (ProtocolNo) ─────────────────────────────────────
// 메인서버 Protocol.h와 동일한 값을 사용합니다.
// "요청(REQ)"은 클라이언트→서버, "응답(RES)/푸시(PUSH)"는 서버→클라이언트입니다.
enum ProtocolNo : int {
    // ===== 인증 관련 (100~109) =====
    LOGIN_REQ              = 100,   // 로그인 요청: 클라이언트 → 서버
    LOGIN_RES              = 101,   // 로그인 응답: 서버 → 클라이언트
    LOGOUT_REQ             = 102,   // 로그아웃 요청
    LOGOUT_RES             = 103,   // 로그아웃 응답
    REGISTER_REQ           = 104,   // 회원가입 요청: 클라이언트 → 서버
    REGISTER_RES           = 105,   // 회원가입 응답: 서버 → 클라이언트

    // ===== 검사 결과 관련 (110~129) =====
    INSPECT_NG_PUSH        = 110,   // NG 결과 푸시: 서버 → 클라이언트 (실시간)
    INSPECT_NG_ACK_EXT     = 111,   // NG 푸시 수신 확인: 클라이언트 → 서버
    INSPECT_OK_COUNT_PUSH  = 112,   // OK/NG 카운트 푸시: 서버 → 클라이언트 (주기적)
    INSPECT_HISTORY_REQ    = 114,   // 검사 이력 조회 요청: 클라이언트 → 서버
    INSPECT_HISTORY_RES    = 115,   // 검사 이력 응답: 서버 → 클라이언트
    INSPECT_IMAGE_REQ      = 116,   // 이력 이미지 요청: 클라 → 서버 (v0.10+)
    INSPECT_IMAGE_RES      = 117,   // 이력 이미지 응답: JSON + 3장 바이너리

    // ===== 통계 관련 (130~149) =====
    STATS_REQ              = 130,   // 통계 데이터 요청
    STATS_RES              = 131,   // 통계 데이터 응답

    // ===== 모델 관리 관련 (150~169) =====
    MODEL_LIST_REQ         = 150,   // 배포된 모델 목록 요청
    MODEL_LIST_RES         = 151,   // 모델 목록 응답
    RETRAIN_REQ            = 152,   // 재학습 실행 요청
    RETRAIN_RES            = 153,   // 재학습 시작 응답
    RETRAIN_PROGRESS_PUSH  = 154,   // 재학습 진행률 푸시: 서버 → 클라이언트
    MODEL_DEPLOY_NOTIFY    = 156,   // 모델 배포 완료 알림
    MODEL_DEPLOY_ACK_EXT   = 157,   // 배포 알림 수신 확인
    RETRAIN_UPLOAD         = 158,   // v0.13.0: 학습용 이미지 1장 업로드 (JSON+binary)
    RETRAIN_UPLOAD_ACK     = 159,   // v0.13.0: 업로드 결과 ACK
    INSPECT_CONTROL_REQ    = 160,   // v0.14.0: 검사 pause/resume 요청
    INSPECT_CONTROL_RES    = 161,   // v0.14.0: pause/resume 결과 ACK

    // ===== 서버 상태 관련 (170~189) =====
    SERVER_HEALTH_PUSH     = 170,   // 서버 헬스체크 상태 푸시: 서버 → 클라이언트

    // ===== 범용 ACK/NACK/에러 (190~199) =====
    EXT_ACK                = 190,   // 범용 수신 확인
    EXT_NACK               = 191,   // 범용 수신 거부
    EXT_ERROR              = 192,   // 에러 응답
};

// ── ACK 필요 여부 판별 함수 ───────────────────────────────────────────────
// 목적: 특정 프로토콜 메시지를 수신했을 때, 상대방에게 ACK를 보내야 하는지 판단
// 파라미터: no - 수신한 프로토콜 번호
// 반환값: true이면 ACK 전송 필요
inline bool RequiresAck(int no) {
    switch (no) {
    case INSPECT_NG_PUSH:       // NG 푸시 수신 시 확인 응답 필요
    case MODEL_DEPLOY_NOTIFY:   // 모델 배포 알림 수신 시 확인 응답 필요
        return true;
    default:
        return false;
    }
}

// ── ACK 메시지 번호 반환 함수 ─────────────────────────────────────────────
// 목적: 수신한 메시지에 대응하는 ACK 번호를 반환
// 예) INSPECT_NG_PUSH(110) → INSPECT_NG_ACK_EXT(111)
inline int AckNoFor(int recv_no) {
    switch (recv_no) {
    case INSPECT_NG_PUSH:     return INSPECT_NG_ACK_EXT;
    case MODEL_DEPLOY_NOTIFY: return MODEL_DEPLOY_ACK_EXT;
    default:                  return EXT_ACK;
    }
}

} // namespace factory_client
