#pragma once

#include "fluxgate/icache.h"

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

class MemoryCache final : public ICache {
public:
    explicit MemoryCache(std::size_t max_entries);

    void put(std::string key, std::string value, std::chrono::seconds ttl) override;
    std::optional<std::string> get(std::string_view key) override;
    std::size_t size() const override;
    void clear() override;

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

// Like cache_key, but canonicalizes a JSON body first (sorted object keys,
// no insignificant whitespace) so semantically identical requests that differ
// only in formatting or key order share a cache entry. Falls back to the raw
// body when it isn't valid JSON.
std::string normalized_cache_key(std::string_view method, std::string_view target,
                                 std::string_view body);

} // namespace fluxgate
