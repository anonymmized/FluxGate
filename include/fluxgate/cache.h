#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fluxgate {

struct CacheEntry {
    std::string value;
    std::chrono::steady_clock::time_point expires_at;
};

class MemoryCache {
public:
    explicit MemoryCache(std::size_t max_entries);

    void put(std::string key, std::string value, std::chrono::seconds ttl);
    std::optional<std::string> get(std::string_view key);
    std::size_t size() const;

private:
    using Order = std::list<std::string>;

    struct StoredEntry {
        CacheEntry entry;
        Order::iterator order_it;
    };

    void evict_expired_locked(std::chrono::steady_clock::time_point now);
    void evict_overflow_locked();

    std::size_t max_entries_;
    mutable std::mutex mutex_;
    Order order_;
    std::unordered_map<std::string, StoredEntry> entries_;
};

std::string cache_key(std::string_view method, std::string_view target, std::string_view body);

} // namespace fluxgate
