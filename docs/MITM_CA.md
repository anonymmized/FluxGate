# FluxGate MITM CA

FluxGate includes the certificate-generation foundation needed for TLS interception, but full MITM request processing is not wired into the proxy yet.

## Generate A Local Root CA

```sh
./build/FluxGate --generate-ca ./fluxgate-local-ca --ca-common-name "FluxGate Local Root CA"
```

This writes:

- `fluxgate-local-ca.key.pem`
- `fluxgate-local-ca.cert.pem`

The private key file is sensitive. Store it with strict local permissions and never commit it.

## Current Implementation

- Creates a self-signed CA certificate with `basicConstraints=CA:TRUE`.
- Loads CA key/certificate from PEM.
- Issues per-host RSA leaf certificates.
- Adds DNS or IP SAN values to leaf certificates.
- Produces a leaf + CA chain PEM for `asio::ssl` server contexts.
- Caches generated leaf certificates by host with bounded LRU eviction.
- Creates hardened server-side TLS contexts for generated leaf certificates.
- Provides an opt-in `--mitm` mode that completes CONNECT, performs server-side TLS with the client, parses the decrypted HTTP request head, and returns a temporary `501` response.

## Run The MITM Scaffold

```sh
./build/FluxGate \
  --listen 127.0.0.1 \
  --port 8080 \
  --mitm \
  --mitm-ca-key ./fluxgate-local-ca.key.pem \
  --mitm-ca-cert ./fluxgate-local-ca.cert.pem
```

## Remaining MITM Work

- Open a separate upstream TLS client connection to the provider.
- Parse decrypted HTTP/1.1 requests and responses.
- Apply body limits, JSON parsing, filters, and cache policy.
- Replace the temporary `501` response with full upstream request/response forwarding.
- Document trust-store installation and removal for each supported platform.
