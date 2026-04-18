// ============================================================================
// ip_filter.h — 네트워크 IP 화이트리스트
// ============================================================================
// 목적:
//   외부망 접근을 차단하고 내부망에서만 서버에 접속 가능하도록 제한한다.
//
// 허용 대역:
//   - 127.0.0.1        : 로컬호스트
//   - 10.0.0.0/8       : 사설 A 클래스
//   - 172.16.0.0/12    : 사설 B 클래스 (172.16~31)
//   - 192.168.0.0/16   : 사설 C 클래스
// ============================================================================
#pragma once

#include <cstdlib>
#include <string>

namespace factory::security {

/// 내부망 IP인지 검증
/// @param ip "192.168.1.10" 형식의 점으로 구분된 IPv4
inline bool is_allowed_ip(const std::string& ip) {
    // 로컬호스트
    if (ip == "127.0.0.1") return true;

    // 10.x.x.x
    if (ip.rfind("10.", 0) == 0) return true;

    // 192.168.x.x
    if (ip.rfind("192.168.", 0) == 0) return true;

    // 172.16.x.x ~ 172.31.x.x
    if (ip.rfind("172.", 0) == 0) {
        int second = std::atoi(ip.c_str() + 4);
        if (second >= 16 && second <= 31) return true;
    }

    return false;
}

} // namespace factory::security
