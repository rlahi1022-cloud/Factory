#pragma once
// ============================================================================
// ClientConfig.h — config/config.json 기반 설정 (MFC)
// ============================================================================
// 프로젝트 루트의 config/config.json에서 클라이언트 관련 설정을 로드한다.
// 앱 시작 시 ClientConfig::Load()를 1회 호출하면 이후 전역에서 Get()으로 접근.
//
// 사용:
//   ClientConfig::Load();  // CWinAppEx::InitInstance()에서 호출
//   CString ip = ClientConfig::GetServerIp();
//   UINT16  port = ClientConfig::GetServerPort();
//
// 경로 탐색 순서:
//   1) 실행파일 경로 기준 ../../config/config.json
//   2) 실행파일 경로 기준 ../config/config.json
//   3) 현재 작업 디렉터리의 config/config.json
//   4) 실패 시 기본값 사용 (Log 경고)
// ============================================================================

#include <afxwin.h>
#include <string>

namespace factory_client {

class ClientConfig {
public:
    /// 설정 파일을 로드한다. 앱 초기화 시 1회 호출.
    /// 실패하면 기본값이 사용되며 DEFAULT_SERVER_IP 등이 유지된다.
    static bool Load();

    /// 메인서버 IP (기본: L"10.10.10.130")
    static CString GetServerIp();

    /// GUI 포트 (기본: 9010)
    static UINT16 GetServerPort();

    /// 로드된 설정 파일 경로
    static CString GetSourcePath();

    /// 설정 로드 성공 여부
    static bool IsLoaded();

private:
    static bool   s_loaded;
    static CString s_serverIp;
    static UINT16  s_serverPort;
    static CString s_sourcePath;

    /// 최소 JSON 파서 — "client.default_server_ip", "network.main_server_gui_port"만 찾음
    static bool ParseJson(const std::string& content);
    static std::string ExtractString(const std::string& json, const std::string& key);
    static int         ExtractInt(const std::string& json, const std::string& key);
};

} // namespace factory_client
