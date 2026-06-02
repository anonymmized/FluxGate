#pragma once

#include "fluxgate/icache.h"
#include "fluxgate/metrics.h"
#include "fluxgate/rate_limiter.h"
#include "fluxgate/runtime_controls.h"

#include <asio.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace fluxgate {

// Keeps the last N metric snapshots for sparkline charts in the dashboard.
class MetricsHistory {
public:
    explicit MetricsHistory(std::size_t capacity = 60);
    void push(const MetricsSnapshot& s);
    std::string to_json() const;
private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<MetricsSnapshot> buf_;
};

class AdminServer final {
public:
    AdminServer(asio::io_context& io_context, std::string host, unsigned short port,
                std::shared_ptr<Metrics> metrics,
                std::shared_ptr<RuntimeControls> controls = nullptr,
                std::shared_ptr<ICache> cache = nullptr,
                std::shared_ptr<RateLimiter> rate_limiter = nullptr,
                std::string token = {});

    // Call once per second from the main loop to record a history point and
    // refresh the live cache-entry gauge.
    void tick();

private:
    void do_accept();

    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<Metrics> metrics_;
    std::shared_ptr<RuntimeControls> controls_;
    std::shared_ptr<ICache> cache_;
    std::shared_ptr<RateLimiter> rate_limiter_;
    std::string token_;
    MetricsHistory history_;
};

} // namespace fluxgate
