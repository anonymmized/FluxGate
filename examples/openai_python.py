"""
FluxGate + OpenAI Python example.

FluxGate sits between your code and api.openai.com, intercepting HTTPS
traffic, trimming chat history, redacting PII, and caching responses.

Setup (one-time):
    1. Generate a local CA:
           ./fluxgate --generate-ca ./fluxgate-ca
    2. Trust the CA (macOS):
           sudo security add-trusted-cert -d -r trustRoot \
               -k /Library/Keychains/System.keychain fluxgate-ca.cert.pem
       Trust the CA (Ubuntu/Debian):
           sudo cp fluxgate-ca.cert.pem /usr/local/share/ca-certificates/fluxgate-ca.crt
           sudo update-ca-certificates
    3. Start FluxGate:
           ./fluxgate --mitm \
               --mitm-ca-key ./fluxgate-ca.key.pem \
               --mitm-ca-cert ./fluxgate-ca.cert.pem \
               --max-history 20 \
               --cache-ttl 300

Usage:
    pip install openai httpx
    python openai_python.py
"""

import httpx
import openai

FLUXGATE_PROXY = "http://127.0.0.1:8080"

client = openai.OpenAI(
    http_client=httpx.Client(
        proxies=FLUXGATE_PROXY,
        # If the CA isn't trusted system-wide, point to the cert file:
        # verify="./fluxgate-ca.cert.pem",
    )
)

response = client.chat.completions.create(
    model="gpt-4o-mini",
    messages=[
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user",   "content": "Summarize the benefits of caching AI responses."},
    ],
    max_tokens=200,
)

print(response.choices[0].message.content)
print("\n--- FluxGate stats ---")

import urllib.request, json
try:
    with urllib.request.urlopen("http://127.0.0.1:9090/stats") as r:
        stats = json.load(r)
    print(f"Cache hit rate:    {stats['cache_hit_rate']:.1f}%")
    print(f"Tokens saved:      {stats['estimated_tokens_saved']:,}")
    print(f"Cost saved (est.): ${stats['estimated_cost_saved_usd']:.4f}")
except Exception as e:
    print(f"(could not reach admin: {e})")
