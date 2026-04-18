// ============================================================================
// config.h — JSON 기반 통합 설정 로더
// ============================================================================
// 목적:
//   프로젝트 루트의 config/config.json을 읽어 모든 설정값을 단일 지점에서
//   제공한다. 하드코딩된 IP/포트/경로/DB 접속정보를 한 곳으로 모은다.
//
// 사용:
//   Config::instance().load("../config/config.json");
//   std::string host = Config::instance().get_str("network.main_server_host");
//   int port        = Config::instance().get_int("network.main_server_ai_port");
//
// JSON 경로 표기:
//   점(.)으로 계층 구분. 예: "network.main_server_host"
//
// 환경변수 오버라이드:
//   get_str()은 설정값을 먼저 확인하고, 특정 환경변수가 있으면 우선 적용.
//   예: TRAIN_HOST가 설정되어 있으면 network.main_server_host보다 우선
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace factory {

class Config {
public:
    /// 싱글톤 인스턴스
    static Config& instance();

    /// JSON 파일 로드 (프로그램 시작 시 1회 호출)
    /// @return 성공 여부
    bool load(const std::string& path);

    /// 문자열 값 조회 (점 구분 키, 예: "network.main_server_host")
    std::string get_str(const std::string& key, const std::string& default_val = "") const;

    /// 정수 값 조회
    int         get_int(const std::string& key, int default_val = 0) const;

    /// 부동소수 값 조회
    double      get_double(const std::string& key, double default_val = 0.0) const;

    /// 문자열 배열 조회 (예: "network.allowed_ip_prefixes")
    std::vector<std::string> get_str_array(const std::string& key) const;

    /// 헬스체크 타겟 전용 구조체 조회
    struct HealthTargetConfig {
        std::string name;
        std::string ip;
        int         port;
    };
    std::vector<HealthTargetConfig> get_health_targets() const;

    /// 로드된 JSON 파일 경로
    const std::string& source_path() const { return path_; }

private:
    Config() = default;

    // 점 구분 키 → flat 키 맵
    // 예: "network" { "port": 9000 } → {"network.port": "9000"}
    std::unordered_map<std::string, std::string>              values_;
    std::unordered_map<std::string, std::vector<std::string>> arrays_;
    std::string                                               path_;

    // 최소 JSON 파서 (nested object + array of primitives + array of objects)
    bool parse(const std::string& json);
};

} // namespace factory
