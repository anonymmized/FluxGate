# FluxGate

[![CI](https://github.com/anonymmized/FluxGate/actions/workflows/ci.yml/badge.svg)](https://github.com/anonymmized/FluxGate/actions/workflows/ci.yml)

> **Cut your LLM API bill by 40–70% without touching your application code.**

FluxGate is a high-performance C++20 transparent HTTPS proxy that sits between your application and any LLM API (OpenAI, Anthropic, Gemini, etc.). It intercepts requests, strips redundant tokens, and caches identical queries — all with sub-millisecond local overhead.

## How it works

```
Your App  ──HTTPS──▶  FluxGate (local)  ──HTTPS──▶  OpenAI / Anthropic / etc.
                           │
                    ┌──────▼──────┐
                    │  PII Strip  │   removes emails, phone numbers
                    │  History    │   truncates chat history to N messages
                    │  Cache      │   exact-match: zero API calls for repeats
                    └─────────────┘
```

FluxGate terminates TLS from your client (MITM with a local CA you generate yourself), inspects and optionally modifies the JSON payload, then opens a fresh TLS connection to the real upstream.

## Features

- **Token savings** — chat history limit filter drops old messages before they reach the API
- **PII redaction** — strips email addresses and phone numbers from request bodies
- **Smart caching** — LRU + TTL cache; identical requests never hit the API twice
- **Async C++20** — standalone Asio, no blocking I/O, scales to thousands of concurrent sessions
- **MITM TLS** — per-host certificate generation signed by your local CA, leaf cert cache
- **Prometheus metrics** — `/metrics` exposes sessions, bytes, cache hits/misses, filtered requests
- **Health endpoint** — `/healthz` for load balancer probes
- **Zero dependencies at runtime** — single static binary, no external services needed

## Quick start

### Option A — Docker (recommended)

```sh
# Generate CA (one-time setup)
docker run --rm -v $(pwd)/certs:/certs fluxgate \
  --generate-ca /certs/fluxgate-ca

# Copy fluxgate.toml.example → fluxgate.toml and adjust paths
cp fluxgate.toml.example fluxgate.toml

docker compose up -d
```

### Option B — Build from source

```sh
cmake -S . -B build && cmake --build build --parallel
```

Requires: C++20 compiler, CMake ≥ 3.15, OpenSSL. `simdjson` and `toml++` are fetched automatically.

**With Redis cache backend:**
```sh
cmake -S . -B build -DFLUXGATE_REDIS=ON
```

### 2. Generate a local CA

```sh
./build/FluxGate --generate-ca ./fluxgate-ca
# writes fluxgate-ca.key.pem and fluxgate-ca.cert.pem
```

Install `fluxgate-ca.cert.pem` into your OS/browser trust store **once**. This is the only setup step.

### 3. Run FluxGate

```sh
./build/FluxGate \
  --listen 127.0.0.1 --port 8080 \
  --mitm \
  --mitm-ca-key ./fluxgate-ca.key.pem \
  --mitm-ca-cert ./fluxgate-ca.cert.pem \
  --max-history 20 \
  --cache-ttl 300
```

### 4. Point your app at the proxy

```python
# Python / openai-python
import openai, httpx
client = openai.OpenAI(
    http_client=httpx.Client(proxies="http://127.0.0.1:8080")
)
```

```sh
# curl
curl --proxy http://127.0.0.1:8080 https://api.openai.com/v1/chat/completions ...
```

## CLI reference

| Flag | Default | Description |
|------|---------|-------------|
| `--listen host` | `0.0.0.0` | Bind address |
| `--port N` | `8080` | Listen port |
| `--threads N` | CPU count | Worker threads |
| `--mitm` | off | Enable TLS interception |
| `--mitm-ca-key path` | — | CA private key PEM |
| `--mitm-ca-cert path` | — | CA certificate PEM |
| `--max-history N` | `20` | Max chat messages kept (0 = unlimited) |
| `--no-pii` | on | Disable PII redaction |
| `--max-body N` | `4194304` | Max intercepted body size (bytes) |
| `--cache-ttl N` | `300` | Cache TTL in seconds |
| `--no-cache` | on | Disable response cache |
| `--cache-max-entries N` | `4096` | Max cached responses |
| `--admin host:port` | `127.0.0.1:9090` | Admin/metrics endpoint |
| `--no-admin` | on | Disable admin endpoint |
| `--allow host` | — | Add host to MITM allowlist (repeatable) |
| `--deny host` | — | Add host to MITM denylist (repeatable) |
| `--config path` | — | Load settings from TOML file |
| `--dump-config` | — | Print current config as TOML and exit |
| `--admin-token secret` | — | Require `Authorization: Bearer secret` on admin endpoints |
| `--generate-ca prefix` | — | Generate a new local CA and exit |

## Admin endpoints

```sh
curl http://127.0.0.1:9090/healthz   # → "ok"
curl http://127.0.0.1:9090/metrics   # → Prometheus text format
```

Metrics include: sessions accepted/active/rejected, bytes relayed, cache hits/misses, filtered requests, upstream failures.

## Architecture

```
ProxyServer (Asio acceptor)
  └── Session (per connection, strand-serialized)
        ├── Plain tunnel path  (CONNECT → TCP relay, no interception)
        └── MITM path (--mitm flag)
              ├── MitmTlsFactory  → per-host leaf cert signed by local CA
              ├── TLS handshake with client  (server-side)
              ├── Body read  (Content-Length driven, max_body_bytes cap)
              ├── FilterPipeline
              │     ├── PiiRedactionFilter   (regex, email + phone)
              │     └── ChatHistoryLimitFilter  (simdjson DOM, keeps last N)
              ├── MemoryCache  (LRU + TTL, shared across sessions)
              ├── TLS connect to real upstream  (client-side, verify_peer + SNI)
              └── Bidirectional relay + response caching
```

## Build & test

```sh
cmake -S . -B build && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Roadmap

- [x] Stage 1 — Async TCP/SSL CONNECT gateway
- [x] Stage 2 — TLS MITM interception, body capture, JSON parsing
- [x] Stage 3 — Filter pipeline (PII redaction, chat history truncation)
- [x] Stage 3 — In-memory LRU/TTL response cache with Prometheus metrics
- [x] Stage 4 — TOML config file, provider allowlist/denylist, --dump-config
- [x] Stage 4 — Docker image + docker-compose, GitHub Actions CI (Linux + macOS)
- [x] Stage 5 — Structured NDJSON logging (request ID, host, method, path, upstream_ms)
- [x] Stage 5 — Admin API Bearer token auth (`--admin-token` / `admin.token` in TOML)
- [x] Stage 5 — GitHub Releases workflow: binaries for linux-amd64, macos-arm64, macos-amd64 + SHA256SUMS
- [x] Stage 5 — systemd unit file with hardening
- [x] Stage 6 — Redis cache backend (`--redis-url` / `cache.backend = "redis"`, `-DFLUXGATE_REDIS=ON`)
- [x] Stage 6 — Integration tests: real end-to-end tunnel + MITM filter+cache round-trip
- [ ] Stage 7 — HTTP/2 upstream support
- [ ] Stage 7 — Plugin ABI for custom filter extensions

## Security model

FluxGate operates as a **local** MITM proxy. The generated CA certificate should only be trusted on the machine running FluxGate. Never distribute the CA private key. Review [docs/MITM_CA.md](docs/MITM_CA.md) before deploying in any shared or production environment.

## License

See LICENSE file.
