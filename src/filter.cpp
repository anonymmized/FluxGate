#include "fluxgate/filter.h"

#include <simdjson.h>

#include <regex>
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
        if (result.rejected) return result;
    }
    return aggregate;
}

std::size_t FilterPipeline::size() const noexcept {
    return filters_.size();
}

std::string_view PiiRedactionFilter::name() const {
    return "pii-redaction";
}

FilterResult PiiRedactionFilter::apply(const FilterContext&, const HttpRequestHead&, std::string& body) {
    const auto original = body;
    body = std::regex_replace(body,
        std::regex(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})"),
        "[REDACTED_EMAIL]");
    body = std::regex_replace(body,
        std::regex(R"(\+?[0-9][0-9 .()/-]{7,}[0-9])"),
        "[REDACTED_PHONE]");
    return {.modified = body != original};
}

ChatHistoryLimitFilter::ChatHistoryLimitFilter(std::size_t max_messages)
    : max_messages_(max_messages) {}

std::string_view ChatHistoryLimitFilter::name() const {
    return "chat-history-limit";
}

FilterResult ChatHistoryLimitFilter::apply(const FilterContext&, const HttpRequestHead&, std::string& body) {
    if (max_messages_ == 0 || body.empty()) return {};

    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(body).get(root) != simdjson::SUCCESS) return {};

    simdjson::dom::array messages;
    if (root["messages"].get(messages) != simdjson::SUCCESS) return {};
    if (messages.size() <= max_messages_) return {};

    const auto skip = messages.size() - max_messages_;

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
