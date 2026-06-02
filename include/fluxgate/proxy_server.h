#pragma once

#include "fluxgate/config.h"
#include "fluxgate/filter.h"
#include "fluxgate/icache.h"
#include "fluxgate/metrics.h"
#include "fluxgate/mitm_services.h"
#include "fluxgate/rate_limiter.h"
#include "fluxgate/runtime_controls.h"

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace fluxgate {

class ProxyServer final {
public:
    ProxyServer(asio::io_context& io_context, AppConfig config, SharedMitmServices mitm_services = nullptr);
    MetricsSnapshot metrics() const;
    std::shared_ptr<Metrics> shared_metrics() const;
    std::shared_ptr<RuntimeControls> shared_controls() const;
    std::shared_ptr<RateLimiter> shared_rate_limiter() const;
    std::shared_ptr<ICache> shared_cache() const;

private:
    void do_accept();

    AppConfig config_;
    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<Metrics> metrics_;
    SharedMitmServices mitm_services_;
    std::shared_ptr<ICache> cache_;
    std::shared_ptr<FilterPipeline> filter_pipeline_;
    std::shared_ptr<RuntimeControls> controls_;
    std::shared_ptr<RateLimiter> rate_limiter_;
    std::atomic_uint64_t session_id_{0};
};

} // namespace fluxgate
