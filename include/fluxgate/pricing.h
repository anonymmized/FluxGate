#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fluxgate {

// Best-effort LLM pricing so cost-saved figures are provider/model-aware rather
// than a flat rate. Prices are USD per 1M input tokens and are intentionally
// conservative; they only need to be in the right ballpark for "savings" math.
double input_price_per_million(std::string_view host, std::string_view model);

// Human-readable provider name for a host ("api.openai.com" -> "OpenAI").
std::string provider_label(std::string_view host);

// Extract the "model" field from a JSON request body, if present. Returns "" if
// not found. Cheap substring scan — avoids a full JSON parse on the hot path.
std::string detect_model(const std::string& body);

// Rough token estimate from a character count (≈4 chars/token).
inline std::uint64_t estimate_tokens(std::size_t chars) {
    return static_cast<std::uint64_t>(chars) / 4;
}

} // namespace fluxgate
