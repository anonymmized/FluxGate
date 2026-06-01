<div align="center">

# ⚡ FluxGate

### Cut your LLM API bill by 40–70% — without touching your code.

[![CI](https://github.com/anonymmized/FluxGate/actions/workflows/ci.yml/badge.svg)](https://github.com/anonymmized/FluxGate/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/anonymmized/FluxGate?color=7c3aed)](https://github.com/anonymmized/FluxGate/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)

A high-performance C++20 proxy that sits between your app and any LLM API
(OpenAI · Anthropic · Gemini · Mistral · Groq). It **caches** identical
requests, **trims** chat history, and **redacts** PII — all locally, with
sub-millisecond overhead.

[**Quick start**](#-quick-start) ·
[**How it works**](#-how-it-works) ·
[**Dashboard**](#-dashboard) ·
[**Config**](#-configuration) ·
[**Website**](https://anonymmized.github.io/FluxGate/)

</div>

---

## ⏱️ TL;DR — start saving in 3 steps

```sh
# 1. Install
curl -fsSL https://raw.githubusercontent.com/anonymmized/FluxGate/main/install.sh | bash

# 2. Run (a wizard sets everything up — just press Enter through it)
fluxgate

# 3. Tell your app to use the proxy
export HTTPS_PROXY=http://127.0.0.1:8080
```

Now run your app as usual. Watch the savings live at **http://127.0.0.1:9090**.

> **No API key? No problem.** Test it instantly with no account:
> ```sh
> for i in $(seq 1 10); do curl -sk -x http://127.0.0.1:8080 https://httpbin.org/get -o /dev/null; done
> ```
> Open the dashboard — you'll see the cache hit rate climb to ~90%.
> *(Pick option **7 — Everything** in the wizard so non-LLM hosts like httpbin are intercepted too.)*

---

## 🚀 Quick start

FluxGate is **interactive**. Just run it with no arguments and a setup wizard
walks you through everything — generating a CA, choosing providers, and writing
your config — in about 60 seconds.

### One-line install

```sh
curl -fsSL https://raw.githubusercontent.com/anonymmized/FluxGate/main/install.sh | bash
```

### Or build from source

```sh
git clone https://github.com/anonymmized/FluxGate.git
cd FluxGate
cmake -S . -B build && cmake --build build --parallel
./build/FluxGate          # ← launches the setup wizard
```

That's it. On first run you'll see:

```
  ⚡ FluxGate Setup Wizard
  ─────────────────────────────────────
  Cut your LLM API bill by 40–70%.

  Proxy
  ─────
  → Listen address [127.0.0.1]:
  → Listen port [8080]:

  TLS Interception (MITM)
  ───────────────────────
  → Enable MITM mode [Y/n]:
  ...
```

The wizard generates a local CA and prints the one command needed to trust it.
After setup, FluxGate launches a **live terminal dashboard**:

```
╔════════════════════════════════════════════════════════════╗
║ FluxGate  ·  127.0.0.1:8080  ·  MITM ON                     ║
╠════════════════════════════════════════════════════════════╣
║                                                              ║
║ SESSIONS                                                     ║
║  Active     2    Accepted     147    Failed     0            ║
║                                                              ║
║ CACHE                                                        ║
║  Hit rate  ██████████████████████  93.2%                    ║
║  Hits        137      Misses        10                       ║
║                                                              ║
║ SAVINGS / TRAFFIC                                            ║
║  Tokens     48.2K      In      12.4 KB                       ║
║  Cost  $0.2410      Out      1.8 MB                          ║
║                                                              ║
╠════════════════════════════════════════════════════════════╣
║  Dashboard: http://127.0.0.1:9090/   [q] quit                ║
╚════════════════════════════════════════════════════════════╝
```

### Point your app at the proxy

```sh
export HTTPS_PROXY=http://127.0.0.1:8080
```

Or in code:

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
                    ┌─────────┴─────────┐
                    │  Provider routing  │  intercept only chosen hosts
                    │  PII redaction     │  strip emails & phone numbers
                    │  History trimming  │  keep only the last N messages
                    │  Response cache    │  identical query → 0 API calls
                    └────────────────────┘
```

FluxGate terminates TLS from your client using a **local CA you generate
yourself**, inspects and optionally rewrites the JSON payload, then opens a
fresh verified TLS connection to the real provider. Nothing ever leaves your
machine except the (optimised) request to the upstream API.

---

## 📊 Dashboard

Two ways to watch FluxGate work:

| Where | What |
|-------|------|
| **Terminal** | Live TUI with cache bar, savings, and traffic — refreshes every second |
| **Browser** | Open `http://127.0.0.1:9090/` for a full dashboard with **live sparkline charts** |

Admin endpoints:

| Endpoint | Description |
|----------|-------------|
| `GET /` | Web dashboard with hit-rate & sessions charts |
| `GET /stats` | JSON snapshot (hit rate, tokens & cost saved) |
| `GET /history` | Last 60s of metrics for charting |
| `GET /metrics` | Prometheus text format |
| `GET /healthz` | `ok` — for load balancers |

---

## ✨ Features

- **🔁 Smart cache** — identical queries return instantly. LRU + TTL in-memory, or Redis for multi-instance setups.
- **✂️ History trimming** — keep only the last *N* chat messages; cuts input tokens on long conversations.
- **🔒 PII redaction** — strip emails and phone numbers before they leave your network.
- **🎯 Provider routing** — intercept only the hosts you choose; everything else tunnels transparently.
- **📈 Token & cost tracking** — see estimated tokens and dollars saved, live.
- **🖥️ Interactive setup** — no flags to memorise; a wizard configures everything.
- **🐳 Deploy anywhere** — single static binary, Docker image, systemd unit.
- **🔍 Structured logs** — NDJSON with request IDs and upstream latency; never logs body content.

---

## ⚙️ Configuration

The wizard writes a `fluxgate.toml` you can edit by hand. Re-run with that file:

```sh
./build/FluxGate --config fluxgate.toml
```

```toml
[proxy]
listen  = "127.0.0.1"
port    = 8080

[tls]
enabled = true
ca_key  = "./fluxgate-ca.key.pem"
ca_cert = "./fluxgate-ca.cert.pem"

[filters]
pii_redaction    = true
max_chat_history = 20      # 0 = unlimited

[cache]
enabled     = true
backend     = "memory"     # or "redis"
ttl_seconds = 300
# redis_url = "redis://127.0.0.1:6379"

[admin]
enabled = true
port    = 9090
# token = "secret"         # require Bearer auth

[providers]
allowlist = ["api.openai.com", "api.anthropic.com"]  # empty = intercept all
denylist  = []
```

<details>
<summary><b>Advanced: command-line flags</b></summary>

Every config value can be overridden from the CLI (flags win over the file):

```sh
./build/FluxGate \
  --config fluxgate.toml \
  --port 8080 \
  --max-history 20 \
  --cache-ttl 300 \
  --allow api.openai.com \
  --deny internal.corp \
  --admin-token secret
```

| Flag | Description |
|------|-------------|
| `--config path` | Load settings from a TOML file |
| `--setup` | Force the interactive wizard |
| `--listen host` / `--port N` | Bind address / port |
| `--threads N` | Worker threads (default: CPU count) |
| `--mitm` `--mitm-ca-key` `--mitm-ca-cert` | Enable TLS interception |
| `--max-history N` / `--no-pii` | Filter controls |
| `--cache-ttl N` / `--no-cache` / `--cache-backend redis` | Cache controls |
| `--allow host` / `--deny host` | Provider allowlist / denylist |
| `--admin host:port` / `--admin-token T` | Admin endpoint |
| `--generate-ca prefix` | Generate a CA and exit |
| `--dump-config` | Print current config as TOML |

Run `./build/FluxGate --help` for the full list.
</details>

---

## 🐳 Docker

```sh
docker compose up -d
# or
docker run -d -v ~/.fluxgate:/certs:ro -p 8080:8080 -p 9090:9090 \
  ghcr.io/anonymmized/fluxgate:latest \
  --mitm --mitm-ca-key /certs/ca.key.pem --mitm-ca-cert /certs/ca.cert.pem
```

---

## 🔧 Build & test

```sh
cmake -S . -B build && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Requires C++20, CMake ≥ 3.15, OpenSSL. `simdjson` and `toml++` are fetched
automatically. For the Redis backend: `cmake -S . -B build -DFLUXGATE_REDIS=ON`.

Tests include a full **integration suite** that spins up a real proxy and a
local HTTPS upstream, then verifies the MITM filter and cache end-to-end.

---

## 🔒 Security

FluxGate runs **locally** — your traffic never leaves your infrastructure
through it. It never logs API keys or body content. See
[`docs/SECURITY.md`](docs/SECURITY.md) for the full trust model.

---

## 🗺️ Roadmap

- [x] Async TCP/SSL CONNECT gateway
- [x] Full TLS MITM with body capture & JSON parsing
- [x] Filter pipeline (PII redaction, history trimming)
- [x] In-memory & Redis response cache
- [x] Provider allowlist/denylist
- [x] Interactive setup wizard + live terminal TUI
- [x] Web dashboard with live charts
- [x] Docker, systemd, signed releases, CI
- [ ] HTTP/2 upstream support
- [ ] Plugin ABI for custom filters
- [ ] Per-client rate limiting & quotas

---

## 📄 License

MIT — see [LICENSE](LICENSE).

<div align="center">
<sub>Built with modern C++20 · <a href="https://anonymmized.github.io/FluxGate/">fluxgate.dev</a></sub>
</div>
