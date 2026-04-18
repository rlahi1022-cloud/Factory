// ============================================================================
// connection_pool.cpp — MariaDB 커넥션 풀 구현
// ============================================================================
#include "storage/connection_pool.h"
#include "core/logger.h"

#include <chrono>

namespace factory {

ConnectionPool::ConnectionPool(const std::string& host,
                               const std::string& user,
                               const std::string& password,
                               const std::string& schema,
                               unsigned int port,
                               int pool_size)
    : host_(host), user_(user), password_(password),
      schema_(schema), port_(port), pool_size_(pool_size) {
}

ConnectionPool::~ConnectionPool() {
    shutdown();
}

MYSQL* ConnectionPool::create_connection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        log_err_db("mysql_init 실패 (풀)");
        return nullptr;
    }

    my_bool reconnect = 1;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                            password_.c_str(), schema_.c_str(),
                            port_, nullptr, 0)) {
        log_err_db("커넥션 풀 연결 실패 | %s", mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }

    return conn;
}

bool ConnectionPool::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < pool_size_; ++i) {
        MYSQL* conn = create_connection();
        if (!conn) {
            log_err_db("커넥션 풀 초기화 실패 | %d/%d", i, pool_size_);
            return false;
        }
        pool_.push(conn);
        all_conns_.push_back(conn);
    }
    log_db("커넥션 풀 초기화 완료 | %d개 연결 | %s:%d/%s",
           pool_size_, host_.c_str(), port_, schema_.c_str());
    return true;
}

MYSQL* ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    // 5초 타임아웃 — 무한 대기 방지 (데드락 등 비정상 상황 대응)
    if (!cv_.wait_for(lock, std::chrono::seconds(5),
                      [this] { return !pool_.empty() || is_shutdown_; })) {
        log_err_db("커넥션 풀 acquire 타임아웃 (5초)");
        return nullptr;
    }
    if (is_shutdown_) return nullptr;

    MYSQL* old_conn = pool_.front();
    pool_.pop();

    // 연결이 끊어졌으면 재연결 시도
    if (mysql_ping(old_conn) != 0) {
        log_err_db("커넥션 끊어짐 → 재연결 시도");
        mysql_close(old_conn);

        MYSQL* new_conn = create_connection();
        // all_conns_에서 old 포인터를 new로 교체 → shutdown 시 double-close 방지
        for (auto& c : all_conns_) {
            if (c == old_conn) {
                c = new_conn;  // new_conn이 nullptr이어도 교체 (double-close 회피가 목적)
                break;
            }
        }

        if (!new_conn) {
            log_err_db("재연결 실패 — 풀 크기 축소");
            return nullptr;
        }
        return new_conn;
    }

    return old_conn;
}

void ConnectionPool::release(MYSQL* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}

void ConnectionPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_shutdown_ = true;
    }
    cv_.notify_all();

    // 재연결 실패로 nullptr이 들어간 슬롯이 있을 수 있음 → 체크 후 close
    for (MYSQL* conn : all_conns_) {
        if (conn) mysql_close(conn);
    }
    all_conns_.clear();
    while (!pool_.empty()) pool_.pop();
    log_db("커넥션 풀 종료");
}

} // namespace factory
