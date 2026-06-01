# Security Model

## What FluxGate is

FluxGate is a **local** HTTPS MITM proxy. It runs inside your network — on your laptop, in a sidecar container, or on a private VM. Traffic never leaves your infrastructure through FluxGate.

## What FluxGate can see

Because FluxGate performs TLS interception, it has access to:

- Request headers (including `Authorization: Bearer <api-key>`)
- Request bodies (the JSON payload sent to the LLM provider)
- Response bodies (the LLM output)

This is intentional — it is the only way to apply filters and a response cache.

## What FluxGate does NOT do

| Action | Status |
|--------|--------|
| Log request/response bodies | ✗ Never |
| Store API keys to disk | ✗ Never |
| Send telemetry to any external service | ✗ Never |
| Persist chat content to any database | ✗ Never |
| Share data with the FluxGate project authors | ✗ Never |

Structured logs contain only: timestamp, session ID, host, HTTP method, path, upstream latency, and status code. No body content, no API keys.

## The CA certificate

FluxGate generates a **local root CA** that is used to sign per-host TLS certificates on-the-fly. This CA:

- Is generated locally and never transmitted anywhere.
- Should only be trusted on the machine(s) running FluxGate.
- Should **not** be distributed to end-user devices unless you explicitly want to proxy their traffic.
- The CA private key lives at `~/.fluxgate/ca.key.pem`. Protect it as you would any private key (0600 permissions, encrypted disk).

## Threat model

FluxGate is designed for the following deployment scenarios:

1. **Developer laptop** — proxy for local development to measure/reduce API costs.
2. **Application sidecar** — proxy deployed alongside your application container in a private VPC.
3. **Corporate gateway** — proxy for an internal team, deployed on an internal VM.

FluxGate is **not** designed to be exposed to the public internet. The admin port (`9090`) should never be publicly accessible.

## Reporting vulnerabilities

Please report security issues by email to `security@fluxgate.dev` rather than opening a public GitHub issue.
