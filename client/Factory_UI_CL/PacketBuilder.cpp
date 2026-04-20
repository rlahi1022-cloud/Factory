// ============================================================================
// PacketBuilder.cpp
// ============================================================================
// 목적:
//   PacketBuilder.h에 선언된 패킷 조립/파싱/JSON 빌더 함수들의 구현부입니다.
//   TCP 프로토콜의 [4바이트 헤더] + [JSON] 포맷을 직접 다루며,
//   외부 라이브러리 없이 단순한 JSON 생성 및 파싱을 수행합니다.
// ============================================================================

#include "pch.h"
#include "PacketBuilder.h"

// ── 정적 멤버 초기화 ─────────────────────────────────────────────────────
// m_requestSeq: 요청 ID 순번. 프로그램 시작 시 0부터 시작하여 매 요청마다 +1.
int CPacketBuilder::m_requestSeq = 0;

// ============================================================================
// BuildPacket — JSON 문자열을 TCP 패킷으로 변환
// ============================================================================
// 동작:
//   1) CString(유니코드)을 UTF-8 바이트 배열로 변환
//   2) UTF-8 바이트의 길이를 4바이트 Big-Endian 헤더로 작성
//   3) [헤더 4바이트] + [JSON UTF-8 바이트]를 하나의 vector로 합침
std::vector<char> CPacketBuilder::BuildPacket(const CString& jsonBody)
{
    // ── 유니코드 → UTF-8 변환 ──
    // MFC의 CString은 유니코드(wchar_t)이므로 네트워크 전송을 위해 UTF-8로 변환합니다.
    // WideCharToMultiByte: Windows API로, 와이드 문자를 멀티바이트(UTF-8)로 변환합니다.
    // CP_UTF8 = UTF-8 코드 페이지
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
        jsonBody.GetString(), jsonBody.GetLength(),  // 입력: 유니코드 문자열
        nullptr, 0,                                   // 출력 버퍼 없이 필요한 크기만 계산
        nullptr, nullptr);

    // UTF-8 바이트를 담을 버퍼 생성
    std::vector<char> utf8(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0,
        jsonBody.GetString(), jsonBody.GetLength(),  // 입력
        utf8.data(), utf8Len,                         // 출력: UTF-8 바이트 배열에 기록
        nullptr, nullptr);

    // ── Big-Endian 헤더 생성 ──
    // JSON 크기를 4바이트 Big-Endian으로 인코딩합니다.
    // Big-Endian: 큰 자리(상위 바이트)부터 저장
    // 예) 크기 256(=0x100) → [0x00, 0x00, 0x01, 0x00]
    UINT32 jsonSize = static_cast<UINT32>(utf8Len);
    char header[4];
    header[0] = static_cast<char>((jsonSize >> 24) & 0xFF);  // 최상위 바이트
    header[1] = static_cast<char>((jsonSize >> 16) & 0xFF);
    header[2] = static_cast<char>((jsonSize >> 8) & 0xFF);
    header[3] = static_cast<char>(jsonSize & 0xFF);            // 최하위 바이트

    // ── 최종 패킷 조립 ──
    // 헤더(4바이트) + JSON(utf8Len바이트) = 전체 패킷
    std::vector<char> packet;
    packet.reserve(4 + utf8Len);                    // 메모리 미리 확보 (성능 최적화)
    packet.insert(packet.end(), header, header + 4); // 헤더 삽입
    packet.insert(packet.end(), utf8.begin(), utf8.end()); // JSON 본문 삽입

    return packet;
}

// ============================================================================
// BuildPacketWithImage — JSON + 이미지 바이너리를 패킷으로 변환
// ============================================================================
// 주의: 헤더의 4바이트는 JSON 크기만 담습니다 (이미지 크기 아님).
//       이미지 크기는 JSON 내부의 "image_size" 필드에 기록되어 있습니다.
std::vector<char> CPacketBuilder::BuildPacketWithImage(
    const CString& jsonBody, const char* imageData, int imageSize)
{
    // JSON 부분 패킷을 먼저 생성
    std::vector<char> packet = BuildPacket(jsonBody);

    // 이미지 바이너리를 패킷 뒤에 추가
    if (imageData && imageSize > 0) {
        packet.insert(packet.end(), imageData, imageData + imageSize);
    }

    return packet;
}

