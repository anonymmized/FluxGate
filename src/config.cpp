#include "fluxgate/config.h"

#include <toml++/toml.hpp>

#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

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

AppConfig load_config_file(const std::string& path) {
    AppConfig cfg;
    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error("config file parse error: " + std::string(e.description()));
    }

    if (auto proxy = tbl["proxy"].as_table()) {
        if (auto v = (*proxy)["listen"].value<std::string>()) cfg.listen_host = *v;
        if (auto v = (*proxy)["port"].value<int64_t>()) cfg.listen_port = static_cast<unsigned short>(*v);
        if (auto v = (*proxy)["threads"].value<int64_t>()) cfg.worker_threads = static_cast<std::size_t>(*v);
        if (auto v = (*proxy)["max_header_bytes"].value<int64_t>()) cfg.max_header_bytes = static_cast<std::size_t>(*v);
        if (auto v = (*proxy)["relay_buffer_bytes"].value<int64_t>()) cfg.relay_buffer_bytes = static_cast<std::size_t>(*v);
        if (auto v = (*proxy)["max_body_bytes"].value<int64_t>()) cfg.max_body_bytes = static_cast<std::size_t>(*v);
    }

    if (auto tls = tbl["tls"].as_table()) {
        if (auto v = (*tls)["enabled"].value<bool>()) cfg.enable_mitm = *v;
        if (auto v = (*tls)["ca_key"].value<std::string>()) cfg.mitm_ca_key_path = *v;
        if (auto v = (*tls)["ca_cert"].value<std::string>()) cfg.mitm_ca_cert_path = *v;
        if (auto v = (*tls)["leaf_cache_entries"].value<int64_t>()) cfg.mitm_leaf_cache_entries = static_cast<std::size_t>(*v);
        if (auto v = (*tls)["leaf_valid_days"].value<int64_t>()) cfg.mitm_leaf_valid_days = static_cast<int>(*v);
    }

    if (auto filters = tbl["filters"].as_table()) {
        if (auto v = (*filters)["pii_redaction"].value<bool>()) cfg.enable_pii_redaction = *v;
        if (auto v = (*filters)["max_chat_history"].value<int64_t>()) cfg.max_chat_history = static_cast<std::size_t>(*v);
    }

    if (auto cache = tbl["cache"].as_table()) {
        if (auto v = (*cache)["enabled"].value<bool>()) cfg.enable_cache = *v;
        if (auto v = (*cache)["max_entries"].value<int64_t>()) cfg.cache_max_entries = static_cast<std::size_t>(*v);
        if (auto v = (*cache)["ttl_seconds"].value<int64_t>()) cfg.cache_ttl_seconds = static_cast<std::size_t>(*v);
    }

    if (auto admin = tbl["admin"].as_table()) {
        if (auto v = (*admin)["enabled"].value<bool>()) cfg.enable_admin = *v;
        if (auto v = (*admin)["listen"].value<std::string>()) cfg.admin_host = *v;
        if (auto v = (*admin)["port"].value<int64_t>()) cfg.admin_port = static_cast<unsigned short>(*v);
        if (auto v = (*admin)["token"].value<std::string>()) cfg.admin_token = *v;
    }

    if (auto providers = tbl["providers"].as_table()) {
        if (auto arr = (*providers)["allowlist"].as_array()) {
            for (auto& elem : *arr) {
                if (auto v = elem.value<std::string>()) cfg.provider_allowlist.push_back(*v);
            }
        }
        if (auto arr = (*providers)["denylist"].as_array()) {
            for (auto& elem : *arr) {
                if (auto v = elem.value<std::string>()) cfg.provider_denylist.push_back(*v);
            }
        }
    }

    return cfg;
}

} // namespace

std::string usage(std::string_view program_name) {
    return "usage: " + std::string(program_name)
        + " [--config path] [--listen host] [--port port] [--threads n]\n"
        + "       [--no-cache] [--cache-max-entries n] [--cache-ttl seconds]\n"
        + "       [--admin host:port] [--no-admin]\n"
        + "       [--max-body bytes] [--no-pii] [--max-history n]\n"
        + "       [--allow host] [--deny host]\n"
        + "       [--generate-ca prefix] [--ca-common-name name] [--ca-valid-days days]\n"
        + "       [--mitm --mitm-ca-key path --mitm-ca-cert path]\n"
        + "       [--mitm-leaf-cache-entries n] [--mitm-leaf-valid-days days]\n"
        + "       [--dump-config]";
}

