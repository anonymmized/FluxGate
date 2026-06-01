#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fluxgate {

// Thread-safe structured logger. Writes NDJSON (one JSON object per line)
// to the configured output stream. Body content is never logged.
class Logger {
public:
    enum class Level { debug, info, warn, error };

    static Logger& instance();

    void set_level(Level level) noexcept;
    void set_json(bool enable) noexcept;  // false → human-readable fallback

    // Session lifecycle
    void intercepted(std::uint64_t session_id, std::string_view host,
                     std::string_view method, std::string_view path);
    void cache_hit(std::uint64_t session_id, std::string_view host,
                   std::string_view method, std::string_view path);
    void cache_miss(std::uint64_t session_id, std::string_view host,
                    std::string_view method, std::string_view path);
    void filtered(std::uint64_t session_id, std::string_view host,
                  std::string_view method, std::string_view path);
    void forwarded(std::uint64_t session_id, std::string_view host,
                   std::string_view method, std::string_view path,
                   int status, std::uint64_t upstream_ms);
    void proxy_error(std::uint64_t session_id, std::string_view host,
                     std::string_view reason);
    void info(std::string_view message);
    void warn(std::string_view message);

private:
    Logger() = default;

    void emit(std::string_view event, std::uint64_t session_id,
              std::string_view host, std::string_view method,
              std::string_view path, int status, std::uint64_t upstream_ms,
              std::string_view error);

    Level level_{Level::info};
    bool json_{true};
};

} // namespace fluxgate
