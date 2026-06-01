#include "fluxgate/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace fluxgate {
namespace {

std::mutex g_log_mutex;

std::string now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count() % 1000;
    std::ostringstream out;
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    out << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms << 'Z';
    return out.str();
}

// Minimal JSON string escaper (handles common control characters and quotes).
std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        if (c == '"')       { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else if (c == '\t') { out += "\\t"; }
        else if (c < 0x20)  { out += "\\u00"; out += "0123456789abcdef"[c >> 4];
                               out += "0123456789abcdef"[c & 0xf]; }
        else                { out += static_cast<char>(c); }
    }
    return out;
}

} // namespace

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(Level level) noexcept { level_ = level; }
void Logger::set_json(bool enable) noexcept  { json_  = enable; }

void Logger::intercepted(std::uint64_t id, std::string_view host,
                         std::string_view method, std::string_view path) {
    emit("intercepted", id, host, method, path, 0, 0, "");
}
void Logger::cache_hit(std::uint64_t id, std::string_view host,
                       std::string_view method, std::string_view path) {
    emit("cache_hit", id, host, method, path, 0, 0, "");
}
void Logger::cache_miss(std::uint64_t id, std::string_view host,
                        std::string_view method, std::string_view path) {
    emit("cache_miss", id, host, method, path, 0, 0, "");
}
void Logger::filtered(std::uint64_t id, std::string_view host,
                      std::string_view method, std::string_view path) {
    emit("filtered", id, host, method, path, 0, 0, "");
}
void Logger::forwarded(std::uint64_t id, std::string_view host,
                       std::string_view method, std::string_view path,
                       int status, std::uint64_t upstream_ms) {
    emit("forwarded", id, host, method, path, status, upstream_ms, "");
}
void Logger::proxy_error(std::uint64_t id, std::string_view host,
                         std::string_view reason) {
    emit("error", id, host, "", "", 0, 0, reason);
}

void Logger::info(std::string_view message) {
    if (level_ > Level::info) return;
    std::lock_guard lock(g_log_mutex);
    if (json_) {
        std::clog << "{\"ts\":\"" << now_iso8601() << "\",\"level\":\"info\","
                  << "\"msg\":\"" << json_escape(message) << "\"}\n";
    } else {
        std::clog << now_iso8601() << " INFO  " << message << '\n';
    }
}

void Logger::warn(std::string_view message) {
    if (level_ > Level::warn) return;
    std::lock_guard lock(g_log_mutex);
    if (json_) {
        std::clog << "{\"ts\":\"" << now_iso8601() << "\",\"level\":\"warn\","
                  << "\"msg\":\"" << json_escape(message) << "\"}\n";
    } else {
        std::clog << now_iso8601() << " WARN  " << message << '\n';
    }
}

void Logger::emit(std::string_view event, std::uint64_t session_id,
                  std::string_view host, std::string_view method,
                  std::string_view path, int status, std::uint64_t upstream_ms,
                  std::string_view error) {
    std::lock_guard lock(g_log_mutex);
    if (json_) {
        std::clog << "{\"ts\":\"" << now_iso8601()
                  << "\",\"event\":\"" << event
                  << "\",\"id\":" << session_id
                  << ",\"host\":\"" << json_escape(host) << '"';
        if (!method.empty()) std::clog << ",\"method\":\"" << json_escape(method) << '"';
        if (!path.empty())   std::clog << ",\"path\":\"" << json_escape(path) << '"';
        if (status)          std::clog << ",\"status\":" << status;
        if (upstream_ms)     std::clog << ",\"upstream_ms\":" << upstream_ms;
        if (!error.empty())  std::clog << ",\"error\":\"" << json_escape(error) << '"';
        std::clog << "}\n";
    } else {
        std::clog << now_iso8601() << ' ' << std::setw(12) << event
                  << " id=" << session_id
                  << " host=" << host;
        if (!method.empty()) std::clog << " " << method;
        if (!path.empty())   std::clog << ' ' << path;
        if (status)          std::clog << " status=" << status;
        if (upstream_ms)     std::clog << " upstream_ms=" << upstream_ms;
        if (!error.empty())  std::clog << " error=" << error;
        std::clog << '\n';
    }
}

} // namespace fluxgate
