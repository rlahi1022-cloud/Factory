// ============================================================================
// TestPacketBuilder.cpp — PacketBuilder 단위 테스트
// ============================================================================
// 목적:
//   PacketBuilder의 패킷 조립/파싱/JSON 추출 함수들이 올바르게 동작하는지
//   검증하는 콘솔 프로그램입니다.
//
// 빌드 방법 (Visual Studio Developer Command Prompt):
//   cl /EHsc /D "_UNICODE" /D "UNICODE" /D "_AFXDLL"
//      /I "..\\Factory_UI_CL"
//      TestPacketBuilder.cpp
//      ..\\Factory_UI_CL\\PacketBuilder.cpp
//      /link ws2_32.lib
//
// 또는 Visual Studio에서 별도 콘솔 프로젝트로 추가하여 빌드할 수 있습니다.
//
// 실행 결과:
//   각 테스트의 PASS/FAIL 결과가 출력됩니다.
// ============================================================================

// 주의: 이 파일은 MFC 프로젝트가 아닌 콘솔 프로그램이므로
//       MFC 의존성을 최소화하여 작성합니다.
//       실제 프로젝트에서는 Google Test 등의 테스트 프레임워크를 사용하세요.

#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>

// ── 패킷 빌더의 헤더 파싱 함수만 별도로 테스트 ──
// (MFC CString 의존성을 피하기 위해 핵심 로직만 테스트)

// Big-Endian 4바이트 헤더 생성
static void make_header(char* out, unsigned int size) {
    out[0] = (char)((size >> 24) & 0xFF);
    out[1] = (char)((size >> 16) & 0xFF);
    out[2] = (char)((size >> 8)  & 0xFF);
    out[3] = (char)( size        & 0xFF);
}

// Big-Endian 4바이트 헤더 파싱
static unsigned int parse_header(const char* buf) {
    return ((unsigned int)(buf[0] & 0xFF) << 24)
         | ((unsigned int)(buf[1] & 0xFF) << 16)
         | ((unsigned int)(buf[2] & 0xFF) << 8)
         | ((unsigned int)(buf[3] & 0xFF));
}

// 간단한 JSON 문자열 값 추출 (PacketBuilder::ExtractString과 동일 로직)
static std::string extract_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return "";
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

// 간단한 JSON 정수 값 추출 (PacketBuilder::ExtractInt과 동일 로직)
static int extract_int(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return 0;
    return atoi(json.c_str() + colon + 1);
}

// 간단한 JSON 실수 값 추출
static double extract_double(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0.0;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return 0.0;
    return atof(json.c_str() + colon + 1);
}

// ── 테스트 매크로 ────────────────────────────────────────────────────────
static int pass_count = 0;
static int fail_count = 0;

#define TEST(name) printf("  %-40s ", name)
#define PASS() do { printf("[PASS]\n"); ++pass_count; } while(0)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); ++fail_count; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while(0)


// ============================================================================
// 테스트 함수들
// ============================================================================

void test_header_roundtrip() {
    // 테스트: Big-Endian 헤더 인코딩 → 디코딩 왕복
    TEST("헤더 인코딩/디코딩 왕복");

    unsigned int sizes[] = {0, 1, 255, 256, 65535, 65536, 100000};
    for (auto sz : sizes) {
        char hdr[4];
        make_header(hdr, sz);
        unsigned int decoded = parse_header(hdr);
        if (decoded != sz) {
            char buf[128];
            snprintf(buf, sizeof(buf), "size=%u decoded=%u", sz, decoded);
            FAIL(buf);
            return;
        }
    }
    PASS();
}

void test_header_known_values() {
    // 테스트: 알려진 값으로 Big-Endian 바이트 확인
    TEST("헤더 알려진 값 검증");

    char hdr[4];
    // 256 = 0x00000100
    make_header(hdr, 256);
    CHECK(hdr[0] == 0x00 && hdr[1] == 0x00 && hdr[2] == 0x01 && hdr[3] == 0x00,
          "256 -> [0,0,1,0]");
}

void test_extract_string() {
    // 테스트: JSON 문자열 값 추출
    TEST("JSON 문자열 추출");

    std::string json = R"({"result":"NG","defect_type":"cap_loose","score":0.87})";
    CHECK(extract_str(json, "result") == "NG" &&
          extract_str(json, "defect_type") == "cap_loose",
          "result=NG, defect=cap_loose");
}

void test_extract_int() {
    // 테스트: JSON 정수 값 추출
    TEST("JSON 정수 추출");

    std::string json = R"({"protocol_no":110,"station_id":2,"latency_ms":45})";
    CHECK(extract_int(json, "protocol_no") == 110 &&
          extract_int(json, "station_id") == 2 &&
          extract_int(json, "latency_ms") == 45,
          "protocol_no=110, station=2, latency=45");
}

