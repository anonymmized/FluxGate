#include "fluxgate/metrics.h"

#include <sstream>

namespace fluxgate {

void Metrics::on_session_accepted() {
    accepted_sessions_.fetch_add(1, std::memory_order_relaxed);
    active_sessions_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::on_session_closed() {
    auto current = active_sessions_.load(std::memory_order_relaxed);
    while (current > 0 && !active_sessions_.compare_exchange_weak(
                              current, current - 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

void Metrics::on_session_rejected() {
    rejected_sessions_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::on_upstream_connect_failure() {
    upstream_connect_failures_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::add_client_to_upstream_bytes(std::uint64_t bytes) {
    client_to_upstream_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void Metrics::add_upstream_to_client_bytes(std::uint64_t bytes) {
    upstream_to_client_bytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void Metrics::on_cache_hit() {
    cache_hits_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::on_cache_miss() {
    cache_misses_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::on_request_filtered() {
    filtered_requests_.fetch_add(1, std::memory_order_relaxed);
}

MetricsSnapshot Metrics::snapshot() const {
    return {
        .accepted_sessions = accepted_sessions_.load(std::memory_order_relaxed),
        .active_sessions = active_sessions_.load(std::memory_order_relaxed),
        .rejected_sessions = rejected_sessions_.load(std::memory_order_relaxed),
        .upstream_connect_failures = upstream_connect_failures_.load(std::memory_order_relaxed),
        .client_to_upstream_bytes = client_to_upstream_bytes_.load(std::memory_order_relaxed),
        .upstream_to_client_bytes = upstream_to_client_bytes_.load(std::memory_order_relaxed),
        .cache_hits = cache_hits_.load(std::memory_order_relaxed),
        .cache_misses = cache_misses_.load(std::memory_order_relaxed),
        .filtered_requests = filtered_requests_.load(std::memory_order_relaxed),
    };
}

std::string to_prometheus_text(const MetricsSnapshot& snapshot) {
    std::ostringstream out;
    out << "# TYPE fluxgate_sessions_accepted_total counter\n";
    out << "fluxgate_sessions_accepted_total " << snapshot.accepted_sessions << '\n';
    out << "# TYPE fluxgate_sessions_active gauge\n";
    out << "fluxgate_sessions_active " << snapshot.active_sessions << '\n';
    out << "# TYPE fluxgate_sessions_rejected_total counter\n";
    out << "fluxgate_sessions_rejected_total " << snapshot.rejected_sessions << '\n';
    out << "# TYPE fluxgate_upstream_connect_failures_total counter\n";
    out << "fluxgate_upstream_connect_failures_total " << snapshot.upstream_connect_failures << '\n';
    out << "# TYPE fluxgate_client_to_upstream_bytes_total counter\n";
    out << "fluxgate_client_to_upstream_bytes_total " << snapshot.client_to_upstream_bytes << '\n';
    out << "# TYPE fluxgate_upstream_to_client_bytes_total counter\n";
    out << "fluxgate_upstream_to_client_bytes_total " << snapshot.upstream_to_client_bytes << '\n';
    out << "# TYPE fluxgate_cache_hits_total counter\n";
    out << "fluxgate_cache_hits_total " << snapshot.cache_hits << '\n';
    out << "# TYPE fluxgate_cache_misses_total counter\n";
    out << "fluxgate_cache_misses_total " << snapshot.cache_misses << '\n';
    out << "# TYPE fluxgate_filtered_requests_total counter\n";
    out << "fluxgate_filtered_requests_total " << snapshot.filtered_requests << '\n';
    return out.str();
}

} // namespace fluxgate