// ============================================================================
// ParseHeader — 4바이트 Big-Endian 헤더에서 JSON 크기 추출
// ============================================================================
bool CPacketBuilder::ParseHeader(const char* headerBuf, UINT32& outJsonSize)
{
    // 4바이트를 Big-Endian으로 해석하여 정수로 변환합니다.
    // 각 바이트를 적절한 비트 위치로 시프트(shift)한 후 OR 연산으로 합칩니다.
    // 0xFF 마스크: char가 signed일 수 있으므로 부호 확장 방지
    outJsonSize = (static_cast<UINT32>(headerBuf[0] & 0xFF) << 24)
                | (static_cast<UINT32>(headerBuf[1] & 0xFF) << 16)
                | (static_cast<UINT32>(headerBuf[2] & 0xFF) << 8)
                | (static_cast<UINT32>(headerBuf[3] & 0xFF));

    // 유효성 검사: 크기가 0이거나 64KB를 초과하면 비정상 패킷
    if (outJsonSize == 0 || outJsonSize > 64 * 1024) {
        return false;
    }
    return true;
}

// ============================================================================
// JSON 간편 추출 함수들
// ============================================================================
// 주의: 이 함수들은 간단한 1-depth JSON만 처리합니다.
//       중첩 객체, 배열, 이스케이프 문자 등은 지원하지 않습니다.
//       실무 프로젝트에서는 nlohmann/json 등의 라이브러리를 사용하세요.

// ExtractString: JSON에서 문자열 값 추출
// 동작: "key":"value" 패턴을 찾아서 value 부분을 반환
CStringA CPacketBuilder::ExtractString(const CStringA& json, const CStringA& key)
{
    if (json.IsEmpty() || key.IsEmpty()) return "";

    // "key" 형태의 검색 문자열 생성
    CStringA needle;
    needle.Format("\"%s\"", (LPCSTR)key);

    int jsonLen = json.GetLength();

    // JSON 안에서 키 위치 찾기
    int pos = json.Find(needle);
    if (pos < 0) return "";

    // ':' (콜론) 찾기 — key: value 구분자
    int colon = json.Find(':', pos + needle.GetLength());
    if (colon < 0 || colon >= jsonLen - 1) return "";

    // value의 시작 따옴표 찾기
    int firstQuote = json.Find('"', colon + 1);
    if (firstQuote < 0 || firstQuote >= jsonLen - 1) return "";

    // value의 끝 따옴표 찾기 (이스케이프된 따옴표 건너뛰기)
    int lastQuote = -1;
    for (int i = firstQuote + 1; i < jsonLen; ++i) {
        if (json[i] == '"' && (i == 0 || json[i - 1] != '\\')) {
            lastQuote = i;
            break;
        }
    }
    if (lastQuote < 0) return "";

    // 길이 검증
    int len = lastQuote - firstQuote - 1;
    if (len < 0 || len > 4096) return "";  // 비정상 크기 차단

    return json.Mid(firstQuote + 1, len);
}

// ExtractInt: JSON에서 정수 값 추출
int CPacketBuilder::ExtractInt(const CStringA& json, const CStringA& key)
{
    if (json.IsEmpty() || key.IsEmpty()) return 0;

    CStringA needle;
    needle.Format("\"%s\"", (LPCSTR)key);

    int pos = json.Find(needle);
    if (pos < 0) return 0;

    int colon = json.Find(':', pos + needle.GetLength());
    if (colon < 0 || colon >= json.GetLength() - 1) return 0;

    // strtol로 변환 — atoi보다 안전 (범위 검증 가능)
    char* endptr = nullptr;
    long val = strtol((LPCSTR)json + colon + 1, &endptr, 10);
    if (endptr == (LPCSTR)json + colon + 1) return 0;  // 숫자 없음
    if (val > INT_MAX || val < INT_MIN) return 0;       // 오버플로우
    return static_cast<int>(val);
}

