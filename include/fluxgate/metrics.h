#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fluxgate {

struct MetricsSnapshot {
    std::uint64_t accepted_sessions = 0;
    std::uint64_t active_sessions = 0;
    std::uint64_t rejected_sessions = 0;
    std::uint64_t upstream_connect_failures = 0;
    std::uint64_t client_to_upstream_bytes = 0;
    std::uint64_t upstream_to_client_bytes = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t filtered_requests = 0;
    std::uint64_t estimated_tokens_saved = 0;
    std::uint64_t rate_limited = 0;
    std::uint64_t cache_entries = 0;
    double        estimated_cost_saved_usd = 0.0;
};

// Per-provider (per-host) rollup shown in the dashboard Providers tab.
struct ProviderStat {
    std::uint64_t requests = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t filtered = 0;
    std::uint64_t bytes_in = 0;
    std::uint64_t bytes_out = 0;
    std::uint64_t tokens_saved = 0;
    std::uint64_t total_latency_ms = 0;
    std::uint64_t latency_samples = 0;
    double        cost_saved_usd = 0.0;
};

// One completed MITM request, captured for the live request inspector.
struct RequestRecord {
    std::uint64_t id = 0;
    std::int64_t  ts_ms = 0;        // wall-clock epoch milliseconds
    std::string   host;
    std::string   method;
    std::string   path;
    std::string   model;
    std::string   client;          // client IP
    std::string   cache;           // "hit" | "miss" | "bypass"
    int           status = 0;
    std::uint64_t latency_ms = 0;
    std::uint64_t bytes_in = 0;
    std::uint64_t bytes_out = 0;
    std::uint64_t tokens_saved = 0;
    double        cost_saved_usd = 0.0;
    bool          filtered = false;
};

class Metrics {
public:
    explicit Metrics(std::size_t log_capacity = 200) : log_capacity_(log_capacity) {}

    void on_session_accepted();
    void on_session_closed();
    void on_session_rejected();
    void on_upstream_connect_failure();
    void add_client_to_upstream_bytes(std::uint64_t bytes);
    void add_upstream_to_client_bytes(std::uint64_t bytes);
    void on_cache_hit();
    void on_cache_miss();
    void on_request_filtered();
    void on_rate_limited();
    void add_estimated_tokens_saved(std::uint64_t tokens);
    void add_cost_saved(double usd);
    void set_cache_entries(std::uint64_t n);

    // Records a completed request: appends to the log ring buffer and rolls it
    // up into the owning host's ProviderStat. Does not touch the hot atomics
    // (those are bumped incrementally as the request progresses).
    void record_request(const RequestRecord& rec);

    MetricsSnapshot snapshot() const;
    std::string providers_json() const;
    std::string requests_json() const;

    // Busiest providers by request count (for the terminal TUI).
    std::vector<std::pair<std::string, ProviderStat>> top_providers(std::size_t n) const;

private:
    std::atomic_uint64_t accepted_sessions_{0};
    std::atomic_uint64_t active_sessions_{0};
    std::atomic_uint64_t rejected_sessions_{0};
    std::atomic_uint64_t upstream_connect_failures_{0};
    std::atomic_uint64_t client_to_upstream_bytes_{0};
    std::atomic_uint64_t upstream_to_client_bytes_{0};
    std::atomic_uint64_t cache_hits_{0};
    std::atomic_uint64_t cache_misses_{0};
    std::atomic_uint64_t filtered_requests_{0};
    std::atomic_uint64_t estimated_tokens_saved_{0};
    std::atomic_uint64_t rate_limited_{0};
    std::atomic_uint64_t cache_entries_{0};

    // Cost accumulator + per-host rollups + request log share one mutex; they
    // are off the per-byte hot path (touched once per completed request).
    mutable std::mutex rich_mutex_;
    double cost_saved_usd_ = 0.0;
    std::unordered_map<std::string, ProviderStat> providers_;
    std::deque<RequestRecord> log_;
    std::size_t log_capacity_;
};

std::string to_prometheus_text(const MetricsSnapshot& snapshot);
std::string to_json(const MetricsSnapshot& snapshot);

} // namespace fluxgate
