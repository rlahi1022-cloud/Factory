// ============================================================================
// connection_pool.cpp — MariaDB 커넥션 풀 구현
// ============================================================================
#include "storage/connection_pool.h"
#include "core/logger.h"

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
    cv_.wait(lock, [this] { return !pool_.empty() || is_shutdown_; });
    if (is_shutdown_) return nullptr;

    MYSQL* conn = pool_.front();
    pool_.pop();

    // 연결이 끊어졌으면 재연결 시도
    if (mysql_ping(conn) != 0) {
        log_err_db("커넥션 끊어짐 → 재연결 시도");
        mysql_close(conn);
        conn = create_connection();
    }

    return conn;
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

    for (MYSQL* conn : all_conns_) {
        if (conn) mysql_close(conn);
    }
    all_conns_.clear();
    while (!pool_.empty()) pool_.pop();
    log_db("커넥션 풀 종료");
}

} // namespace factory
