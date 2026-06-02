#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fluxgate {

// Per-client token-bucket rate limiter. Keyed by client IP. Thread-safe.
// rpm == 0 disables limiting entirely (allow() always returns true).
class RateLimiter {
public:
    struct ClientStat {
        std::string client;
        std::uint64_t allowed = 0;
        std::uint64_t blocked = 0;
    };

    // Refilled at rpm/60 tokens per second, capped at `burst`.
    void configure(std::size_t rpm, std::size_t burst);

    // Returns true if the request is allowed; consumes one token if so.
    bool allow(const std::string& client);

    // Snapshot of the busiest clients (by total requests), for the dashboard.
    std::vector<ClientStat> top_clients(std::size_t limit = 8) const;

private:
    struct Bucket {
        double tokens = 0;
        std::chrono::steady_clock::time_point last;
        std::uint64_t allowed = 0;
        std::uint64_t blocked = 0;
    };

    mutable std::mutex mutex_;
    std::size_t rpm_ = 0;
    std::size_t burst_ = 0;
    std::unordered_map<std::string, Bucket> buckets_;
};

} // namespace fluxgate