// ExtractDouble: JSON에서 실수 값 추출
double CPacketBuilder::ExtractDouble(const CStringA& json, const CStringA& key)
{
    if (json.IsEmpty() || key.IsEmpty()) return 0.0;

    CStringA needle;
    needle.Format("\"%s\"", (LPCSTR)key);

    int pos = json.Find(needle);
    if (pos < 0) return 0.0;

    int colon = json.Find(':', pos + needle.GetLength());
    if (colon < 0 || colon >= json.GetLength() - 1) return 0.0;

    char* endptr = nullptr;
    double val = strtod((LPCSTR)json + colon + 1, &endptr);
    if (endptr == (LPCSTR)json + colon + 1) return 0.0;  // 숫자 없음
    return val;
}

// ============================================================================
// GetTimestamp — 현재 시각을 ISO8601 형식으로 반환
// ============================================================================
CStringA CPacketBuilder::GetTimestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);  // Windows API: 현재 로컬 시간 가져오기

    CStringA ts;
    // ISO8601 형식: "YYYY-MM-DDTHH:MM:SS"
    ts.Format("%04d-%02d-%02dT%02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return ts;
}

// ============================================================================
// GenerateRequestId — 고유 요청 ID 생성
// ============================================================================
// 목적: 요청-응답 매칭을 위한 고유 식별자
// 형식: "req-00000001", "req-00000002", ...
CStringA CPacketBuilder::GenerateRequestId()
{
    // InterlockedIncrement: 멀티스레드 환경에서 안전하게 +1 (원자적 연산)
    int seq = InterlockedIncrement(reinterpret_cast<volatile LONG*>(&m_requestSeq));
    CStringA id;
    id.Format("req-%08d", seq);
    return id;
}

// ============================================================================
// ExtractBool — JSON의 불리언 값(true/false) 추출
// ============================================================================
// 중요: ExtractString은 따옴표 없는 원시값(true/false/123)을 잘못 파싱하므로
//       boolean 필드는 반드시 이 함수를 사용해야 한다.
//
// 예) "success":true,"username":"admin123"
//   ExtractString("success") → "username" (잘못됨!)
//   ExtractBool("success")   → true (정상)
bool CPacketBuilder::ExtractBool(const CStringA& json, const CStringA& key)
{
    if (json.IsEmpty() || key.IsEmpty()) return false;

    CStringA needle;
    needle.Format("\"%s\"", (LPCSTR)key);

    int pos = json.Find(needle);
    if (pos < 0) return false;

    int colon = json.Find(':', pos + needle.GetLength());
    if (colon < 0) return false;

    // 콜론 다음부터 원시값 확인 (공백 스킵)
    int i = colon + 1;
    while (i < json.GetLength() &&
           (json[i] == ' ' || json[i] == '\t')) ++i;

    // "true" 시작이면 true, 그 외는 false
    return (i + 3 < json.GetLength()) &&
           json[i]   == 't' &&
           json[i+1] == 'r' &&
           json[i+2] == 'u' &&
           json[i+3] == 'e';
}

