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

# wget is used by the docker-compose healthcheck (GET /healthz).
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 ca-certificates wget \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/FluxGate /usr/local/bin/fluxgate

# Proxy port | Admin/metrics port
EXPOSE 8080 9090

# Mount a CA at /certs (generate once with: --generate-ca /certs/ca).
# IMPORTANT: inside a container the listeners must bind 0.0.0.0, otherwise
# Docker's published ports can't reach them and `curl -x` fails with a reset.
# Example:
#   docker run -v ./certs:/certs -p 8080:8080 -p 9090:9090 fluxgate \
#     --listen 0.0.0.0 --admin 0.0.0.0:9090 \
#     --mitm --mitm-ca-key /certs/ca.key.pem --mitm-ca-cert /certs/ca.cert.pem

ENTRYPOINT ["/usr/local/bin/fluxgate"]
CMD ["--help"]
