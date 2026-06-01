#pragma once

#include "fluxgate/config.h"
#include "fluxgate/metrics.h"

#include <atomic>
#include <memory>
#include <thread>

namespace fluxgate {

class FluxGateTUI {
public:
    FluxGateTUI(std::shared_ptr<Metrics> metrics, const AppConfig& config);
    ~FluxGateTUI();

    void start();                   // begin rendering loop
    void stop();                    // stop rendering loop
    bool quit_requested() const;    // true if user pressed 'q'

private:
    void run();
    void draw(const MetricsSnapshot& snap) const;
    static std::string bar(double ratio, int width = 20);
    static std::string fmt_bytes(std::uint64_t b);
    static std::string fmt_num(std::uint64_t n);

    std::shared_ptr<Metrics> metrics_;
    AppConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::thread thread_;
};

} // namespace fluxgate