// ============================================================================
// Utf8ToWide — UTF-8 바이트 → Unicode CString 변환
// ============================================================================
// 목적:
//   서버가 보낸 UTF-8 JSON에서 추출한 CStringA(ANSI)를
//   Unicode CString으로 변환해 MFC 컨트롤에 올바르게 표시한다.
//
// 문제:
//   CString(CStringA) 생성자는 CP949(한국어 Windows 기본 ANSI)로 해석 →
//   UTF-8 바이트를 잘못 매핑하여 "溢웴헿???" 같은 깨진 문자 발생.
//
// 해결:
//   MultiByteToWideChar(CP_UTF8, ...)로 UTF-8 → UTF-16 명시적 변환.
CString CPacketBuilder::Utf8ToWide(const CStringA& utf8)
{
    if (utf8.IsEmpty()) return CString();

    int wlen = MultiByteToWideChar(CP_UTF8, 0,
        (LPCSTR)utf8, utf8.GetLength(), nullptr, 0);
    if (wlen <= 0) return CString((LPCSTR)utf8);  // 변환 실패 시 폴백

    CString result;
    MultiByteToWideChar(CP_UTF8, 0,
        (LPCSTR)utf8, utf8.GetLength(),
        result.GetBuffer(wlen), wlen);
    result.ReleaseBuffer(wlen);
    return result;
}

// ExtractStringW: ExtractString + UTF-8 변환 (편의 함수)
CString CPacketBuilder::ExtractStringW(const CStringA& json, const CStringA& key)
{
    return Utf8ToWide(ExtractString(json, key));
}

// ============================================================================
// JSON 이스케이프 — 서버 측 security::escape_json과 동일 기준으로 정렬
//   처리: " \ \n \r \t \b \f + 제어문자(0x00~0x1F) → \uXXXX
// ============================================================================
static CStringA EscapeJson(const CStringA& s)
{
    CStringA out;
    out.Preallocate(s.GetLength() + 16);
    for (int i = 0; i < s.GetLength(); ++i) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    CStringA hex;
                    hex.Format("\\u%04x", static_cast<unsigned char>(c));
                    out += hex;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

// ============================================================================
// 요청 메시지 빌더 — 각 프로토콜별 JSON 문자열 생성
// ============================================================================

// BuildLoginReq: 로그인 요청 JSON (프로토콜 100)
// 서버로 전송할 인증 정보를 JSON 형태로 조립합니다.
// 참고: 비밀번호는 내부 폐쇄망(10.10.10.x)에서만 전송되며,
//       서버 측에서 bcrypt 해싱 처리. 실무에서는 TLS 적용 필요.
CString CPacketBuilder::BuildLoginReq(const CString& username, const CString& password)
{
    CStringA ts = GetTimestamp();
    CStringA reqId = GenerateRequestId();

    // UTF-8 변환 + JSON 이스케이프 (injection 방지)
    CStringA userA = EscapeJson(CStringA(username));
    CStringA passA = EscapeJson(CStringA(password));

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"username\":\"%s\","
        "\"password\":\"%s\","
        "\"timestamp\":\"%s\""
        "}",
        factory_client::LOGIN_REQ,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)reqId,
        (LPCSTR)userA,
        (LPCSTR)passA,
        (LPCSTR)ts);

    return CString(json);  // CStringA → CString(유니코드) 변환하여 반환
}

// BuildRegisterReq: 회원가입 요청 JSON (프로토콜 104)
CString CPacketBuilder::BuildRegisterReq(const CString& username, const CString& password,
                                          const CString& employeeId, const CString& role)
{
    CStringA ts = GetTimestamp();
    CStringA reqId = GenerateRequestId();
    CStringA userA = EscapeJson(CStringA(username));
    CStringA passA = EscapeJson(CStringA(password));
    CStringA empA  = EscapeJson(CStringA(employeeId));
    CStringA roleA = EscapeJson(CStringA(role));

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"username\":\"%s\","
        "\"password\":\"%s\","
        "\"employee_id\":\"%s\","
        "\"role\":\"%s\","
        "\"timestamp\":\"%s\""
        "}",
        factory_client::REGISTER_REQ,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)reqId,
        (LPCSTR)userA,
        (LPCSTR)passA,
        (LPCSTR)empA,
        (LPCSTR)roleA,
        (LPCSTR)ts);

    return CString(json);
}

// BuildLogoutReq: 로그아웃 요청 JSON (프로토콜 102)
CString CPacketBuilder::BuildLogoutReq(const CString& username)
{
    CStringA ts = GetTimestamp();
    CStringA userA(username);

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"username\":\"%s\","
        "\"timestamp\":\"%s\""
        "}",
        factory_client::LOGOUT_REQ,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)userA,
        (LPCSTR)ts);

    return CString(json);
}

