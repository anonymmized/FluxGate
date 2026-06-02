#include "fluxgate/metrics.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace fluxgate {
namespace {

// Minimal JSON string escaper for the request log (hosts/paths only — never
// body content). Handles the characters that would break JSON.
void append_json_escaped(std::ostringstream& o, std::string_view s) {
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                      << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
                } else {
                    o << c;
                }
        }
    }
}

} // namespace

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

void Metrics::on_rate_limited() {
    rate_limited_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::add_estimated_tokens_saved(std::uint64_t tokens) {
    estimated_tokens_saved_.fetch_add(tokens, std::memory_order_relaxed);
}

void Metrics::add_cost_saved(double usd) {
    std::lock_guard lock(rich_mutex_);
    cost_saved_usd_ += usd;
}

void Metrics::set_cache_entries(std::uint64_t n) {
    cache_entries_.store(n, std::memory_order_relaxed);
}

void Metrics::record_request(const RequestRecord& rec) {
    std::lock_guard lock(rich_mutex_);

    auto& p = providers_[rec.host];
    ++p.requests;
    if (rec.cache == "hit")  ++p.cache_hits;
    if (rec.cache == "miss") ++p.cache_misses;
    if (rec.filtered)        ++p.filtered;
    p.bytes_in     += rec.bytes_in;
    p.bytes_out    += rec.bytes_out;
    p.tokens_saved += rec.tokens_saved;
    p.cost_saved_usd += rec.cost_saved_usd;
    if (rec.latency_ms > 0) {
        p.total_latency_ms += rec.latency_ms;
        ++p.latency_samples;
    }

    log_.push_back(rec);
    while (log_.size() > log_capacity_) log_.pop_front();
}

MetricsSnapshot Metrics::snapshot() const {
    MetricsSnapshot s{
        .accepted_sessions = accepted_sessions_.load(std::memory_order_relaxed),
        .active_sessions = active_sessions_.load(std::memory_order_relaxed),
        .rejected_sessions = rejected_sessions_.load(std::memory_order_relaxed),
        .upstream_connect_failures = upstream_connect_failures_.load(std::memory_order_relaxed),
        .client_to_upstream_bytes = client_to_upstream_bytes_.load(std::memory_order_relaxed),
        .upstream_to_client_bytes = upstream_to_client_bytes_.load(std::memory_order_relaxed),
        .cache_hits = cache_hits_.load(std::memory_order_relaxed),
        .cache_misses = cache_misses_.load(std::memory_order_relaxed),
        .filtered_requests = filtered_requests_.load(std::memory_order_relaxed),
        .estimated_tokens_saved = estimated_tokens_saved_.load(std::memory_order_relaxed),
        .rate_limited = rate_limited_.load(std::memory_order_relaxed),
        .cache_entries = cache_entries_.load(std::memory_order_relaxed),
    };
    {
        std::lock_guard lock(rich_mutex_);
        s.estimated_cost_saved_usd = cost_saved_usd_;
    }
    return s;
}

std::string Metrics::providers_json() const {
    std::lock_guard lock(rich_mutex_);
    std::ostringstream o;
    o << std::fixed;
    o << '[';
    bool first = true;
    for (const auto& [host, p] : providers_) {
        if (!first) o << ',';
        first = false;
        const double hr = (p.cache_hits + p.cache_misses > 0)
            ? 100.0 * p.cache_hits / (p.cache_hits + p.cache_misses) : 0.0;
        const double avg_ms = p.latency_samples > 0
            ? static_cast<double>(p.total_latency_ms) / p.latency_samples : 0.0;
        o << "{\"host\":\"";
        append_json_escaped(o, host);
        o << "\",\"requests\":" << p.requests
          << ",\"cache_hits\":" << p.cache_hits
          << ",\"cache_misses\":" << p.cache_misses
          << ",\"hit_rate\":" << std::setprecision(1) << hr
          << ",\"filtered\":" << p.filtered
          << ",\"bytes_in\":" << p.bytes_in
          << ",\"bytes_out\":" << p.bytes_out
          << ",\"tokens_saved\":" << p.tokens_saved
          << ",\"avg_latency_ms\":" << std::setprecision(0) << avg_ms
          << ",\"cost_saved_usd\":" << std::setprecision(4) << p.cost_saved_usd
          << "}";
    }
    o << ']';
    return o.str();
}

std::string Metrics::requests_json() const {
    std::lock_guard lock(rich_mutex_);
    std::ostringstream o;
    o << std::fixed;
    o << '[';
    // Newest first.
    bool first = true;
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        const auto& r = *it;
        if (!first) o << ',';
        first = false;
        o << "{\"id\":" << r.id
          << ",\"ts\":" << r.ts_ms
          << ",\"host\":\"";   append_json_escaped(o, r.host);
        o << "\",\"method\":\""; append_json_escaped(o, r.method);
        o << "\",\"path\":\"";   append_json_escaped(o, r.path);
        o << "\",\"model\":\"";  append_json_escaped(o, r.model);
        o << "\",\"client\":\"";append_json_escaped(o, r.client);
        o << "\",\"cache\":\"";  append_json_escaped(o, r.cache);
        o << "\",\"status\":" << r.status
          << ",\"latency_ms\":" << r.latency_ms
          << ",\"bytes_in\":" << r.bytes_in
          << ",\"bytes_out\":" << r.bytes_out
          << ",\"tokens_saved\":" << r.tokens_saved
          << ",\"cost_saved_usd\":" << std::setprecision(4) << r.cost_saved_usd
          << ",\"filtered\":" << (r.filtered ? "true" : "false")
          << "}";
    }
    o << ']';
    return o.str();
}

