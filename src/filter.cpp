#include "fluxgate/filter.h"

#include <simdjson.h>

#include <sstream>

namespace fluxgate {

void FilterPipeline::add(std::unique_ptr<TrafficFilter> filter) {
    if (filter) filters_.push_back(std::move(filter));
}

FilterResult FilterPipeline::apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) const {
    FilterResult aggregate;
    for (const auto& filter : filters_) {
        auto result = filter->apply(context, request, body);
        aggregate.modified = aggregate.modified || result.modified;
        aggregate.estimated_tokens_removed += result.estimated_tokens_removed;
        aggregate.redactions += result.redactions;
        if (result.rejected) {
            result.modified = aggregate.modified;
            return result;
        }
    }
    return aggregate;
}

std::size_t FilterPipeline::size() const noexcept {
    return filters_.size();
}

// ── PII redaction ───────────────────────────────────────────────────────────

PiiRedactionFilter::PiiRedactionFilter(std::shared_ptr<RuntimeControls> controls,
                                       const std::vector<CustomRedactionRule>& custom)
    : controls_(std::move(controls)) {
    // Order matters: more specific patterns first so they win over broad ones.
    const auto add = [&](const char* pattern, const char* replacement) {
        rules_.push_back({std::regex(pattern, std::regex::optimize), replacement});
    };
    add(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})", "[REDACTED_EMAIL]");
    // Credit-card-like: 13–16 digits, optionally separated by spaces/dashes.
    add(R"(\b(?:\d[ -]?){13,16}\b)", "[REDACTED_CARD]");
    // OpenAI / generic secret keys (sk-..., or long token-looking strings).
    add(R"(\bsk-[A-Za-z0-9_-]{16,}\b)", "[REDACTED_KEY]");
    add(R"(\b(?:AKIA|ASIA)[A-Z0-9]{16}\b)", "[REDACTED_AWS_KEY]");
    // IPv4 addresses.
    add(R"(\b(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)\b)", "[REDACTED_IP]");
    // Phone numbers (kept last among built-ins — broadest digit pattern).
    add(R"(\+?[0-9][0-9 .()/-]{7,}[0-9])", "[REDACTED_PHONE]");

    for (const auto& c : custom) {
        if (c.pattern.empty()) continue;
        try {
            rules_.push_back({std::regex(c.pattern, std::regex::optimize), c.replacement});
        } catch (const std::regex_error&) {
            // Skip invalid user patterns rather than crashing the proxy.
        }
    }
}

std::string_view PiiRedactionFilter::name() const {
    return "pii-redaction";
}

FilterResult PiiRedactionFilter::apply(const FilterContext&, const HttpRequestHead&, std::string& body) {
    if (controls_ && !controls_->pii_redaction.load(std::memory_order_relaxed)) return {};
    if (body.empty()) return {};

    FilterResult result;
    for (const auto& rule : rules_) {
        // Count matches so the dashboard can show how much PII was scrubbed.
        auto begin = std::sregex_iterator(body.begin(), body.end(), rule.regex);
        const auto end = std::sregex_iterator();
        const auto n = static_cast<std::size_t>(std::distance(begin, end));
        if (n == 0) continue;
        result.redactions += n;
        body = std::regex_replace(body, rule.regex, rule.replacement);
    }
    result.modified = result.redactions > 0;
    return result;
}

// ── Chat history trimming ─────────────────────────────────────────────────

ChatHistoryLimitFilter::ChatHistoryLimitFilter(std::shared_ptr<RuntimeControls> controls)
    : controls_(std::move(controls)) {}

std::string_view ChatHistoryLimitFilter::name() const {
    return "chat-history-limit";
}

FilterResult ChatHistoryLimitFilter::apply(const FilterContext&, const HttpRequestHead&, std::string& body) {
    const std::size_t max_messages =
        controls_ ? controls_->max_chat_history.load(std::memory_order_relaxed) : 0;
    if (max_messages == 0 || body.empty()) return {};

    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(body).get(root) != simdjson::SUCCESS) return {};

    simdjson::dom::array messages;
    if (root["messages"].get(messages) != simdjson::SUCCESS) return {};
    if (messages.size() <= max_messages) return {};

    const auto skip = messages.size() - max_messages;

    // Serialize the truncated messages array using simdjson's DOM serialization
    std::ostringstream arr;
    arr << '[';
    bool first = true;
    std::size_t i = 0;
    for (simdjson::dom::element msg : messages) {
        if (i++ < skip) continue;
        if (!first) arr << ',';
        arr << msg;
        first = false;
    }
    arr << ']';
    const std::string new_array = arr.str();

    // Locate the messages array in the original body and replace it.
    // Use a state-machine scan that handles quoted strings correctly,
    // so embedded '[' / ']' inside JSON string values don't confuse the search.
    const auto key_pos = body.find(R"("messages")");
    if (key_pos == std::string::npos) return {};
    const auto arr_open = body.find('[', key_pos);
    if (arr_open == std::string::npos) return {};

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t arr_close = std::string::npos;

    for (std::size_t j = arr_open; j < body.size(); ++j) {
        const char c = body[j];
        if (escaped) { escaped = false; continue; }
        if (in_string) {
            if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '[' || c == '{') ++depth;
        else if (c == ']' || c == '}') {
            if (--depth == 0) { arr_close = j; break; }
        }
    }
    if (arr_close == std::string::npos) return {};

    // Count chars of the removed prefix to estimate tokens saved (4 chars ≈ 1 token).
    const std::size_t removed_chars = (arr_close - arr_open + 1) - new_array.size();
    const std::size_t tokens_removed = removed_chars / 4;

    body.replace(arr_open, arr_close - arr_open + 1, new_array);
    return {.modified = true, .estimated_tokens_removed = tokens_removed};
}

} // namespace fluxgate