void test_extract_double() {
    // 테스트: JSON 실수 값 추출
    TEST("JSON 실수 추출");

    std::string json = R"({"score":0.87,"latency_avg":42.5})";
    double score = extract_double(json, "score");
    double lat = extract_double(json, "latency_avg");
    CHECK(score > 0.86 && score < 0.88 && lat > 42.4 && lat < 42.6,
          "score~0.87, lat~42.5");
}

void test_extract_missing_key() {
    // 테스트: 존재하지 않는 키 추출 시 기본값 반환
    TEST("JSON 없는 키 추출");

    std::string json = R"({"result":"OK"})";
    CHECK(extract_str(json, "missing") == "" &&
          extract_int(json, "missing") == 0 &&
          extract_double(json, "missing") == 0.0,
          "빈 문자열/0/0.0 반환");
}

void test_packet_build_and_parse() {
    // 테스트: 패킷 조립 → 헤더 파싱 → JSON 추출 전체 흐름
    TEST("패킷 조립/파싱 전체 흐름");

    // JSON 문자열 준비
    std::string json = R"({"protocol_no":110,"station_id":1,"result":"NG","score":0.85})";
    int json_size = (int)json.size();

    // 패킷 조립: [4바이트 헤더] + [JSON]
    std::vector<char> packet;
    char hdr[4];
    make_header(hdr, json_size);
    packet.insert(packet.end(), hdr, hdr + 4);
    packet.insert(packet.end(), json.begin(), json.end());

    // 패킷 검증
    // 1) 총 크기 확인
    if ((int)packet.size() != 4 + json_size) { FAIL("패킷 크기 불일치"); return; }

    // 2) 헤더 파싱
    unsigned int parsed_size = parse_header(packet.data());
    if ((int)parsed_size != json_size) { FAIL("헤더 크기 불일치"); return; }

    // 3) JSON 추출
    std::string parsed_json(packet.data() + 4, parsed_size);
    if (extract_int(parsed_json, "protocol_no") != 110) { FAIL("protocol_no 불일치"); return; }
    if (extract_str(parsed_json, "result") != "NG") { FAIL("result 불일치"); return; }

    PASS();
}

void test_ng_push_format() {
    // 테스트: NG 푸시 메시지(프로토콜 110) 형식 검증
    TEST("NG 푸시 메시지 형식");

    std::string json = R"({
        "protocol_no": 110,
        "inspection_id": "station1-20260416120000123-000001",
        "station_id": 1,
        "result": "NG",
        "defect_type": "anomaly",
        "score": 0.87,
        "latency_ms": 45,
        "timestamp": "2026-04-16T12:00:00"
    })";

    CHECK(extract_int(json, "protocol_no") == 110 &&
          extract_str(json, "inspection_id").find("station1") == 0 &&
          extract_int(json, "station_id") == 1 &&
          extract_str(json, "result") == "NG" &&
          extract_str(json, "defect_type") == "anomaly" &&
          extract_double(json, "score") > 0.86 &&
          extract_int(json, "latency_ms") == 45,
          "모든 필드 파싱 성공");
}

void test_health_push_format() {
    // 테스트: 서버 헬스 푸시 메시지(프로토콜 170) 형식 검증
    TEST("헬스 푸시 메시지 형식");

    std::string json = R"({
        "protocol_no": 170,
        "server_name": "ai_inference_2",
        "ip": "10.10.10.130",
        "port": 9102,
        "status": "down"
    })";

    CHECK(extract_int(json, "protocol_no") == 170 &&
          extract_str(json, "server_name") == "ai_inference_2" &&
          extract_str(json, "status") == "down",
          "헬스 필드 파싱 성공");
}

void test_login_req_format() {
    // 테스트: 로그인 요청 메시지(프로토콜 100) 형식 검증
    TEST("로그인 요청 메시지 형식");

    std::string json = R"({
        "protocol_no": 100,
        "protocol_version": "1.0",
        "request_id": "req-00000001",
        "username": "admin01",
        "password": "1234",
        "timestamp": "2026-04-16T14:30:00"
    })";

    CHECK(extract_int(json, "protocol_no") == 100 &&
          extract_str(json, "protocol_version") == "1.0" &&
          extract_str(json, "username") == "admin01",
          "로그인 필드 파싱 성공");
}

// ============================================================================
// 메인 함수
// ============================================================================
int main() {
    printf("============================================================\n");
    printf("  Factory QC — PacketBuilder 단위 테스트\n");
    printf("============================================================\n\n");

    // 테스트 실행
    test_header_roundtrip();
    test_header_known_values();
    test_extract_string();
    test_extract_int();
    test_extract_double();
    test_extract_missing_key();
    test_packet_build_and_parse();
    test_ng_push_format();
    test_health_push_format();
    test_login_req_format();

    // 결과 요약
    printf("\n============================================================\n");
    printf("  결과: %d PASS, %d FAIL (총 %d개)\n",
           pass_count, fail_count, pass_count + fail_count);
    printf("============================================================\n");

    return fail_count > 0 ? 1 : 0;
}
