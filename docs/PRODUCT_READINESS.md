# FluxGate Product Readiness

This document is the engineering gate for turning FluxGate from a prototype into a sellable product.

## Current Capabilities

- Asynchronous C++20 HTTP CONNECT proxy built on standalone Asio.
- Multi-threaded event loop with per-session serialized handlers.
- Configurable listen address, port, worker count, and local cache sizing.
- CONNECT parser with support for explicit ports, default `443`, and IPv6 authority syntax.
- Bidirectional tunnel relay with no blocking I/O in request handling.
- Testable core modules for HTTP request-head parsing, filter pipelines, PII redaction, chat-history trimming, and in-memory TTL/LRU cache behavior.
- Admin HTTP endpoint with `/healthz` and Prometheus-style `/metrics`.
- OpenSSL-backed local CA generation, PEM loading, per-host leaf certificate issuing with SAN support, bounded leaf certificate caching, and generated server TLS context creation.
- Opt-in MITM scaffold that completes CONNECT, performs server-side TLS with generated per-host certificates, and parses decrypted HTTP request heads.

## Not Yet Sellable

FluxGate is not ready for paid external customers until these blockers are closed:

- TLS MITM interception is only partially implemented. FluxGate can complete server-side TLS and inspect request headers, but it does not yet forward decrypted requests upstream or inspect bodies.
- Dynamic certificate generation and server context creation exist, but they do not yet include secure key storage, rotation, or trust-store UX.
- JSON filters are not wired into intercepted HTTPS request/response flows.
- The current chat-history filter is a lightweight structural heuristic, not a full JSON parser.
- No authentication exists for the admin API.
- No production logging backend exists; metrics are present but intentionally minimal.
- No packaging, installer, Docker image, systemd/launchd service, or signed release artifacts exist.
- No load, fuzz, soak, or security testing has been completed.
- Legal/compliance documentation for MITM deployment and data processing is missing.

## Private Beta Gate

Minimum criteria for a private technical beta:

- Full TLS MITM for HTTP/1.1 upstream traffic with explicit opt-in CA installation.
- Secure CA key loading from runtime config.
- Request and response body capture with strict max-body limits.
- JSON parsing through a real parser such as simdjson or RapidJSON.
- Filter pipeline wired into intercepted OpenAI-compatible chat/completions traffic.
- Local cache for deterministic safe responses, with opt-in policy.
- Structured logs with request IDs and no accidental body logging.
- Integration tests with a local HTTPS upstream fixture.

## Paid Pilot Gate

Minimum criteria for a paid pilot:

- Config file support with documented schema.
- Admin health/metrics endpoint with documented deployment guidance.
- Provider allowlist and denylist.
- PII redaction policy controls.
- Cache controls with TTL, max-size, and bypass rules.
- Docker image and reproducible release build.
- Load test report covering concurrency, memory growth, and p95/p99 latency.
- Security review of certificate storage, key permissions, and logging.

## General Availability Gate

Minimum criteria for broad sale:

- Full MITM lifecycle UX: CA creation, certificate rotation, trust-store documentation, and uninstall path.
- HTTP/2 strategy, either supported or explicitly downgraded/disabled with documented behavior.
- Plugin ABI or stable in-process plugin API.
- Persistent cache backend option, such as Redis.
- Signed binaries or packages for target platforms.
- SBOM and dependency license review.
- Operational runbooks, troubleshooting docs, and customer deployment examples.
- End-user license terms, privacy policy, and enterprise security documentation.
