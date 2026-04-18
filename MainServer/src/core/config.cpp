// ============================================================================
// config.cpp — JSON 파서 + Config 싱글톤 구현
// ============================================================================
// 외부 라이브러리 없는 최소 JSON 파서.
// 지원: 중첩 객체, 문자열/숫자/bool/null 값, 문자열 배열, 객체 배열
// 미지원: 중첩 배열, 이스케이프 완전 처리 (\u 등)
//
// 실제 프로젝트 범위(config.json의 형태)에 맞춰 설계됨.
// ============================================================================
#include "core/config.h"
#include "core/logger.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace factory {

Config& Config::instance() {
    static Config inst;
    return inst;
}

// ── JSON 토크나이저 ─────────────────────────────────────────────────────
namespace {

struct Parser {
    const std::string& src;
    std::size_t pos = 0;

    explicit Parser(const std::string& s) : src(s) {}

    void skip_ws() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
    }

    char peek() { skip_ws(); return pos < src.size() ? src[pos] : '\0'; }

    bool consume(char c) {
        skip_ws();
        if (pos < src.size() && src[pos] == c) { ++pos; return true; }
        return false;
    }

    // 문자열 파싱: "..." → 내용 반환
    std::string parse_string() {
        skip_ws();
        if (pos >= src.size() || src[pos] != '"') return "";
        ++pos;
        std::string out;
        while (pos < src.size() && src[pos] != '"') {
            if (src[pos] == '\\' && pos + 1 < src.size()) {
                char n = src[pos + 1];
                switch (n) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    default:   out += n;    break;
                }
                pos += 2;
            } else {
                out += src[pos++];
            }
        }
        if (pos < src.size()) ++pos; // 닫는 "
        return out;
    }

    // 원시값(숫자/bool/null)을 문자열 그대로 추출 — 값 종료까지
    std::string parse_primitive() {
        skip_ws();
        std::string out;
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ',' || c == '}' || c == ']' || std::isspace(static_cast<unsigned char>(c))) break;
            out += c;
            ++pos;
        }
        return out;
    }
};

} // anonymous

// ── JSON 파서 본체 ─────────────────────────────────────────────────────
bool Config::parse(const std::string& json) {
    Parser p(json);
    if (!p.consume('{')) return false;

    // DFS로 중첩 객체 탐색
    struct StackFrame {
        std::string prefix;   // "network.database" 같은 점 구분 경로
        bool is_object;
    };
    std::vector<StackFrame> stack;
    stack.push_back({"", true});

    auto make_key = [&](const std::string& k) -> std::string {
        if (stack.back().prefix.empty()) return k;
        return stack.back().prefix + "." + k;
    };

    while (!stack.empty()) {
        p.skip_ws();

        // 객체 종료
        if (p.peek() == '}') {
            p.consume('}');
            stack.pop_back();
            if (!stack.empty()) p.consume(','); // 부모의 다음 항목 구분자
            continue;
        }

        // 키 파싱
        std::string key = p.parse_string();
        if (key.empty()) return false;
        if (!p.consume(':')) return false;

        std::string full_key = make_key(key);
        p.skip_ws();

        // 값 타입 분기
        if (p.peek() == '{') {
            p.consume('{');
            stack.push_back({full_key, true});
        }
        else if (p.peek() == '[') {
            p.consume('[');
            // 배열: 문자열 배열 OR 객체 배열 결정
            p.skip_ws();
            if (p.peek() == '"') {
                // 문자열 배열
                std::vector<std::string> arr;
                while (p.peek() != ']') {
                    arr.push_back(p.parse_string());
                    if (!p.consume(',')) break;
                }
                p.consume(']');
                arrays_[full_key] = std::move(arr);
            } else if (p.peek() == '{') {
                // 객체 배열 — 각 객체를 "key.N.field" 형식으로 flatten
                int idx = 0;
                while (p.peek() == '{') {
                    p.consume('{');
                    std::string obj_prefix = full_key + "." + std::to_string(idx);
                    while (p.peek() != '}') {
                        std::string subkey = p.parse_string();
                        if (!p.consume(':')) break;
                        std::string subval;
                        if (p.peek() == '"') {
                            subval = p.parse_string();
                        } else {
                            subval = p.parse_primitive();
                        }
                        values_[obj_prefix + "." + subkey] = subval;
                        if (!p.consume(',')) break;
                    }
                    p.consume('}');
                    if (!p.consume(',')) break;
                    idx++;
                }
                values_[full_key + ".count"] = std::to_string(idx);
                p.consume(']');
            } else {
                // 빈 배열 또는 숫자 배열 — 현재 미사용
                while (p.pos < p.src.size() && p.src[p.pos] != ']') ++p.pos;
                p.consume(']');
            }
            if (!p.consume(',')) {
                // 쉼표 없이 끝나면 객체 닫기 대기
            }
        }
        else if (p.peek() == '"') {
            values_[full_key] = p.parse_string();
            p.consume(',');
        }
        else {
            values_[full_key] = p.parse_primitive();
            p.consume(',');
        }
    }

    return true;
}

// ── 공개 API ─────────────────────────────────────────────────────────
bool Config::load(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        log_err_main("config 파일 열기 실패 | %s", path.c_str());
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    values_.clear();
    arrays_.clear();
    path_ = path;

    if (!parse(content)) {
        log_err_main("config JSON 파싱 실패 | %s", path.c_str());
        return false;
    }

    log_main("config 로드 완료 | %s (%zu 값, %zu 배열)",
             path.c_str(), values_.size(), arrays_.size());
    return true;
}

std::string Config::get_str(const std::string& key, const std::string& default_val) const {
    auto it = values_.find(key);
    return (it != values_.end()) ? it->second : default_val;
}

int Config::get_int(const std::string& key, int default_val) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    return std::atoi(it->second.c_str());
}

double Config::get_double(const std::string& key, double default_val) const {
    auto it = values_.find(key);
    if (it == values_.end()) return default_val;
    return std::atof(it->second.c_str());
}

std::vector<std::string> Config::get_str_array(const std::string& key) const {
    auto it = arrays_.find(key);
    return (it != arrays_.end()) ? it->second : std::vector<std::string>{};
}

std::vector<Config::HealthTargetConfig> Config::get_health_targets() const {
    std::vector<HealthTargetConfig> result;
    int count = get_int("health_check.targets.count");
    for (int i = 0; i < count; ++i) {
        HealthTargetConfig t;
        std::string base = "health_check.targets." + std::to_string(i);
        t.name = get_str(base + ".name");
        t.ip   = get_str(base + ".ip");
        t.port = get_int(base + ".port");
        result.push_back(t);
    }
    return result;
}

} // namespace factory
