#pragma once

#include "fluxgate/icache.h"

#include <mutex>
#include <string>
#include <string_view>

struct redisContext;  // forward-declare hiredis type

namespace fluxgate {

// Redis-backed cache using hiredis synchronous API.
// Thread-safe via internal mutex. Falls back gracefully on connection failure.
class RedisCache final : public ICache {
public:
    // url: "redis://host:port" or "redis://host:port/db"
    explicit RedisCache(std::string_view url);
    ~RedisCache() override;

    void put(std::string key, std::string value, std::chrono::seconds ttl) override;
    std::optional<std::string> get(std::string_view key) override;
    std::size_t size() const override;

    bool connected() const;

private:
    void reconnect_if_needed();

    std::string host_;
    int port_{6379};
    int db_{0};
    mutable std::mutex mutex_;
    redisContext* ctx_{nullptr};
};

} // namespace fluxgate
