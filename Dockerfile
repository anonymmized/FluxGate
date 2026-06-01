# syntax=docker/dockerfile:1
# ──────────────────────────────────────────────
# Stage 1: build
# ──────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git libssl-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    && cmake --build /build --parallel "$(nproc)"

# ──────────────────────────────────────────────
# Stage 2: minimal runtime image
# ──────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/FluxGate /usr/local/bin/fluxgate

# Proxy port | Admin/metrics port
EXPOSE 8080 9090

# Expects CA key/cert to be mounted at /certs/ or provided via env.
# Example: docker run -v ./certs:/certs fluxgate \
#   --mitm --mitm-ca-key /certs/ca.key.pem --mitm-ca-cert /certs/ca.cert.pem

ENTRYPOINT ["/usr/local/bin/fluxgate"]
CMD ["--help"]