std::vector<std::pair<std::string, ProviderStat>> Metrics::top_providers(std::size_t n) const {
    std::lock_guard lock(rich_mutex_);
    std::vector<std::pair<std::string, ProviderStat>> out(providers_.begin(), providers_.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.second.requests > b.second.requests;
    });
    if (out.size() > n) out.resize(n);
    return out;
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
    out << "# TYPE fluxgate_rate_limited_total counter\n";
    out << "fluxgate_rate_limited_total " << snapshot.rate_limited << '\n';
    out << "# TYPE fluxgate_cache_entries gauge\n";
    out << "fluxgate_cache_entries " << snapshot.cache_entries << '\n';
    out << "# TYPE fluxgate_estimated_tokens_saved_total counter\n";
    out << "fluxgate_estimated_tokens_saved_total " << snapshot.estimated_tokens_saved << '\n';
    out << "# TYPE fluxgate_estimated_cost_saved_usd counter\n";
    out << "fluxgate_estimated_cost_saved_usd " << std::fixed << std::setprecision(6)
        << snapshot.estimated_cost_saved_usd << '\n';
    return out.str();
}

std::string to_json(const MetricsSnapshot& s) {
    const double hit_rate = (s.cache_hits + s.cache_misses > 0)
        ? 100.0 * s.cache_hits / (s.cache_hits + s.cache_misses) : 0.0;

    std::ostringstream o;
    o << std::fixed;
    o << "{"
      << "\"accepted_sessions\":"   << s.accepted_sessions   << ","
      << "\"active_sessions\":"     << s.active_sessions     << ","
      << "\"rejected_sessions\":"   << s.rejected_sessions   << ","
      << "\"upstream_failures\":"   << s.upstream_connect_failures << ","
      << "\"bytes_in\":"            << s.client_to_upstream_bytes  << ","
      << "\"bytes_out\":"           << s.upstream_to_client_bytes  << ","
      << "\"cache_hits\":"          << s.cache_hits          << ","
      << "\"cache_misses\":"        << s.cache_misses        << ","
      << "\"cache_hit_rate\":"      << std::setprecision(1) << hit_rate << ","
      << "\"cache_entries\":"       << s.cache_entries       << ","
      << "\"filtered_requests\":"   << s.filtered_requests   << ","
      << "\"rate_limited\":"        << s.rate_limited        << ","
      << "\"estimated_tokens_saved\":" << s.estimated_tokens_saved << ","
      << "\"estimated_cost_saved_usd\":" << std::setprecision(4) << s.estimated_cost_saved_usd
      << "}";
    return o.str();
}

} // namespace fluxgate
