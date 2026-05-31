# FluxGate
High-performance C++ proxy gateway for AI traffic optimization. Reduces API costs by 40-70% via real-time intelligent filtering, deduplication, and caching.

## Current Status

FluxGate currently implements the production base for Stage 1 of the roadmap: an asynchronous HTTP CONNECT gateway plus testable core primitives for later AI traffic optimization.

- Accepts client TCP connections on a configurable port.
- Parses CONNECT targets, including default `:443` and IPv6 bracket notation.
- Resolves and connects to upstream hosts asynchronously.
- Relays traffic bidirectionally without blocking I/O.
- Uses a multi-threaded Asio event loop with per-session strand serialization.
- Provides reusable parser, filter-pipeline, and local memory-cache modules.
- Provides an admin endpoint with `/healthz` and Prometheus-style `/metrics`.
- Provides local CA generation, per-host leaf-certificate caching, and server TLS context creation primitives for the upcoming MITM layer.
- Includes CTest unit tests for parsers, filters, and cache behavior.

MITM TLS handshakes inside CONNECT sessions are available as an opt-in scaffold. Upstream TLS forwarding, JSON payload extraction, filter wiring, and cache integration are the next layers.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/FluxGate 8080
```

Then configure an HTTPS client to use `127.0.0.1:8080` as an HTTP proxy.

CLI options:

```sh
./build/FluxGate --listen 127.0.0.1 --port 8080 --threads 4 --admin 127.0.0.1:9090
```

Admin endpoints:

```sh
curl http://127.0.0.1:9090/healthz
curl http://127.0.0.1:9090/metrics
```

Generate a local root CA for future MITM mode:

```sh
./build/FluxGate --generate-ca ./fluxgate-local-ca --ca-common-name "FluxGate Local Root CA"
```

This writes `fluxgate-local-ca.key.pem` and `fluxgate-local-ca.cert.pem`. Do not install the CA into a trust store except in explicit test or controlled enterprise deployments.

Run the MITM handshake scaffold:

```sh
./build/FluxGate \
  --listen 127.0.0.1 \
  --port 8080 \
  --mitm \
  --mitm-ca-key ./fluxgate-local-ca.key.pem \
  --mitm-ca-cert ./fluxgate-local-ca.cert.pem
```

In this mode FluxGate performs CONNECT, presents a generated per-host leaf certificate, completes server-side TLS with the client, reads the decrypted HTTP request head, and returns `501 Not Implemented` until upstream forwarding is wired.

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Product Readiness

See [docs/PRODUCT_READINESS.md](docs/PRODUCT_READINESS.md) for the release criteria required before FluxGate can be sold.
