#include "fluxgate/pricing.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace fluxgate {
namespace {

struct ModelPrice {
    std::string_view needle;   // substring matched against the model name
    double per_million;        // USD / 1M input tokens
};

// Ordered most-specific first; first substring match wins.
constexpr std::array<ModelPrice, 18> kModelPrices{{
    {"gpt-4o-mini",     0.15},
    {"gpt-4o",          2.50},
    {"gpt-4.1-mini",    0.40},
    {"gpt-4.1-nano",    0.10},
    {"gpt-4.1",         2.00},
    {"gpt-4-turbo",     10.00},
    {"gpt-4",           30.00},
    {"gpt-3.5",         0.50},
    {"o1-mini",         3.00},
    {"o1",              15.00},
    {"claude-3-5-haiku",0.80},
    {"claude-3-haiku",  0.25},
    {"claude-3-5-sonnet",3.00},
    {"claude-3-sonnet", 3.00},
    {"claude-3-opus",   15.00},
    {"claude",          3.00},
    {"gemini-1.5-pro",  1.25},
    {"gemini",          0.075},
}};

// Per-host fallback when the model is unknown.
struct HostPrice {
    std::string_view needle;
    double per_million;
};
constexpr std::array<HostPrice, 5> kHostPrices{{
    {"openai.com",    2.50},
    {"anthropic.com", 3.00},
    {"googleapis.com",1.25},
    {"mistral.ai",    2.00},
    {"groq.com",      0.59},
}};

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

} // namespace

double input_price_per_million(std::string_view host, std::string_view model) {
    if (!model.empty()) {
        for (const auto& mp : kModelPrices)
            if (contains(model, mp.needle)) return mp.per_million;
    }
    for (const auto& hp : kHostPrices)
        if (contains(host, hp.needle)) return hp.per_million;
    return 5.0;  // conservative default
}

std::string provider_label(std::string_view host) {
    if (contains(host, "openai.com"))     return "OpenAI";
    if (contains(host, "anthropic.com"))  return "Anthropic";
    if (contains(host, "googleapis.com")) return "Google Gemini";
    if (contains(host, "mistral.ai"))     return "Mistral";
    if (contains(host, "groq.com"))       return "Groq";
    if (contains(host, "deepseek"))       return "DeepSeek";
    if (contains(host, "cohere"))         return "Cohere";
    return std::string(host);
}

std::string detect_model(const std::string& body) {
    // Look for "model" : "<value>" tolerating whitespace. Cheap manual scan.
    const auto key = body.find("\"model\"");
    if (key == std::string::npos) return {};
    auto i = key + 7;
    while (i < body.size() && (body[i] == ' ' || body[i] == ':' || body[i] == '\t')) ++i;
    if (i >= body.size() || body[i] != '"') return {};
    ++i;
    const auto start = i;
    while (i < body.size() && body[i] != '"') {
        if (body[i] == '\\') ++i;  // skip escaped char
        ++i;
    }
    if (i > body.size()) return {};
    return body.substr(start, i - start);
}

} // namespace fluxgate
