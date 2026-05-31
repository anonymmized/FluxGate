# FluxGate Architecture

## Process Model

FluxGate runs one Asio `io_context` across a configurable worker pool. Accepted client sessions own their client socket, upstream socket, DNS resolver, stream buffer, relay buffers, and session state. Session handlers are bound through an Asio strand so that state transitions remain serialized even when the event loop has multiple worker threads.

## Current Data Flow

```text
client -> TCP accept -> CONNECT parser -> async DNS -> async upstream connect -> bidirectional relay
```

By default, the gateway is a transparent CONNECT tunnel. With `--mitm`, it establishes CONNECT, performs server-side TLS using a generated leaf certificate, reads the decrypted HTTP request head, and returns a temporary `501` response while upstream forwarding is under development.

## Target Data Flow

```text
client
  -> TCP accept
  -> CONNECT parser
  -> load or create local CA
  -> get cached per-host leaf certificate
  -> create server TLS context
  -> local TLS server handshake using generated leaf cert
  -> HTTP parser
  -> upstream TLS client
  -> body limits
  -> JSON parser
  -> filter pipeline
  -> cache lookup/write policy
  -> response filter
  -> client response
```

## Core Modules

- `connect_parser`: CONNECT authority parsing.
- `http_message`: request-head parsing helpers for intercepted HTTP/1.1 traffic.
- `filter`: in-process request filter interface and initial filters.
- `cache`: bounded in-memory TTL/LRU cache.
- `cert_authority`: OpenSSL-backed local CA and per-host leaf certificate generation.
- `leaf_certificate_cache`: bounded per-host leaf certificate cache.
- `mitm_tls`: `asio::ssl::context` creation for generated leaf certificates.
- `metrics`: atomic counters and Prometheus text rendering.
- `proxy_server`: Asio listener and tunnel session implementation.
- `admin_server`: lightweight local HTTP endpoint for health and metrics.
- `config`: command-line configuration parsing.

## Performance Rules

- No blocking I/O in request paths.
- Bounded buffers for headers and relay operations.
- Explicit body-size limits before JSON parsing.
- No body logging by default.
- CA private keys must never be logged.
- Leaf certificates must be cached with bounded size and validity.
- Filters must be deterministic, bounded, and test-covered.
