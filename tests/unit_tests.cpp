#include "fluxgate/cache.h"
#include "fluxgate/cert_authority.h"
#include "fluxgate/config.h"
#include "fluxgate/connect_parser.h"
#include "fluxgate/filter.h"
#include "fluxgate/http_message.h"
#include "fluxgate/leaf_certificate_cache.h"
#include "fluxgate/metrics.h"
#include "fluxgate/mitm_tls.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

namespace {

void test_connect_parser() {
    auto target = fluxgate::parse_connect_request("CONNECT api.openai.com:443 HTTP/1.1\r\nHost: api.openai.com\r\n\r\n");
    assert(target);
    assert(target->host == "api.openai.com");
    assert(target->port == "443");

    target = fluxgate::parse_connect_request("CONNECT example.com HTTP/1.1\r\n\r\n");
    assert(target);
    assert(target->host == "example.com");
    assert(target->port == "443");

    target = fluxgate::parse_connect_request("CONNECT [::1]:8443 HTTP/1.1\r\n\r\n");
    assert(target);
    assert(target->host == "::1");
    assert(target->port == "8443");

    assert(!fluxgate::parse_connect_request("GET / HTTP/1.1\r\n\r\n"));
}

void test_http_parser() {
    auto request = fluxgate::parse_http_request_head(
        "POST /v1/chat/completions HTTP/1.1\r\nHost: api.openai.com\r\nContent-Type: application/json\r\n\r\n{}");
    assert(request);
    assert(request->method == "POST");
    assert(request->target == "/v1/chat/completions");
    auto host = fluxgate::header_value(*request, "host");
    assert(host);
    assert(*host == "api.openai.com");
}

void test_filters() {
    fluxgate::AppConfig cfg;
    cfg.enable_pii_redaction = true;
    cfg.max_chat_history = 2;
    auto controls = std::make_shared<fluxgate::RuntimeControls>(cfg);

    fluxgate::FilterPipeline pipeline;
    pipeline.add(std::make_unique<fluxgate::PiiRedactionFilter>(controls));
    pipeline.add(std::make_unique<fluxgate::ChatHistoryLimitFilter>(controls));

    auto request = fluxgate::parse_http_request_head("POST /v1/chat/completions HTTP/1.1\r\n\r\n");
    assert(request);

    std::string body = R"({"messages":[{"role":"system","content":"keep policy"},{"role":"user","content":"a@b.com"},{"role":"assistant","content":"+1 202 555 0199"}]})";
    auto result = pipeline.apply({}, *request, body);
    assert(result.modified);
    assert(body.find("[REDACTED_EMAIL]") != std::string::npos || body.find("[REDACTED_PHONE]") != std::string::npos);
    assert(body.find(R"("system")") == std::string::npos);

    // Live toggle: disabling PII via controls should stop redaction.
    controls->pii_redaction.store(false);
    std::string body2 = R"({"messages":[{"role":"user","content":"reach me at a@b.com"}]})";
    auto r2 = pipeline.apply({}, *request, body2);
    assert(body2.find("a@b.com") != std::string::npos);
    (void)r2;
}

void test_normalized_cache_key() {
    using fluxgate::normalized_cache_key;
    // Same JSON, different key order + whitespace → same key.
    const auto a = normalized_cache_key("POST", "h/p", R"({"a":1,"b":2})");
    const auto b = normalized_cache_key("POST", "h/p", R"({ "b" : 2, "a" : 1 })");
    assert(a == b);
    const auto c = normalized_cache_key("POST", "h/p", R"({"a":1,"b":3})");
    assert(a != c);
}

void test_cache() {
    fluxgate::MemoryCache cache(1);
    cache.put("a", "first", std::chrono::seconds(10));
    assert(cache.get("a") == "first");
    cache.put("b", "second", std::chrono::seconds(10));
    assert(!cache.get("a"));
    assert(cache.get("b") == "second");

    fluxgate::MemoryCache ttl_cache(4);
    ttl_cache.put("x", "expired", std::chrono::seconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(!ttl_cache.get("x"));
}

void test_config_mitm_args() {
    const char* argv[] = {
        "FluxGateTests",
        "--mitm",
        "--mitm-ca-key",
        "/tmp/ca.key.pem",
        "--mitm-ca-cert",
        "/tmp/ca.cert.pem",
        "--mitm-leaf-cache-entries",
        "32",
        "--mitm-leaf-valid-days",
        "3",
    };

    auto config = fluxgate::parse_args(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    assert(config.enable_mitm);
    assert(config.mitm_ca_key_path == "/tmp/ca.key.pem");
    assert(config.mitm_ca_cert_path == "/tmp/ca.cert.pem");
    assert(config.mitm_leaf_cache_entries == 32);
    assert(config.mitm_leaf_valid_days == 3);
}

void test_config_mitm_requires_ca_paths() {
    const char* argv[] = {"FluxGateTests", "--mitm"};
    bool threw = false;
    try {
        (void)fluxgate::parse_args(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void test_config_provider_filtering() {
    // allow + deny via CLI
    const char* argv[] = {
        "FluxGateTests",
        "--allow", "api.openai.com",
        "--allow", "api.anthropic.com",
        "--deny", "internal.corp",
    };
    auto config = fluxgate::parse_args(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    assert(config.provider_allowlist.size() == 2);
    assert(config.provider_allowlist[0] == "api.openai.com");
    assert(config.provider_allowlist[1] == "api.anthropic.com");
    assert(config.provider_denylist.size() == 1);
    assert(config.provider_denylist[0] == "internal.corp");
}

void test_config_dump_toml() {
    fluxgate::AppConfig cfg;
    cfg.provider_allowlist = {"api.openai.com", "api.anthropic.com"};
    const auto toml = fluxgate::dump_config_toml(cfg);
    assert(toml.find("[proxy]") != std::string::npos);
    assert(toml.find("[tls]") != std::string::npos);
    assert(toml.find("[filters]") != std::string::npos);
    assert(toml.find("[cache]") != std::string::npos);
    assert(toml.find("[providers]") != std::string::npos);
    assert(toml.find("api.openai.com") != std::string::npos);
}

void test_config_file_toml(const std::string& tmpdir) {
    // Write a minimal TOML config file and verify it loads correctly
    const std::string path = tmpdir + "/fluxgate_test.toml";
    {
        std::ofstream f(path);
        f << "[proxy]\nlisten = \"127.0.0.1\"\nport = 9999\n"
          << "[filters]\npii_redaction = false\nmax_chat_history = 5\n"
          << "[providers]\nallowlist = [\"api.openai.com\"]\ndenylist = [\"bad.host\"]\n";
    }
    const char* argv[] = {"FluxGateTests", "--config", path.c_str()};
    auto config = fluxgate::parse_args(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
    assert(config.listen_host == "127.0.0.1");
    assert(config.listen_port == 9999);
    assert(!config.enable_pii_redaction);
    assert(config.max_chat_history == 5);
    assert(config.provider_allowlist.size() == 1);
    assert(config.provider_allowlist[0] == "api.openai.com");
    assert(config.provider_denylist.size() == 1);
    assert(config.provider_denylist[0] == "bad.host");
}

void test_metrics() {
    fluxgate::Metrics metrics;
    metrics.on_session_accepted();
    metrics.add_client_to_upstream_bytes(10);
    metrics.add_upstream_to_client_bytes(20);
    metrics.on_session_rejected();
    metrics.on_upstream_connect_failure();

    auto snapshot = metrics.snapshot();
    assert(snapshot.accepted_sessions == 1);
    assert(snapshot.active_sessions == 1);
    assert(snapshot.rejected_sessions == 1);
    assert(snapshot.upstream_connect_failures == 1);
    assert(snapshot.client_to_upstream_bytes == 10);
    assert(snapshot.upstream_to_client_bytes == 20);

    metrics.on_session_closed();
    snapshot = metrics.snapshot();
    assert(snapshot.active_sessions == 0);

    const auto text = fluxgate::to_prometheus_text(snapshot);
    assert(text.find("fluxgate_sessions_accepted_total 1") != std::string::npos);
}

void test_certificate_authority() {
    auto ca = fluxgate::CertificateAuthority::create_self_signed("FluxGate Test Root CA", 30);
    assert(ca.can_sign());

    const auto ca_pem = ca.pem();
    assert(ca_pem.private_key_pem.find("BEGIN PRIVATE KEY") != std::string::npos);
    assert(ca_pem.certificate_pem.find("BEGIN CERTIFICATE") != std::string::npos);

    auto loaded_ca = fluxgate::CertificateAuthority::from_pem(ca_pem.private_key_pem, ca_pem.certificate_pem);
    assert(loaded_ca.can_sign());

    const auto leaf = loaded_ca.issue_leaf("api.openai.com", 7);
    assert(leaf.private_key_pem.find("BEGIN PRIVATE KEY") != std::string::npos);
    assert(leaf.certificate_pem.find("BEGIN CERTIFICATE") != std::string::npos);
    assert(leaf.chain_pem.find(ca_pem.certificate_pem) != std::string::npos);
    assert(fluxgate::certificate_has_dns_subject_alt_name(leaf.certificate_pem, "api.openai.com"));
}

void test_leaf_certificate_cache() {
    auto ca = fluxgate::CertificateAuthority::create_self_signed("FluxGate Cache Test Root CA", 30);
    fluxgate::LeafCertificateCache cache(ca, 1, 7);

    const auto first = cache.get("api.openai.com");
    const auto second = cache.get("api.openai.com");
    assert(first.certificate_pem == second.certificate_pem);
    assert(cache.size() == 1);

    const auto other = cache.get("api.anthropic.com");
    assert(other.certificate_pem != first.certificate_pem);
    assert(cache.size() == 1);
}

void test_mitm_tls_factory() {
    auto ca = fluxgate::CertificateAuthority::create_self_signed("FluxGate TLS Test Root CA", 30);
    fluxgate::LeafCertificateCache cache(ca, 8, 7);
    fluxgate::MitmTlsFactory factory(cache);

    auto context = factory.create_server_context("api.openai.com");
    assert(context != nullptr);
    assert(cache.size() == 1);
}

} // namespace

int main() {
    test_connect_parser();
    test_http_parser();
    test_filters();
    test_normalized_cache_key();
    test_cache();
    test_config_mitm_args();
    test_config_mitm_requires_ca_paths();
    test_config_provider_filtering();
    test_config_dump_toml();
    test_config_file_toml("/tmp");
    test_metrics();
    test_certificate_authority();
    test_leaf_certificate_cache();
    test_mitm_tls_factory();
}
