// ============================================================================
// ClientConfig.cpp — config.json 로더 (MFC)
// ============================================================================
#include "pch.h"
#include "ClientConfig.h"
#include "ClientProtocol.h"

#include <fstream>
#include <sstream>

namespace factory_client {

// 정적 멤버 초기화 (기본값은 ClientProtocol.h의 DEFAULT 값으로)
bool    ClientConfig::s_loaded    = false;
CString ClientConfig::s_serverIp  = DEFAULT_SERVER_IP;
UINT16  ClientConfig::s_serverPort = GUI_PORT;
CString ClientConfig::s_sourcePath;

// ── 경로 탐색 후보 ──
// 실행파일 기준 config.json 탐색 경로 (Visual Studio 디버그 기준)
// Factory_UI_CL.exe 위치:
//   Debug/Release:  client/Factory_UI_CL/x64/Debug/
//   프로젝트 루트까지: ../../../
//   config 위치:    Factory/config/config.json
static const wchar_t* kSearchPaths[] = {
    L"..\\..\\..\\..\\config\\config.json",   // VS 빌드: x64/Debug/ → Factory/config/
    L"..\\..\\..\\config\\config.json",       // 프로젝트 루트 3단계 상위
    L"..\\..\\config\\config.json",           // 2단계 상위
    L"..\\config\\config.json",               // 1단계 상위
    L"config\\config.json",                   // 같은 경로에 config/
};

bool ClientConfig::Load()
{
    // 1) 실행파일 위치 획득
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    CString exeDir = exePath;
    int lastSlash = exeDir.ReverseFind(L'\\');
    if (lastSlash > 0) exeDir = exeDir.Left(lastSlash + 1);

    // 2) 후보 경로 순차 시도
    CString foundPath;
    for (const auto* rel : kSearchPaths) {
        CString candidate = exeDir + rel;
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
            foundPath = candidate;
            break;
        }
    }
    // 현재 작업 디렉터리도 시도
    if (foundPath.IsEmpty()) {
        for (const auto* rel : kSearchPaths) {
            if (GetFileAttributesW(rel) != INVALID_FILE_ATTRIBUTES) {
                foundPath = rel;
                break;
            }
        }
    }

    if (foundPath.IsEmpty()) {
        TRACE(L"[ClientConfig] config.json을 찾지 못함 — 기본값 사용\n");
        return false;
    }

    // 3) 파일 읽기
    CStringA pathA(foundPath);
    std::ifstream ifs((LPCSTR)pathA);
    if (!ifs) {
        TRACE(L"[ClientConfig] 파일 열기 실패: %s\n", (LPCTSTR)foundPath);
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    // 4) 파싱
    if (!ParseJson(content)) {
        TRACE(L"[ClientConfig] JSON 파싱 실패\n");
        return false;
    }

    s_sourcePath = foundPath;
    s_loaded = true;
    TRACE(L"[ClientConfig] 로드 완료: %s (IP=%s, Port=%d)\n",
          (LPCTSTR)foundPath, (LPCTSTR)s_serverIp, s_serverPort);
    return true;
}

bool ClientConfig::ParseJson(const std::string& content)
{
    // 클라이언트는 2개 값만 사용하므로 최소 파서로 충분
    std::string ip = ExtractString(content, "default_server_ip");
    if (ip.empty()) ip = ExtractString(content, "main_server_host");  // 폴백
    if (!ip.empty()) {
        s_serverIp = CString(ip.c_str());
    }

    int port = ExtractInt(content, "main_server_gui_port");
    if (port > 0 && port < 65536) {
        s_serverPort = static_cast<UINT16>(port);
    }
    return true;
}

// "key":"value" 패턴 검색 (단순 파서)
std::string ClientConfig::ExtractString(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto fq = json.find('"', colon + 1);
    if (fq == std::string::npos) return "";
    auto lq = json.find('"', fq + 1);
    if (lq == std::string::npos) return "";
    return json.substr(fq + 1, lq - fq - 1);
}

// "key":123 패턴 검색
int ClientConfig::ExtractInt(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return 0;
    return std::atoi(json.c_str() + colon + 1);
}

CString ClientConfig::GetServerIp()    { return s_serverIp; }
UINT16  ClientConfig::GetServerPort()  { return s_serverPort; }
CString ClientConfig::GetSourcePath()  { return s_sourcePath; }
bool    ClientConfig::IsLoaded()       { return s_loaded; }

} // namespace factory_client
