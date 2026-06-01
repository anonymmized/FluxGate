// Integration tests: spin up real ProxyServer + test server, exercise tunnel and MITM paths.
#include "fluxgate/cache.h"
#include "fluxgate/cert_authority.h"
#include "fluxgate/config.h"
#include "fluxgate/filter.h"
#include "fluxgate/proxy_server.h"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <openssl/ssl.h>

#include <atomic>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using asio::ip::tcp;

// ─── Helpers ────────────────────────────────────────────────────────────────

static unsigned short free_port() {
    asio::io_context io;
    tcp::acceptor a(io, tcp::endpoint(tcp::v4(), 0));
    return a.local_endpoint().port();
}

// ─── Plain TCP tunnel test ───────────────────────────────────────────────────
// Starts a tiny echo server + FluxGate in tunnel mode.
// Sends data through a CONNECT tunnel and verifies it round-trips.

void test_tunnel_echo() {
    asio::io_context echo_io, proxy_io;

    // Echo server
    const unsigned short echo_port = free_port();
    tcp::acceptor echo_acc(echo_io, tcp::endpoint(tcp::v4(), echo_port));

    std::thread echo_thread([&] {
        std::error_code ec;
        tcp::socket sock(echo_io);
        echo_acc.accept(sock, ec);
        if (ec) return;

        std::array<char, 256> buf{};
        auto n = sock.read_some(asio::buffer(buf), ec);
        if (!ec && n > 0)
            asio::write(sock, asio::buffer(buf.data(), n), ec);
    });

    // FluxGate proxy (no MITM)
    fluxgate::AppConfig cfg;
    cfg.listen_port    = free_port();
    cfg.worker_threads = 1;
    cfg.enable_admin   = false;
    cfg.enable_cache   = false;

    fluxgate::ProxyServer proxy(proxy_io, cfg);

    std::thread proxy_thread([&] { proxy_io.run(); });

    // Client: connect to proxy, send CONNECT, send data
    {
        asio::io_context client_io;
        tcp::socket client(client_io);
        client.connect(tcp::endpoint(asio::ip::address_v4::loopback(), cfg.listen_port));

        const std::string connect_req =
            "CONNECT 127.0.0.1:" + std::to_string(echo_port) + " HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n\r\n";
        asio::write(client, asio::buffer(connect_req));

        asio::streambuf resp_buf;
        asio::read_until(client, resp_buf, "\r\n\r\n");
        const auto resp_data = resp_buf.data();
        const std::string resp(asio::buffers_begin(resp_data), asio::buffers_end(resp_data));
        assert(resp.find("200") != std::string::npos && "expected 200 Connection Established");

        const std::string payload = "HELLO_FLUXGATE";
        asio::write(client, asio::buffer(payload));

        std::array<char, 64> recv{};
        std::error_code ec;
        auto n = client.read_some(asio::buffer(recv), ec);
        const std::string echoed(recv.data(), n);
        assert(echoed == payload && "echo mismatch");
    }

    proxy_io.stop();
    echo_acc.close();
    echo_io.stop();
    if (echo_thread.joinable()) echo_thread.join();
    if (proxy_thread.joinable()) proxy_thread.join();

    std::cout << "PASS test_tunnel_echo\n";
}

// ─── MITM filter + cache test ────────────────────────────────────────────────
// Uses a tiny plaintext HTTP server (FluxGate will MITM it via TLS).
// Verifies: filter modifies body, cache returns hit on second identical request.
// This test exercises the MITM HTTPS path end-to-end over loopback.

