#include "fluxgate/rate_limiter.h"

#include <algorithm>

namespace fluxgate {

void RateLimiter::configure(std::size_t rpm, std::size_t burst) {
    std::lock_guard lock(mutex_);
    rpm_ = rpm;
    burst_ = burst > 0 ? burst : (rpm > 0 ? rpm : 0);
}

bool RateLimiter::allow(const std::string& client) {
    std::lock_guard lock(mutex_);
    if (rpm_ == 0) return true;  // limiting disabled

    const auto now = std::chrono::steady_clock::now();
    auto& b = buckets_[client];
    if (b.last.time_since_epoch().count() == 0) {
        b.tokens = static_cast<double>(burst_);
        b.last = now;
    } else {
        const double elapsed =
            std::chrono::duration<double>(now - b.last).count();
        b.tokens = std::min(static_cast<double>(burst_),
                            b.tokens + elapsed * (static_cast<double>(rpm_) / 60.0));
        b.last = now;
    }

    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        ++b.allowed;
        return true;
    }
    ++b.blocked;
    return false;
}

std::vector<RateLimiter::ClientStat> RateLimiter::top_clients(std::size_t limit) const {
    std::lock_guard lock(mutex_);
    std::vector<ClientStat> out;
    out.reserve(buckets_.size());
    for (const auto& [client, b] : buckets_)
        out.push_back({client, b.allowed, b.blocked});
    std::sort(out.begin(), out.end(), [](const ClientStat& a, const ClientStat& b) {
        return (a.allowed + a.blocked) > (b.allowed + b.blocked);
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

} // namespace fluxgate
