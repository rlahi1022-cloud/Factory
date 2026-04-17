// ============================================================================
// password_hash.cpp — bcrypt 해시/검증 구현
// ============================================================================
#include "storage/password_hash.h"
#include "core/logger.h"

#include <crypt.h>
#include <cstring>
#include <fstream>
#include <random>

namespace factory {

// bcrypt base64 문자셋 (표준 base64와 다름)
static const char BCRYPT_BASE64[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

std::string PasswordHash::generate_salt() {
    // /dev/urandom에서 16바이트 난수 읽기
    unsigned char random_bytes[16];
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(random_bytes), sizeof(random_bytes));
    } else {
        // fallback: std::random_device
        std::random_device rd;
        for (auto& b : random_bytes) b = static_cast<unsigned char>(rd());
    }

    // bcrypt salt: "$2b$12$" + 22자 base64
    std::string salt = "$2b$12$";
    for (int i = 0; i < 22; ++i) {
        salt += BCRYPT_BASE64[random_bytes[i % 16] % 64];
    }
    return salt;
}

std::string PasswordHash::hash(const std::string& password) {
    std::string salt = generate_salt();

    struct crypt_data data;
    data.initialized = 0;

    const char* result = crypt_r(password.c_str(), salt.c_str(), &data);
    if (!result || strlen(result) < 20) {
        log_err_db("bcrypt 해시 생성 실패");
        return "";
    }

    return std::string(result);
}

bool PasswordHash::verify(const std::string& password, const std::string& stored_hash) {
    if (stored_hash.empty()) return false;

    struct crypt_data data;
    data.initialized = 0;

    // 저장된 해시를 salt로 사용하면 동일한 해시가 나와야 함
    const char* result = crypt_r(password.c_str(), stored_hash.c_str(), &data);
    if (!result) return false;

    return stored_hash == std::string(result);
}

} // namespace factory
