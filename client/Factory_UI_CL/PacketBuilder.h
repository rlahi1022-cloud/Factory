#pragma once
// ============================================================================
// PacketBuilder.h
// ============================================================================
// 목적:
//   TCP 통신에서 사용하는 패킷을 조립(build)하고 분해(parse)하는 유틸리티 클래스.
//   모든 패킷은 다음 포맷을 따릅니다:
//
//     [4바이트 길이 헤더, Big-Endian] + [JSON 문자열] + [이미지 바이너리(선택)]
//
//   "Big-Endian"이란?
//   → 숫자의 큰 자리(상위 바이트)부터 먼저 저장하는 방식입니다.
//     예) 숫자 256 = 0x00000100
//         Big-Endian 바이트 배열: [0x00, 0x00, 0x01, 0x00]
//         Little-Endian(Intel CPU 기본): [0x00, 0x01, 0x00, 0x00]
//   네트워크 통신에서는 Big-Endian이 표준이므로 변환이 필요합니다.
//
// 사용 예시:
//   // 1) JSON 문자열을 패킷으로 변환해서 전송
//   CString json = CPacketBuilder::BuildLoginReq("admin01", "1234");
//   std::vector<char> packet = CPacketBuilder::BuildPacket(json);
//   networkClient.Send(packet);
//
//   // 2) 수신한 4바이트 헤더에서 JSON 크기 추출
//   UINT32 jsonSize = 0;
//   CPacketBuilder::ParseHeader(headerBuf, jsonSize);
// ============================================================================

#include "ClientProtocol.h"
#include <vector>   // std::vector — 가변 크기 배열
#include <string>   // std::string — C++ 문자열

class CPacketBuilder {
public:
    // ── 패킷 조립 함수 ───────────────────────────────────────────────────

    // BuildPacket: JSON 문자열을 프로토콜 패킷으로 변환
    // 목적: [4바이트 헤더] + [JSON 바이트]를 하나의 버퍼로 합침
    // 파라미터:
    //   jsonBody — 전송할 JSON 문자열 (CString: MFC 유니코드 문자열)
    // 반환값: 전송 가능한 바이트 배열 (std::vector<char>)
    static std::vector<char> BuildPacket(const CString& jsonBody);

    // BuildPacketWithImage: JSON + 이미지 바이너리를 패킷으로 변환
    // 목적: NG 이미지를 함께 전송할 때 사용 (현재는 서버→클라이언트 방향만 해당)
    // 파라미터:
    //   jsonBody  — JSON 문자열
    //   imageData — 이미지 바이트 배열
    //   imageSize — 이미지 크기 (바이트)
    // 반환값: [4바이트 헤더] + [JSON] + [이미지 바이너리]
    static std::vector<char> BuildPacketWithImage(
        const CString& jsonBody,
        const char* imageData,
        int imageSize);

    // ── 패킷 파싱 함수 ───────────────────────────────────────────────────

    // ParseHeader: 수신한 4바이트 헤더에서 JSON 본문 크기를 추출
    // 목적: 수신 시 "다음에 읽어야 할 바이트 수"를 알아냄
    // 파라미터:
    //   headerBuf    — 4바이트 헤더 버퍼 (Big-Endian)
    //   outJsonSize  — [출력] JSON 본문의 바이트 크기
    // 반환값: true=유효한 크기, false=비정상 (0이거나 64KB 초과)
    static bool ParseHeader(const char* headerBuf, UINT32& outJsonSize);

    // ── JSON 간편 추출 함수 (경량 파서) ───────────────────────────────────
    // 목적: 외부 JSON 라이브러리 없이 단순한 key-value를 추출합니다.
    //       중첩 구조나 배열은 지원하지 않으며, 단순 필드용입니다.
    //       (실무에서는 nlohmann/json 같은 라이브러리를 사용하지만,
    //        학습 목적으로 직접 구현합니다.)

    // ExtractString: JSON에서 문자열 값 추출
    // 예) {"result":"NG"} 에서 ExtractString(json, "result") → "NG"
    static CStringA ExtractString(const CStringA& json, const CStringA& key);

    // ExtractInt: JSON에서 정수 값 추출
    // 예) {"station_id":1} 에서 ExtractInt(json, "station_id") → 1
    static int ExtractInt(const CStringA& json, const CStringA& key);

    // ExtractDouble: JSON에서 실수 값 추출
    // 예) {"score":0.87} 에서 ExtractDouble(json, "score") → 0.87
    static double ExtractDouble(const CStringA& json, const CStringA& key);

    // ── 요청 메시지 빌더 (각 프로토콜별 JSON 생성) ────────────────────────

    // BuildLoginReq: 로그인 요청 JSON 생성 (프로토콜 100)
    // 파라미터:
    //   username — 사용자 이름 (예: "admin01")
    //   password — 비밀번호 (평문; 실무에서는 해시 처리 필요)
    // 반환값: JSON 문자열
    static CString BuildLoginReq(const CString& username, const CString& password);

    // BuildRegisterReq: 회원가입 요청 JSON 생성 (프로토콜 104)
    static CString BuildRegisterReq(const CString& username, const CString& password,
                                     const CString& employeeId, const CString& role);

    // BuildLogoutReq: 로그아웃 요청 JSON 생성 (프로토콜 102)
    static CString BuildLogoutReq(const CString& username);

    // BuildInspectHistoryReq: 검사 이력 조회 요청 JSON (프로토콜 114)
    // 파라미터:
    //   stationFilter — 0=전체, 1=입고, 2=조립
    //   dateFrom      — 시작 날짜 (예: "2026-04-15")
    //   dateTo        — 종료 날짜 (예: "2026-04-16")
    //   limit         — 최대 조회 건수
    static CString BuildInspectHistoryReq(
        int stationFilter,
        const CString& dateFrom,
        const CString& dateTo,
        int limit = 100);

    // BuildStatsReq: 통계 데이터 요청 JSON (프로토콜 130)
    static CString BuildStatsReq(
        int stationFilter,
        const CString& dateFrom,
        const CString& dateTo);

    // BuildModelListReq: 모델 목록 요청 JSON (프로토콜 150)
    static CString BuildModelListReq();

    // BuildRetrainReq: 재학습 요청 JSON (프로토콜 152)
    // 파라미터:
    //   stationId   — 대상 스테이션 (1 또는 2)
    //   modelType   — 모델 종류 ("PatchCore" 또는 "YOLO11")
    //   productName — 제품명 (예: "samdasoo_500ml")
    //   imageCount  — 업로드된 학습 이미지 수
    static CString BuildRetrainReq(
        int stationId,
        const CString& modelType,
        const CString& productName,
        int imageCount);

    // BuildAck: 범용 ACK 응답 JSON 생성
    // 파라미터:
    //   ackProtocolNo — ACK 프로토콜 번호 (예: 111)
    //   inspectionId  — 대상 검사 ID (수신한 패킷의 ID 그대로 반환)
    static CString BuildAck(int ackProtocolNo, const CString& inspectionId);

private:
    // GetTimestamp: 현재 시각을 ISO8601 형식 문자열로 반환
    // 예) "2026-04-16T14:30:00"
    static CStringA GetTimestamp();

    // GenerateRequestId: 고유한 요청 ID 생성
    // 예) "req-00000001"
    // 목적: 요청과 응답을 매칭하기 위한 식별자
    static CStringA GenerateRequestId();

    // m_requestSeq: 요청 ID 순번 카운터 (매 요청마다 1씩 증가)
    static int m_requestSeq;
};
