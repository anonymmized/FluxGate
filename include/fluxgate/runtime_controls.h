#pragma once

#include "fluxgate/config.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace fluxgate {

// Live-tunable settings shared across all sessions. Sessions and filters read
// these on every request, so the web dashboard can change behaviour at runtime
// without a restart. Hot fields are atomics; the host lists are mutex-guarded.
class RuntimeControls {
public:
    explicit RuntimeControls(const AppConfig& cfg)
        : pii_redaction(cfg.enable_pii_redaction),
          cache_enabled(cfg.enable_cache),
          max_chat_history(cfg.max_chat_history),
          cache_ttl_seconds(cfg.cache_ttl_seconds),
          rate_limit_rpm(cfg.rate_limit_rpm),
          rate_limit_burst(cfg.rate_limit_burst),
          monthly_budget_usd(cfg.monthly_budget_usd),
          allowlist_(cfg.provider_allowlist),
          denylist_(cfg.provider_denylist) {}

    std::atomic<bool>        pii_redaction;
    std::atomic<bool>        cache_enabled;
    std::atomic<std::size_t> max_chat_history;
    std::atomic<std::size_t> cache_ttl_seconds;
    std::atomic<std::size_t> rate_limit_rpm;     // 0 = unlimited
    std::atomic<std::size_t> rate_limit_burst;
    std::atomic<double>      monthly_budget_usd;  // 0 = no alert

    // ── Host lists (mutex-guarded copies returned to callers) ───────────────
    std::vector<std::string> allowlist() const {
        std::lock_guard lock(mutex_);
        return allowlist_;
    }
    std::vector<std::string> denylist() const {
        std::lock_guard lock(mutex_);
        return denylist_;
    }
    void set_allowlist(std::vector<std::string> hosts) {
        std::lock_guard lock(mutex_);
        allowlist_ = std::move(hosts);
    }
    void set_denylist(std::vector<std::string> hosts) {
        std::lock_guard lock(mutex_);
        denylist_ = std::move(hosts);
    }

    // Decide whether a host should be intercepted, given the live lists.
    bool should_mitm(const std::string& host) const {
        std::lock_guard lock(mutex_);
        for (const auto& h : denylist_)
            if (h == host) return false;
        if (!allowlist_.empty()) {
            for (const auto& h : allowlist_)
                if (h == host) return true;
            return false;
        }
        return true;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> allowlist_;
    std::vector<std::string> denylist_;
};

} // namespace fluxgate
