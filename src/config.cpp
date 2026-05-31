#include "fluxgate/config.h"

#include <charconv>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <limits>

namespace fluxgate {
namespace {

unsigned short parse_port(std::string_view value) {
    int parsed = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size() || parsed <= 0 || parsed > 65535) {
        throw std::invalid_argument("invalid port: " + std::string(value));
    }
    return static_cast<unsigned short>(parsed);
}

std::size_t parse_size(std::string_view value, std::string_view label) {
    std::size_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        throw std::invalid_argument("invalid " + std::string(label) + ": " + std::string(value));
    }
    return parsed;
}

} // namespace

std::string usage(std::string_view program_name) {
    return "usage: " + std::string(program_name)
        + " [--listen host] [--port port] [--threads n] [--no-cache] [--cache-max-entries n]"
        + " [--admin host:port] [--no-admin]"
        + " [--generate-ca prefix] [--ca-common-name name] [--ca-valid-days days]"
        + " [--mitm --mitm-ca-key path --mitm-ca-cert path]"
        + " [--mitm-leaf-cache-entries n] [--mitm-leaf-valid-days days]";
}

AppConfig parse_args(int argc, char* argv[]) {
    AppConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto require_value = [&](std::string_view option) -> std::string_view {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value for " + std::string(option));
            }
            return argv[++i];
        };

        if (arg == "--listen") {
            config.listen_host = std::string(require_value(arg));
        } else if (arg == "--port") {
            config.listen_port = parse_port(require_value(arg));
        } else if (arg == "--threads") {
            config.worker_threads = parse_size(require_value(arg), "thread count");
            if (config.worker_threads == 0) {
                throw std::invalid_argument("thread count must be greater than zero");
            }
        } else if (arg == "--no-cache") {
            config.enable_cache = false;
        } else if (arg == "--cache-max-entries") {
            config.cache_max_entries = parse_size(require_value(arg), "cache max entries");
        } else if (arg == "--admin") {
            const auto value = require_value(arg);
            const auto colon = value.rfind(':');
            if (colon == std::string_view::npos || colon == 0 || colon + 1 >= value.size()) {
                throw std::invalid_argument("admin endpoint must be host:port");
            }
            config.admin_host = std::string(value.substr(0, colon));
            config.admin_port = parse_port(value.substr(colon + 1));
            config.enable_admin = true;
        } else if (arg == "--no-admin") {
            config.enable_admin = false;
        } else if (arg == "--generate-ca") {
            config.generate_ca_prefix = std::string(require_value(arg));
        } else if (arg == "--ca-common-name") {
            config.ca_common_name = std::string(require_value(arg));
        } else if (arg == "--ca-valid-days") {
            const auto days = parse_size(require_value(arg), "CA validity days");
            if (days == 0 || days > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                throw std::invalid_argument("CA validity days must fit a positive int");
            }
            config.ca_valid_days = static_cast<int>(days);
        } else if (arg == "--mitm") {
            config.enable_mitm = true;
        } else if (arg == "--mitm-ca-key") {
            config.mitm_ca_key_path = std::string(require_value(arg));
            config.enable_mitm = true;
        } else if (arg == "--mitm-ca-cert") {
            config.mitm_ca_cert_path = std::string(require_value(arg));
            config.enable_mitm = true;
        } else if (arg == "--mitm-leaf-cache-entries") {
            config.mitm_leaf_cache_entries = parse_size(require_value(arg), "MITM leaf cache entries");
            if (config.mitm_leaf_cache_entries == 0) {
                throw std::invalid_argument("MITM leaf cache entries must be greater than zero");
            }
        } else if (arg == "--mitm-leaf-valid-days") {
            const auto days = parse_size(require_value(arg), "MITM leaf validity days");
            if (days == 0 || days > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                throw std::invalid_argument("MITM leaf validity days must fit a positive int");
            }
            config.mitm_leaf_valid_days = static_cast<int>(days);
        } else if (arg == "--help" || arg == "-h") {
            throw HelpRequested(usage(argv[0]));
        } else if (!arg.empty() && arg.front() != '-') {
            config.listen_port = parse_port(arg);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(arg));
        }
    }

    if (config.worker_threads == 0) {
        const auto detected = std::thread::hardware_concurrency();
        config.worker_threads = detected == 0 ? 1 : detected;
    }
    if (config.enable_mitm && (config.mitm_ca_key_path.empty() || config.mitm_ca_cert_path.empty())) {
        throw std::invalid_argument("MITM mode requires --mitm-ca-key and --mitm-ca-cert");
    }
    return config;
}

} // namespace fluxgate
