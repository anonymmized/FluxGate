#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fluxgate {

class HelpRequested final : public std::runtime_error {
public:
    explicit HelpRequested(const std::string& message)
        : std::runtime_error(message) {}
};

struct AppConfig {
    std::string listen_host = "0.0.0.0";
    unsigned short listen_port = 8080;
    std::size_t worker_threads = 0;
    std::size_t max_header_bytes = 16 * 1024;
    std::size_t relay_buffer_bytes = 32 * 1024;
    bool enable_cache = true;
    std::size_t cache_max_entries = 4096;
    bool enable_admin = true;
    std::string admin_host = "127.0.0.1";
    unsigned short admin_port = 9090;
    std::optional<std::string> generate_ca_prefix;
    std::string ca_common_name = "FluxGate Local Root CA";
    int ca_valid_days = 825;
    bool enable_mitm = false;
    std::string mitm_ca_key_path;
    std::string mitm_ca_cert_path;
    std::size_t mitm_leaf_cache_entries = 4096;
    int mitm_leaf_valid_days = 7;
};

AppConfig parse_args(int argc, char* argv[]);
std::string usage(std::string_view program_name);

} // namespace fluxgate
