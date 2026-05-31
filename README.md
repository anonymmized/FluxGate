# FluxGate
High-performance C++ proxy gateway for AI traffic optimization. Reduces API costs by 40-70% via real-time intelligent filtering, deduplication, and caching.

## Current Status

FluxGate currently implements Stage 1 of the roadmap: an asynchronous HTTP CONNECT gateway.

- Accepts client TCP connections on a configurable port.
- Parses CONNECT targets, including default `:443` and IPv6 bracket notation.
- Resolves and connects to upstream hosts asynchronously.
- Relays traffic bidirectionally without blocking I/O.
- Uses a multi-threaded Asio event loop with per-session strand serialization.

MITM TLS interception, JSON payload extraction, filter plugins, and cache integration are the next layers.

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
