"""
FluxGate + Anthropic Python example.

See openai_python.py for CA trust setup and FluxGate startup instructions.

    pip install anthropic httpx
    python anthropic_python.py
"""

import httpx
import anthropic

FLUXGATE_PROXY = "http://127.0.0.1:8080"

client = anthropic.Anthropic(
    http_client=httpx.Client(proxies=FLUXGATE_PROXY),
)

message = client.messages.create(
    model="claude-haiku-4-5",
    max_tokens=200,
    messages=[
        {"role": "user", "content": "What is the capital of France?"},
    ],
)

print(message.content[0].text)
