#!/usr/bin/env bash
# FluxGate benchmark — measures cache hit latency vs direct API latency.
# Requires: curl, bc. FluxGate must be running with MITM enabled.
#
# Usage:
#   chmod +x benchmark.sh
#   OPENAI_API_KEY=sk-... ./benchmark.sh

set -euo pipefail

PROXY="http://127.0.0.1:8080"
ADMIN="http://127.0.0.1:9090"
API_URL="https://api.openai.com/v1/chat/completions"
KEY="${OPENAI_API_KEY:-}"
RUNS=10

if [[ -z "$KEY" ]]; then
    echo "Set OPENAI_API_KEY to run this benchmark." >&2
    exit 1
fi

PAYLOAD='{"model":"gpt-4o-mini","max_tokens":10,"messages":[{"role":"user","content":"Reply with one word: hello"}]}'

echo "=== FluxGate Benchmark ==="
echo "Sending $RUNS requests through proxy (first is cache miss, rest are hits)…"
echo ""

total_ms=0
for i in $(seq 1 $RUNS); do
    start=$(date +%s%3N)
    curl -s -x "$PROXY" -X POST "$API_URL" \
        -H "Authorization: Bearer $KEY" \
        -H "Content-Type: application/json" \
        -d "$PAYLOAD" > /dev/null
    end=$(date +%s%3N)
    elapsed=$((end - start))
    total_ms=$((total_ms + elapsed))
    echo "  Request $i: ${elapsed}ms"
done

avg_ms=$((total_ms / RUNS))
echo ""
echo "Average latency (via FluxGate): ${avg_ms}ms"
echo ""

# Fetch stats from admin
if stats=$(curl -sf "$ADMIN/stats"); then
    hit_rate=$(echo "$stats" | python3 -c "import sys,json; d=json.load(sys.stdin); print(f\"{d['cache_hit_rate']:.1f}\")")
    tokens_saved=$(echo "$stats" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['estimated_tokens_saved'])")
    cost_saved=$(echo "$stats" | python3 -c "import sys,json; d=json.load(sys.stdin); print(f\"\${d['estimated_cost_saved_usd']:.4f}\")")
    echo "=== FluxGate Stats ==="
    echo "  Cache hit rate:    ${hit_rate}%"
    echo "  Tokens saved:      ${tokens_saved}"
    echo "  Cost saved (est.): ${cost_saved}"
fi
