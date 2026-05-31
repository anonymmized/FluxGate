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
    fluxgate::FilterPipeline pipeline;
    pipeline.add(std::make_unique<fluxgate::PiiRedactionFilter>());
    pipeline.add(std::make_unique<fluxgate::ChatHistoryLimitFilter>(2));

    auto request = fluxgate::parse_http_request_head("POST /v1/chat/completions HTTP/1.1\r\n\r\n");
    assert(request);

    std::string body = R"({"messages":[{"role":"system","content":"keep policy"},{"role":"user","content":"a@b.com"},{"role":"assistant","content":"+1 202 555 0199"}]})";
    auto result = pipeline.apply({}, *request, body);
    assert(result.modified);
    assert(body.find("[REDACTED_EMAIL]") != std::string::npos || body.find("[REDACTED_PHONE]") != std::string::npos);
    assert(body.find(R"("system")") == std::string::npos);
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
    test_cache();
    test_config_mitm_args();
    test_config_mitm_requires_ca_paths();
    test_metrics();
    test_certificate_authority();
    test_leaf_certificate_cache();
    test_mitm_tls_factory();
}