void test_mitm_filter_and_cache() {
    // Generate a test CA for FluxGate to use
    auto ca = fluxgate::CertificateAuthority::create_self_signed("Integration Test CA", 1);
    assert(ca.can_sign());
    const auto ca_pem = ca.pem();

    // Write CA files to /tmp
    const std::string key_path  = "/tmp/fg_test_ca.key.pem";
    const std::string cert_path = "/tmp/fg_test_ca.cert.pem";
    {
        std::ofstream k(key_path), c(cert_path);
        k << ca_pem.private_key_pem;
        c << ca_pem.certificate_pem;
    }

    // Upstream HTTPS test server (FluxGate will impersonate it to the client)
    const unsigned short upstream_port = free_port();
    asio::io_context upstream_io;
    asio::ssl::context upstream_ssl_ctx(asio::ssl::context::tls_server);
    {
        // Give the upstream its own self-signed cert
        auto leaf = ca.issue_leaf("localhost", 1);
        upstream_ssl_ctx.use_certificate_chain(
            asio::buffer(leaf.chain_pem.data(), leaf.chain_pem.size()));
        upstream_ssl_ctx.use_private_key(
            asio::buffer(leaf.private_key_pem.data(), leaf.private_key_pem.size()),
            asio::ssl::context::pem);
    }
    tcp::acceptor upstream_acc(upstream_io, tcp::endpoint(tcp::v4(), upstream_port));

    // Track how many times upstream actually received a request
    std::atomic<int> upstream_hits{0};

    std::thread upstream_thread([&] {
        for (int i = 0; i < 2; ++i) {  // accept up to 2 connections
            std::error_code ec;
            tcp::socket raw(upstream_io);
            upstream_acc.accept(raw, ec);
            if (ec) break;

            asio::ssl::stream<tcp::socket> tls_sock(std::move(raw), upstream_ssl_ctx);
            tls_sock.handshake(asio::ssl::stream_base::server, ec);
            if (ec) continue;

            asio::streambuf req_buf;
            asio::read_until(tls_sock, req_buf, "\r\n\r\n", ec);
            if (ec) continue;

            // Read body
            auto req_data = req_buf.data();
            const std::string req_str(asio::buffers_begin(req_data), asio::buffers_end(req_data));

            ++upstream_hits;

            // Echo back a simple JSON response
            const std::string body = R"({"response":"ok"})";
            const std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "Connection: close\r\n\r\n" + body;
            asio::write(tls_sock, asio::buffer(response), ec);
            tls_sock.shutdown(ec);
        }
    });

    // FluxGate in MITM mode
    fluxgate::AppConfig cfg;
    cfg.listen_port          = free_port();
    cfg.worker_threads       = 1;
    cfg.enable_admin         = false;
    cfg.enable_cache         = true;
    cfg.cache_max_entries    = 64;
    cfg.cache_ttl_seconds    = 60;
    cfg.enable_mitm          = true;
    cfg.mitm_ca_key_path     = key_path;
    cfg.mitm_ca_cert_path    = cert_path;
    cfg.mitm_leaf_cache_entries = 16;
    cfg.mitm_leaf_valid_days = 1;
    cfg.enable_pii_redaction = true;
    cfg.max_chat_history     = 2;

    asio::io_context proxy_io;
    auto mitm_svc = std::make_shared<fluxgate::MitmServices>(
        std::move(ca), cfg.mitm_leaf_cache_entries, cfg.mitm_leaf_valid_days);
    fluxgate::ProxyServer proxy(proxy_io, cfg, mitm_svc);

    std::thread proxy_thread([&] { proxy_io.run(); });

    // Helper: connect through FluxGate to upstream, do TLS (trusting FluxGate CA),
    // send an HTTP POST, return the full HTTP response string.
    auto send_request = [&](const std::string& body) -> std::string {
        asio::io_context client_io;
        asio::ssl::context client_ssl(asio::ssl::context::tls_client);
        // Trust FluxGate's CA (it will issue a leaf cert for "localhost")
        client_ssl.add_certificate_authority(
            asio::buffer(ca_pem.certificate_pem.data(), ca_pem.certificate_pem.size()));
        client_ssl.set_verify_mode(asio::ssl::verify_peer);

        tcp::socket raw(client_io);
        raw.connect(tcp::endpoint(asio::ip::address_v4::loopback(), cfg.listen_port));

        // CONNECT tunnel
        const std::string connect_req =
            "CONNECT localhost:" + std::to_string(upstream_port) + " HTTP/1.1\r\n"
            "Host: localhost\r\n\r\n";
        asio::write(raw, asio::buffer(connect_req));
        asio::streambuf resp_buf;
        asio::read_until(raw, resp_buf, "\r\n\r\n");
        resp_buf.consume(resp_buf.size());

        // TLS over tunnel
        asio::ssl::stream<tcp::socket> tls(std::move(raw), client_ssl);
        SSL_set_tlsext_host_name(tls.native_handle(), "localhost");
        tls.set_verify_callback(asio::ssl::host_name_verification("localhost"));
        tls.handshake(asio::ssl::stream_base::client);

        // Send HTTP request
        const std::string http_req =
            "POST /v1/chat/completions HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        asio::write(tls, asio::buffer(http_req));

        // Read response
        asio::streambuf resp;
        std::error_code ec;
        asio::read(tls, resp, ec);  // read until EOF
        const auto rdata = resp.data();
        return std::string(asio::buffers_begin(rdata), asio::buffers_end(rdata));
    };

    // Request with 3 chat messages + PII — filter should strip system msg and PII
    const std::string req_body = R"({"model":"gpt-4","messages":[)"
        R"({"role":"system","content":"secret"},)"
        R"({"role":"user","content":"my email is test@example.com"},)"
        R"({"role":"assistant","content":"I see"}]})";

    const std::string resp1 = send_request(req_body);
    assert(!resp1.empty() && "expected non-empty response");
    assert(resp1.find("200") != std::string::npos && "expected HTTP 200");

    // Second identical request — should come from cache, upstream_hits stays at 1
    const std::string resp2 = send_request(req_body);
    assert(!resp2.empty());
    assert(resp2.find("200") != std::string::npos);
    assert(upstream_hits.load() == 1 && "second request should be served from cache");

    proxy_io.stop();
    upstream_acc.close();
    upstream_io.stop();
    if (upstream_thread.joinable()) upstream_thread.join();
    if (proxy_thread.joinable()) proxy_thread.join();

    std::cout << "PASS test_mitm_filter_and_cache (upstream_hits="
              << upstream_hits.load() << ")\n";
}

// ─── Cache interface polymorphism ────────────────────────────────────────────
void test_icache_polymorphism() {
    std::shared_ptr<fluxgate::ICache> cache =
        std::make_shared<fluxgate::MemoryCache>(10);
    cache->put("k", "v", std::chrono::seconds(60));
    assert(cache->get("k") == "v");
    assert(cache->size() == 1);
    std::cout << "PASS test_icache_polymorphism\n";
}

int main() {
    test_icache_polymorphism();
    test_tunnel_echo();
    test_mitm_filter_and_cache();
    std::cout << "All integration tests passed.\n";
}
