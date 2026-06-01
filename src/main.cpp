#include "fluxgate/admin_server.h"
#include "fluxgate/cert_authority.h"
#include "fluxgate/config.h"
#include "fluxgate/logger.h"
#include "fluxgate/mitm_services.h"
#include "fluxgate/proxy_server.h"

#include <asio.hpp>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

namespace {

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        auto config = fluxgate::parse_args(argc, argv);
        fluxgate::Logger::instance().set_json(true);
        if (config.generate_ca_prefix) {
            const auto ca = fluxgate::CertificateAuthority::create_self_signed(config.ca_common_name, config.ca_valid_days);
            const auto pem = ca.pem();
            const auto key_path = *config.generate_ca_prefix + ".key.pem";
            const auto cert_path = *config.generate_ca_prefix + ".cert.pem";

            std::ofstream key_out(key_path, std::ios::binary | std::ios::trunc);
            std::ofstream cert_out(cert_path, std::ios::binary | std::ios::trunc);
            if (!key_out || !cert_out) {
                throw std::runtime_error("failed to open CA output files");
            }
            key_out << pem.private_key_pem;
            cert_out << pem.certificate_pem;
            std::cout << "Generated CA private key: " << key_path << '\n';
            std::cout << "Generated CA certificate: " << cert_path << '\n';
            return EXIT_SUCCESS;
        }

        fluxgate::SharedMitmServices mitm_services;
        if (config.enable_mitm) {
            auto authority = fluxgate::CertificateAuthority::from_pem(
                read_file(config.mitm_ca_key_path),
                read_file(config.mitm_ca_cert_path));
            if (!authority.can_sign()) {
                throw std::runtime_error("configured MITM CA cannot sign certificates");
            }
            mitm_services = std::make_shared<fluxgate::MitmServices>(
                std::move(authority),
                config.mitm_leaf_cache_entries,
                config.mitm_leaf_valid_days);
            std::cout << "FluxGate MITM mode enabled with CA certificate: "
                      << config.mitm_ca_cert_path << '\n';
        }

        asio::io_context io_context;

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context](const std::error_code&, int) {
            io_context.stop();
        });

        fluxgate::ProxyServer server(io_context, config, mitm_services);
        fluxgate::Logger::instance().info(
            "FluxGate listening on " + config.listen_host + ':' + std::to_string(config.listen_port)
            + " threads=" + std::to_string(config.worker_threads)
            + (config.enable_mitm ? " mitm=on" : " mitm=off"));

        std::unique_ptr<fluxgate::AdminServer> admin_server;
        if (config.enable_admin) {
            admin_server = std::make_unique<fluxgate::AdminServer>(
                io_context, config.admin_host, config.admin_port,
                server.shared_metrics(), config.admin_token);
            fluxgate::Logger::instance().info(
                "admin endpoint on " + config.admin_host + ':' + std::to_string(config.admin_port)
                + (config.admin_token.empty() ? " (no auth)" : " (Bearer auth)"));
        }

        std::vector<std::thread> workers;
        workers.reserve(config.worker_threads > 0 ? config.worker_threads - 1 : 0);
        for (std::size_t i = 1; i < config.worker_threads; ++i) {
            workers.emplace_back([&io_context] {
                io_context.run();
            });
        }

        io_context.run();
        for (auto& worker : workers) {
            worker.join();
        }
    } catch (const fluxgate::HelpRequested& e) {
        std::cout << e.what() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << '\n';
        std::cerr << fluxgate::usage(argv[0]) << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
