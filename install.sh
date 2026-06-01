#!/usr/bin/env bash
# FluxGate installer — installs binary, generates CA, trusts it, creates config.
# Usage:  curl -fsSL https://raw.githubusercontent.com/anonymmized/FluxGate/main/install.sh | bash
set -euo pipefail

REPO="anonymmized/FluxGate"
INSTALL_DIR="${FLUXGATE_DIR:-$HOME/.fluxgate}"
BIN="$INSTALL_DIR/fluxgate"
CA_KEY="$INSTALL_DIR/ca.key.pem"
CA_CERT="$INSTALL_DIR/ca.cert.pem"
CONFIG="$INSTALL_DIR/config.toml"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}[FluxGate]${NC} $*"; }
ok()    { echo -e "${GREEN}[✓]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
die()   { echo -e "${RED}[✗]${NC} $*" >&2; exit 1; }

# ── Detect OS + arch ─────────────────────────────────────────────────────────
OS=$(uname -s)
ARCH=$(uname -m)

case "$OS" in
  Darwin) PLATFORM="macos" ;;
  Linux)  PLATFORM="linux" ;;
  *)      die "Unsupported OS: $OS" ;;
esac

case "$ARCH" in
  x86_64|amd64) CPU="amd64" ;;
  arm64|aarch64) CPU="arm64" ;;
  *) die "Unsupported architecture: $ARCH" ;;
esac

ASSET="fluxgate-${PLATFORM}-${CPU}"

# ── Get latest release ────────────────────────────────────────────────────────
info "Fetching latest FluxGate release…"
if command -v curl &>/dev/null; then
  FETCH="curl -fsSL"
elif command -v wget &>/dev/null; then
  FETCH="wget -qO-"
else
  die "curl or wget is required"
fi

LATEST=$(${FETCH} "https://api.github.com/repos/${REPO}/releases/latest" \
  | grep '"tag_name"' | head -1 | sed 's/.*"tag_name": *"\(.*\)".*/\1/')

[[ -z "$LATEST" ]] && die "Could not determine latest release version."
info "Latest version: $LATEST"

# ── Download binary ───────────────────────────────────────────────────────────
mkdir -p "$INSTALL_DIR"
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${LATEST}/${ASSET}"
info "Downloading ${ASSET}…"
${FETCH} "$DOWNLOAD_URL" -o "$BIN" 2>/dev/null || \
  ${FETCH} "$DOWNLOAD_URL" > "$BIN"
chmod +x "$BIN"
ok "Binary installed to $BIN"

# ── Generate CA (if not already present) ─────────────────────────────────────
if [[ ! -f "$CA_KEY" ]]; then
  info "Generating local root CA…"
  "$BIN" --generate-ca "$INSTALL_DIR/ca" \
         --ca-common-name "FluxGate Local Root CA" >/dev/null
  ok "CA generated: $CA_CERT"
else
  ok "CA already exists, skipping generation."
fi

# ── Trust CA in system store ──────────────────────────────────────────────────
info "Trusting CA certificate…"
case "$OS" in
  Darwin)
    sudo security add-trusted-cert \
      -d -r trustRoot \
      -k /Library/Keychains/System.keychain \
      "$CA_CERT" 2>/dev/null \
    && ok "CA trusted (macOS System Keychain)" \
    || warn "Could not auto-trust CA. Run manually:\n  sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain $CA_CERT"
    ;;
  Linux)
    if command -v update-ca-certificates &>/dev/null; then
      sudo cp "$CA_CERT" /usr/local/share/ca-certificates/fluxgate-local-ca.crt
      sudo update-ca-certificates -f >/dev/null 2>&1
      ok "CA trusted (update-ca-certificates)"
    elif command -v trust &>/dev/null; then
      sudo trust anchor --store "$CA_CERT"
      ok "CA trusted (p11-kit trust)"
    else
      warn "Could not auto-trust CA. Add $CA_CERT to your system trust store manually."
    fi
    ;;
esac

# ── Write default config ──────────────────────────────────────────────────────
if [[ ! -f "$CONFIG" ]]; then
  cat > "$CONFIG" <<TOML
[proxy]
listen  = "127.0.0.1"
port    = 8080
threads = 4

[tls]
enabled = true
ca_key  = "$CA_KEY"
ca_cert = "$CA_CERT"

[filters]
pii_redaction    = true
max_chat_history = 20

[cache]
enabled     = true
ttl_seconds = 300

[admin]
enabled = true
listen  = "127.0.0.1"
port    = 9090

[providers]
allowlist = [
  "api.openai.com",
  "api.anthropic.com",
  "generativelanguage.googleapis.com",
  "api.mistral.ai",
  "api.cohere.ai",
]
TOML
  ok "Config written to $CONFIG"
fi

# ── Add to PATH hint ──────────────────────────────────────────────────────────
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
  warn "Add FluxGate to your PATH:"
  echo "    echo 'export PATH=\"\$PATH:$INSTALL_DIR\"' >> ~/.zshrc && source ~/.zshrc"
fi

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}  FluxGate $LATEST installed successfully!${NC}"
echo -e "${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "  Start:"
echo -e "    ${CYAN}$BIN --config $CONFIG${NC}"
echo ""
echo "  Then set HTTP proxy in your app:"
echo -e "    ${CYAN}http://127.0.0.1:8080${NC}"
echo ""
echo "  Dashboard:  http://127.0.0.1:9090/"
echo "  Metrics:    http://127.0.0.1:9090/metrics"
echo ""