AppConfig parse_args(int argc, char* argv[]) {
    AppConfig config;

    // First pass: load config file if --config is present (CLI args override it below).
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--config" && i + 1 < argc) {
            config = load_config_file(argv[i + 1]);
            config.config_file = argv[i + 1];
            break;
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        auto require_value = [&](std::string_view option) -> std::string_view {
            if (i + 1 >= argc)
                throw std::invalid_argument("missing value for " + std::string(option));
            return argv[++i];
        };

        if (arg == "--config") {
            ++i;  // already handled in first pass
        } else if (arg == "--listen") {
            config.listen_host = std::string(require_value(arg));
        } else if (arg == "--port") {
            config.listen_port = parse_port(require_value(arg));
        } else if (arg == "--threads") {
            config.worker_threads = parse_size(require_value(arg), "thread count");
            if (config.worker_threads == 0)
                throw std::invalid_argument("thread count must be greater than zero");
        } else if (arg == "--no-cache") {
            config.enable_cache = false;
        } else if (arg == "--cache-max-entries") {
            config.cache_max_entries = parse_size(require_value(arg), "cache max entries");
        } else if (arg == "--cache-ttl") {
            config.cache_ttl_seconds = parse_size(require_value(arg), "cache TTL seconds");
        } else if (arg == "--max-body") {
            config.max_body_bytes = parse_size(require_value(arg), "max body bytes");
        } else if (arg == "--no-pii") {
            config.enable_pii_redaction = false;
        } else if (arg == "--max-history") {
            config.max_chat_history = parse_size(require_value(arg), "max chat history");
        } else if (arg == "--allow") {
            config.provider_allowlist.emplace_back(require_value(arg));
        } else if (arg == "--deny") {
            config.provider_denylist.emplace_back(require_value(arg));
        } else if (arg == "--admin") {
            const auto value = require_value(arg);
            const auto colon = value.rfind(':');
            if (colon == std::string_view::npos || colon == 0 || colon + 1 >= value.size())
                throw std::invalid_argument("admin endpoint must be host:port");
            config.admin_host = std::string(value.substr(0, colon));
            config.admin_port = parse_port(value.substr(colon + 1));
            config.enable_admin = true;
        } else if (arg == "--no-admin") {
            config.enable_admin = false;
        } else if (arg == "--admin-token") {
            config.admin_token = std::string(require_value(arg));
        } else if (arg == "--generate-ca") {
            config.generate_ca_prefix = std::string(require_value(arg));
        } else if (arg == "--ca-common-name") {
            config.ca_common_name = std::string(require_value(arg));
        } else if (arg == "--ca-valid-days") {
            const auto days = parse_size(require_value(arg), "CA validity days");
            if (days == 0 || days > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("CA validity days must fit a positive int");
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
            if (config.mitm_leaf_cache_entries == 0)
                throw std::invalid_argument("MITM leaf cache entries must be greater than zero");
        } else if (arg == "--mitm-leaf-valid-days") {
            const auto days = parse_size(require_value(arg), "MITM leaf validity days");
            if (days == 0 || days > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("MITM leaf validity days must fit a positive int");
            config.mitm_leaf_valid_days = static_cast<int>(days);
        } else if (arg == "--dump-config") {
            throw HelpRequested(dump_config_toml(config));
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
    if (config.enable_mitm && (config.mitm_ca_key_path.empty() || config.mitm_ca_cert_path.empty()))
        throw std::invalid_argument("MITM mode requires --mitm-ca-key and --mitm-ca-cert");

    return config;
}

std::string dump_config_toml(const AppConfig& c) {
    std::ostringstream out;
    out << "# FluxGate configuration\n"
        << "# Generated by: FluxGate --dump-config\n\n"
        << "[proxy]\n"
        << "listen  = \"" << c.listen_host << "\"\n"
        << "port    = " << c.listen_port << "\n"
        << "threads = " << c.worker_threads << "\n"
        << "max_header_bytes   = " << c.max_header_bytes << "\n"
        << "relay_buffer_bytes = " << c.relay_buffer_bytes << "\n"
        << "max_body_bytes     = " << c.max_body_bytes << "\n\n"
        << "[tls]\n"
        << "enabled          = " << (c.enable_mitm ? "true" : "false") << "\n"
        << "ca_key           = \"" << c.mitm_ca_key_path << "\"\n"
        << "ca_cert          = \"" << c.mitm_ca_cert_path << "\"\n"
        << "leaf_cache_entries = " << c.mitm_leaf_cache_entries << "\n"
        << "leaf_valid_days    = " << c.mitm_leaf_valid_days << "\n\n"
        << "[filters]\n"
        << "pii_redaction    = " << (c.enable_pii_redaction ? "true" : "false") << "\n"
        << "max_chat_history = " << c.max_chat_history << "\n\n"
        << "[cache]\n"
        << "enabled     = " << (c.enable_cache ? "true" : "false") << "\n"
        << "max_entries = " << c.cache_max_entries << "\n"
        << "ttl_seconds = " << c.cache_ttl_seconds << "\n\n"
        << "[admin]\n"
        << "enabled = " << (c.enable_admin ? "true" : "false") << "\n"
        << "listen  = \"" << c.admin_host << "\"\n"
        << "port    = " << c.admin_port << "\n"
        << "# token = \"changeme\"  # uncomment to require Bearer auth\n\n"
        << "[providers]\n"
        << "# Empty allowlist = intercept all hosts not in denylist\n"
        << "allowlist = [";
    for (std::size_t i = 0; i < c.provider_allowlist.size(); ++i) {
        if (i) out << ", ";
        out << '"' << c.provider_allowlist[i] << '"';
    }
    out << "]\n"
        << "denylist  = [";
    for (std::size_t i = 0; i < c.provider_denylist.size(); ++i) {
        if (i) out << ", ";
        out << '"' << c.provider_denylist[i] << '"';
    }
    out << "]\n";
    return out.str();
}

} // namespace fluxgate
