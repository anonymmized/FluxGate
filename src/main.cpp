#include "fluxgate/admin_server.h"
#include "fluxgate/cert_authority.h"
#include "fluxgate/config.h"
#include "fluxgate/logger.h"
#include "fluxgate/mitm_services.h"
#include "fluxgate/proxy_server.h"
#include "fluxgate/setup_wizard.h"
#include "fluxgate/tui.h"

#include <asio.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to open file: " + path);
    std::ostringstream buf;
    buf << input.rdbuf();
    return buf.str();
}

// Returns true if argc/argv look like the user wants an interactive launch
// (no meaningful flags passed, or only --setup).
bool wants_interactive(int argc, char* argv[]) {
    if (argc == 1) return true;
    if (argc == 2 && std::string_view(argv[1]) == "--setup") return true;
    return false;
}

const std::string DEFAULT_CONFIG = "./fluxgate.toml";

} // namespace

int main(int argc, char* argv[]) {
    try {
        fluxgate::AppConfig config;
        bool interactive = wants_interactive(argc, argv);

        if (interactive) {
            // ── Interactive mode ──────────────────────────────────────────
            // If a default config already exists, load it; otherwise wizard.
            if (fs::exists(DEFAULT_CONFIG)) {
                const char* load_argv[] = {argv[0], "--config", DEFAULT_CONFIG.c_str()};
                config = fluxgate::parse_args(3, const_cast<char**>(load_argv));
                std::cout << "\033[35m\033[1m  ⚡ FluxGate\033[0m  loaded \033[36m"
                          << DEFAULT_CONFIG << "\033[0m\n\n";
            } else {
                config = fluxgate::run_setup_wizard(DEFAULT_CONFIG);
            }
        } else {
            config = fluxgate::parse_args(argc, argv);
        }

        // ── CA generation shortcut ────────────────────────────────────────
        if (config.generate_ca_prefix) {
            const auto ca = fluxgate::CertificateAuthority::create_self_signed(
                config.ca_common_name, config.ca_valid_days);
            const auto pem = ca.pem();
            const auto key_path  = *config.generate_ca_prefix + ".key.pem";
            const auto cert_path = *config.generate_ca_prefix + ".cert.pem";
            std::ofstream k(key_path, std::ios::binary | std::ios::trunc);
            std::ofstream c(cert_path, std::ios::binary | std::ios::trunc);
            if (!k || !c) throw std::runtime_error("failed to open CA output files");
            k << pem.private_key_pem;
            c << pem.certificate_pem;
            std::cout << "Generated CA private key: " << key_path << '\n';
            std::cout << "Generated CA certificate: " << cert_path << '\n';
            return EXIT_SUCCESS;
        }

        // ── Logger ────────────────────────────────────────────────────────
        fluxgate::Logger::instance().set_json(!interactive);

        // ── MITM services ─────────────────────────────────────────────────
        fluxgate::SharedMitmServices mitm_services;
        if (config.enable_mitm) {
            auto authority = fluxgate::CertificateAuthority::from_pem(
                read_file(config.mitm_ca_key_path),
                read_file(config.mitm_ca_cert_path));
            if (!authority.can_sign())
                throw std::runtime_error("configured MITM CA cannot sign certificates");
            mitm_services = std::make_shared<fluxgate::MitmServices>(
                std::move(authority),
                config.mitm_leaf_cache_entries,
                config.mitm_leaf_valid_days);
        }

        asio::io_context io_context;

        asio::signal_set signals(io_context, SIGINT, SIGTERM);

        // ── Proxy server ──────────────────────────────────────────────────
        fluxgate::ProxyServer server(io_context, config, mitm_services);

        if (!interactive) {
            fluxgate::Logger::instance().info(
                "FluxGate listening on " + config.listen_host + ':'
                + std::to_string(config.listen_port)
                + " threads=" + std::to_string(config.worker_threads)
                + (config.enable_mitm ? " mitm=on" : " mitm=off"));
        }

        // ── Admin server ──────────────────────────────────────────────────
        std::unique_ptr<fluxgate::AdminServer> admin_server;
        if (config.enable_admin) {
            admin_server = std::make_unique<fluxgate::AdminServer>(
                io_context, config.admin_host, config.admin_port,
                server.shared_metrics(), server.shared_controls(),
                server.shared_cache(), server.shared_rate_limiter(),
                config.admin_token);
        }

        // ── TUI (only in interactive/TTY mode) ────────────────────────────
        std::unique_ptr<fluxgate::FluxGateTUI> tui;
        if (interactive || isatty(STDOUT_FILENO)) {
            // Suppress per-request info logs so they don't corrupt the TUI box.
            fluxgate::Logger::instance().set_level(fluxgate::Logger::Level::warn);
            tui = std::make_unique<fluxgate::FluxGateTUI>(
                server.shared_metrics(), config);
            tui->start();
        }

        // Stop on signal or TUI quit
        signals.async_wait([&](const std::error_code&, int) {
            io_context.stop();
        });

        // ── Thread pool ───────────────────────────────────────────────────
        std::vector<std::thread> workers;
        const auto nw = config.worker_threads > 1 ? config.worker_threads - 1 : 0;
        workers.reserve(nw);
        for (std::size_t i = 0; i < nw; ++i)
            workers.emplace_back([&io_context] { io_context.run(); });

        // Poll for TUI quit in main loop; record a history point each second.
        auto last_tick = std::chrono::steady_clock::now();
        while (!io_context.stopped()) {
            io_context.run_one_for(std::chrono::milliseconds(100));
            if (tui && tui->quit_requested()) {
                io_context.stop();
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (admin_server && now - last_tick >= std::chrono::seconds(1)) {
                admin_server->tick();
                last_tick = now;
            }
        }

        for (auto& w : workers) w.join();
        if (tui) tui->stop();

    } catch (const fluxgate::HelpRequested& e) {
        std::cout << e.what() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        const std::string msg = e.what();
        if (msg.find("Address already in use") != std::string::npos) {
            std::cerr << "\033[31m✗ Port already in use.\033[0m "
                      << "FluxGate (or another program) is already running on that port.\n\n"
                      << "  Stop the existing FluxGate:   \033[36mpkill -if fluxgate\033[0m\n"
                      << "  Or pick different ports:      \033[36mfluxgate --port 8081 --admin 127.0.0.1:9091\033[0m\n";
        } else {
            std::cerr << "\033[31mError:\033[0m " << msg << '\n';
        }
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
