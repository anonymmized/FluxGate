<div align="center">

# ⚡ FluxGate

### Cut your LLM API bill by 40–70% — without touching your code.

[![CI](https://github.com/anonymmized/FluxGate/actions/workflows/ci.yml/badge.svg)](https://github.com/anonymmized/FluxGate/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/anonymmized/FluxGate?color=7c3aed)](https://github.com/anonymmized/FluxGate/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)

A high-performance **C++20 HTTPS proxy** that sits between your app and any LLM API
(OpenAI · Anthropic · Gemini · Mistral · Groq). It **caches** identical requests,
**trims** chat history, and **redacts** PII — all locally, with sub-millisecond overhead.

[Quick start](#-quick-start) · [How it works](#-how-it-works) · [Dashboard](#-dashboards) · [Config](#️-configuration) · [Docker](#-docker) · [Website](https://anonymmized.github.io/FluxGate/)

</div>

---

## 🚀 Quick start

```sh
# 1. Install (downloads the binary, generates & trusts a local CA)
curl -fsSL https://raw.githubusercontent.com/anonymmized/FluxGate/main/install.sh | bash

# 2. Run — an interactive wizard sets everything up (just press Enter through it)
fluxgate

# 3. Point your app at the proxy
export HTTPS_PROXY=http://127.0.0.1:8080
```

Run your app as usual and watch the savings live at **http://127.0.0.1:9090**.

> **No API key? No problem.** Pick option **7 — Everything** in the wizard, then:
> ```sh
> for i in $(seq 1 10); do curl -sk -x http://127.0.0.1:8080 https://httpbin.org/get -o /dev/null; done
> ```
> Open the dashboard — the cache hit rate climbs to ~90%.

<details>
<summary><b>Build from source instead</b></summary>

```sh
git clone https://github.com/anonymmized/FluxGate.git && cd FluxGate
cmake -S . -B build && cmake --build build --parallel
./build/FluxGate          # launches the setup wizard
```
Requires C++20, CMake ≥ 3.15, OpenSSL. `asio`, `simdjson` and `toml++` are fetched automatically.
</details>

### Use it from code

```python
import openai, httpx
client = openai.OpenAI(http_client=httpx.Client(proxies="http://127.0.0.1:8080"))
```

See [`examples/`](examples/) for OpenAI, Anthropic, and a benchmark script.

---

## 🧠 How it works

```
   Your App  ──HTTPS──▶   ⚡ FluxGate   ──HTTPS──▶   OpenAI / Anthropic / …
                              │
                    ┌─────────┴──────────┐
                    │  Provider routing   │  intercept only chosen hosts
                    │  PII redaction      │  strip emails & phone numbers
                    │  History trimming   │  keep only the last N messages
                    │  Response cache     │  identical query → 0 API calls
                    └─────────────────────┘
```

FluxGate terminates TLS from your client using a **local CA you generate yourself**,
inspects and optionally rewrites the JSON payload, then opens a fresh *verified* TLS
connection to the real provider. Nothing leaves your machine except the (optimised)
upstream request.

---

## 📊 Dashboards & control panel

| Where | What |
|-------|------|
| **Terminal** | Live TUI — cache bar, savings, traffic, rate-limit counter and your top providers, refreshed every second |
| **Browser** | `http://127.0.0.1:9090/` — a full **control panel** with four tabs |

The web control panel has four tabs:
- **Overview** — cards + live sparklines (hit rate, sessions, tokens & dollars saved, cache entries, rate-limited).
- **Requests** — a live inspector: host, method, path, model, cache hit/miss, status, latency and tokens saved per request (never body content).
- **Providers** — per-provider rollup: requests, hit rate, filtered, tokens & cost saved, avg latency, traffic.
- **Settings** — toggle PII/cache, change history limit, cache TTL, rate limits and budget, edit allow/deny lists, and clear the cache — **all applied live, no restart**.

Admin endpoints: `GET /` (panel) · `/stats` · `/providers` · `/requests` · `/history` · `/metrics` (Prometheus) · `/healthz` · `POST /api/control` · `POST /api/cache/clear`. Set an admin `token` to require Bearer auth on all of them.

### 🖥️ Desktop app (macOS & Linux)

Prefer a native window over the browser? [`desktop/`](desktop/) is an Electron app
with a sidebar UI that drives the same API — and can **start/stop the proxy** for you,
streaming its logs.

```sh
cd desktop && npm install && npm start   # dev run
npm run dist                              # build a .dmg / AppImage / .deb
```

---

## ✨ Features

- **🔁 Smart cache** — identical queries return instantly. Keys are **normalized** (sorted JSON, whitespace-insensitive) so reordered requests still hit. LRU + TTL in-memory, or Redis for multi-instance setups.
- **✂️ History trimming** — keep only the last *N* chat messages; cuts input tokens on long conversations.
- **🔒 PII & secret redaction** — strip emails, phones, credit cards, IPs and API-key-shaped secrets, plus your own custom regex rules, before they leave your network.
- **🎯 Provider routing** — intercept only the hosts you choose; everything else tunnels transparently. Editable live from the dashboard.
- **🚦 Rate limiting & quotas** — per-client (per-IP) token-bucket limits to stop runaway costs, with a monthly budget alert.
- **🔎 Request inspector + per-provider analytics** — a live request log and cost/latency/hit-rate broken down by provider and model, with **provider-aware pricing**.
- **🎛️ Live control panel** — change filters, cache TTL, limits and routing from the browser with no restart.
- **🐳 Deploy anywhere** — single static binary, Docker image, systemd unit. Linux + macOS.
- **🔍 Structured logs + Prometheus** — NDJSON with request IDs and upstream latency (never body content); every counter at `GET /metrics`.

---

## ⚙️ Configuration

The wizard writes a `fluxgate.toml` you can edit by hand; re-run with `--config fluxgate.toml`.

```toml
[proxy]
listen = "127.0.0.1"   # use "0.0.0.0" in containers
port   = 8080

[tls]
enabled = true
ca_key  = "./fluxgate-ca.key.pem"
ca_cert = "./fluxgate-ca.cert.pem"

[filters]
pii_redaction    = true
max_chat_history = 20          # 0 = unlimited
# custom_rules = [ { pattern = "secret-\\w+", replacement = "[REDACTED]" } ]

[limits]
rate_limit_rpm     = 0         # per-client requests/min; 0 = unlimited
rate_limit_burst   = 0         # 0 = defaults to rpm
monthly_budget_usd = 0         # dashboard alert threshold; 0 = off

[cache]
enabled     = true
backend     = "memory"         # or "redis"
ttl_seconds = 300
# redis_url = "redis://127.0.0.1:6379"

[admin]
enabled = true
listen  = "127.0.0.1"
port    = 9090
# token = "secret"             # require Bearer auth

[providers]
allowlist = ["api.openai.com", "api.anthropic.com"]  # empty = intercept all
denylist  = []
```

<details>
<summary><b>Command-line flags</b> (override the file)</summary>

| Flag | Description |
|------|-------------|
| `--config path` | Load settings from a TOML file |
| `--setup` | Force the interactive wizard |
| `--listen host` / `--port N` | Bind address / port |
| `--threads N` | Worker threads (default: CPU count) |
| `--mitm` `--mitm-ca-key` `--mitm-ca-cert` | Enable TLS interception |
| `--max-history N` / `--no-pii` | Filter controls |
| `--rate-limit RPM` / `--rate-limit-burst N` | Per-client rate limit |
| `--monthly-budget USD` | Budget alert threshold for the dashboard |
| `--cache-ttl N` / `--no-cache` / `--cache-backend redis` | Cache controls |
| `--allow host` / `--deny host` | Provider allowlist / denylist |
| `--admin host:port` / `--admin-token T` / `--no-admin` | Admin endpoint |
| `--generate-ca prefix` | Generate a CA and exit |
| `--dump-config` | Print current config as TOML |

Run `./build/FluxGate --help` for the full list.
</details>

---

## 🐳 Docker

> Inside a container the listeners **must** bind `0.0.0.0`, or Docker's published ports
> can't reach them and `curl -x` fails with a connection reset.

```sh
# 1. Generate a CA into ./certs (one time)
docker compose run --rm fluxgate-init        # or use the docker run form below

# 2. Start the proxy + dashboard
docker compose up -d

# 3. Test it — no API key needed
curl -sk -x http://localhost:8080 https://httpbin.org/get
```

<details>
<summary><b>Plain <code>docker run</code> (no compose)</b></summary>

```sh
# 1. Generate the CA
docker run --rm -v "$PWD/certs:/certs" --entrypoint /usr/local/bin/fluxgate \
  ghcr.io/anonymmized/fluxgate:latest --generate-ca /certs/ca

# 2. Run (note --listen 0.0.0.0 and --admin 0.0.0.0:9090)
docker run -d --name fluxgate \
  -v "$PWD/certs:/certs:ro" -p 8080:8080 -p 9090:9090 \
  ghcr.io/anonymmized/fluxgate:latest \
  --listen 0.0.0.0 --admin 0.0.0.0:9090 \
  --mitm --mitm-ca-key /certs/ca.key.pem --mitm-ca-cert /certs/ca.cert.pem
```
</details>

To verify upstream certs without `-k`, trust `./certs/ca.cert.pem` in the calling environment.

---

## 🔧 Build & test

```sh
cmake -S . -B build && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The integration suite spins up a real proxy and a local HTTPS upstream, then verifies
the MITM filter and cache end-to-end. Redis backend: `-DFLUXGATE_REDIS=ON`.

---

## 🔒 Security

FluxGate runs **locally** — your traffic never leaves your infrastructure through it,
and it never logs API keys or body content. See [`docs/SECURITY.md`](docs/SECURITY.md)
for the full trust model.

---

## 🗺️ Roadmap

Done: async CONNECT gateway · full TLS MITM with JSON parsing · filter pipeline
(PII/secret redaction + custom rules, history trim) · normalized-key cache (memory & Redis) ·
per-client rate limiting & quotas · live request inspector + per-provider analytics ·
runtime control panel · provider allow/denylist · interactive wizard + TUI ·
Docker, systemd, signed releases, CI.

Next: HTTP/2 upstream · plugin ABI for custom filters · semantic (embedding) cache.

---

## 📄 License

MIT — see [LICENSE](LICENSE).

<div align="center">
<sub>Built with modern C++20 · <a href="https://anonymmized.github.io/FluxGate/">fluxgate.dev</a></sub>
</div>
