#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace fluxgate {

struct MetricsSnapshot {
    std::uint64_t accepted_sessions = 0;
    std::uint64_t active_sessions = 0;
    std::uint64_t rejected_sessions = 0;
    std::uint64_t upstream_connect_failures = 0;
    std::uint64_t client_to_upstream_bytes = 0;
    std::uint64_t upstream_to_client_bytes = 0;
};

class Metrics {
public:
    void on_session_accepted();
    void on_session_closed();
    void on_session_rejected();
    void on_upstream_connect_failure();
    void add_client_to_upstream_bytes(std::uint64_t bytes);
    void add_upstream_to_client_bytes(std::uint64_t bytes);
    MetricsSnapshot snapshot() const;

private:
    std::atomic_uint64_t accepted_sessions_{0};
    std::atomic_uint64_t active_sessions_{0};
    std::atomic_uint64_t rejected_sessions_{0};
    std::atomic_uint64_t upstream_connect_failures_{0};
    std::atomic_uint64_t client_to_upstream_bytes_{0};
    std::atomic_uint64_t upstream_to_client_bytes_{0};
};

std::string to_prometheus_text(const MetricsSnapshot& snapshot);

} // namespace fluxgate