// BuildInspectHistoryReq: 검사 이력 조회 요청 JSON (프로토콜 114)
CString CPacketBuilder::BuildInspectHistoryReq(
    int stationFilter, const CString& dateFrom, const CString& dateTo, int limit)
{
    CStringA ts = GetTimestamp();
    CStringA reqId = GenerateRequestId();
    CStringA fromA(dateFrom);
    CStringA toA(dateTo);

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"station_filter\":%d,"          // 0=전체, 1=입고, 2=조립
        "\"date_from\":\"%s\","           // 조회 시작일
        "\"date_to\":\"%s\","             // 조회 종료일
        "\"limit\":%d,"                   // 최대 조회 건수
        "\"timestamp\":\"%s\""
        "}",
        factory_client::INSPECT_HISTORY_REQ,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)reqId,
        stationFilter,
        (LPCSTR)fromA,
        (LPCSTR)toA,
        limit,
        (LPCSTR)ts);

    return CString(json);
}

// BuildStatsReq: 통계 데이터 요청 JSON (프로토콜 130)
CString CPacketBuilder::BuildStatsReq(
    int stationFilter, const CString& dateFrom, const CString& dateTo)
{
    CStringA ts = GetTimestamp();
    CStringA reqId = GenerateRequestId();
    CStringA fromA(dateFrom);
    CStringA toA(dateTo);

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"station_filter\":%d,"
        "\"date_from\":\"%s\","
        "\"date_to\":\"%s\","
        "\"timestamp\":\"%s\""
        "}",
        factory_client::STATS_REQ,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)reqId,
        stationFilter,
        (LPCSTR)fromA,
        (LPCSTR)toA,
        (LPCSTR)ts);

    return CString(json);
}

// BuildModelListReq: 모델 목록 요청 JSON (프로토콜 150)
CString CPacketBuilder::BuildModelListReq()
{
    CStringA ts = GetTimestamp();
    CStringA reqId = GenerateRequestId();

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"timestamp\":\"%s\""
        "}",
        factory_client::MODEL_LIST_REQ,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)reqId,
        (LPCSTR)ts);

    return CString(json);
}

// BuildRetrainReq: 재학습 요청 JSON (프로토콜 152)
CString CPacketBuilder::BuildRetrainReq(
    int stationId, const CString& modelType,
    const CString& productName, int imageCount)
{
    CStringA ts = GetTimestamp();
    CStringA reqId = GenerateRequestId();
    CStringA typeA(modelType);
    CStringA prodA(productName);

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"station_id\":%d,"
        "\"model_type\":\"%s\","          // "PatchCore" 또는 "YOLO11"
        "\"product_name\":\"%s\","        // 제품명
        "\"image_count\":%d,"             // 업로드된 이미지 수
        "\"timestamp\":\"%s\""
        "}",
        factory_client::RETRAIN_REQ,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)reqId,
        stationId,
        (LPCSTR)typeA,
        (LPCSTR)prodA,
        imageCount,
        (LPCSTR)ts);

    return CString(json);
}

// BuildAck: ACK 응답 JSON 생성
// 서버로부터 NG 푸시 등을 수신했을 때, "잘 받았다"고 알려주는 응답입니다.
CString CPacketBuilder::BuildAck(int ackProtocolNo, const CString& inspectionId)
{
    CStringA ts = GetTimestamp();
    CStringA idA(inspectionId);

    CStringA json;
    json.Format(
        "{"
        "\"protocol_no\":%d,"
        "\"protocol_version\":\"%s\","
        "\"inspection_id\":\"%s\","
        "\"timestamp\":\"%s\""
        "}",
        ackProtocolNo,
        factory_client::PROTOCOL_VERSION,
        (LPCSTR)idA,
        (LPCSTR)ts);

    return CString(json);
}
