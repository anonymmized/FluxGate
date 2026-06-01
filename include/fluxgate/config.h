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
    std::size_t max_body_bytes = 4 * 1024 * 1024;  // 4 MB
    bool enable_cache = true;
    std::size_t cache_max_entries = 4096;
    std::size_t cache_ttl_seconds = 300;
    std::string cache_backend = "memory";  // "memory" | "redis"
    std::string redis_url = "redis://127.0.0.1:6379";
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
    bool enable_pii_redaction = true;
    std::size_t max_chat_history = 20;  // 0 = unlimited
    // Provider filtering: hosts to intercept (allowlist) and never touch (denylist).
    // Empty allowlist = intercept all hosts that aren't in denylist.
    std::vector<std::string> provider_allowlist;
    std::vector<std::string> provider_denylist;
    // Path to TOML config file (loaded before CLI args, CLI args override file).
    std::optional<std::string> config_file;
    // If set, admin endpoint requires "Authorization: Bearer <token>".
    std::string admin_token;
};

AppConfig parse_args(int argc, char* argv[]);
std::string usage(std::string_view program_name);
std::string dump_config_toml(const AppConfig& config);

} // namespace fluxgate
