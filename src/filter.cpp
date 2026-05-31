#include "fluxgate/filter.h"

#include <regex>

namespace fluxgate {

void FilterPipeline::add(std::unique_ptr<TrafficFilter> filter) {
    if (filter) {
        filters_.push_back(std::move(filter));
    }
}

FilterResult FilterPipeline::apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) const {
    FilterResult aggregate;
    for (const auto& filter : filters_) {
        auto result = filter->apply(context, request, body);
        aggregate.modified = aggregate.modified || result.modified;
        if (result.rejected) {
            return result;
        }
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
    body = std::regex_replace(body, std::regex(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})"), "[REDACTED_EMAIL]");
    body = std::regex_replace(body, std::regex(R"(\+?[0-9][0-9 .()/-]{7,}[0-9])"), "[REDACTED_PHONE]");
    return {.modified = body != original};
}

ChatHistoryLimitFilter::ChatHistoryLimitFilter(std::size_t max_messages)
    : max_messages_(max_messages) {}

std::string_view ChatHistoryLimitFilter::name() const {
    return "chat-history-limit";
}

FilterResult ChatHistoryLimitFilter::apply(const FilterContext&, const HttpRequestHead&, std::string& body) {
    if (max_messages_ == 0) {
        return {};
    }

    constexpr std::string_view marker = R"("role")";
    std::vector<std::size_t> positions;
    std::size_t cursor = 0;
    while ((cursor = body.find(marker, cursor)) != std::string::npos) {
        positions.push_back(cursor);
        cursor += marker.size();
    }

    if (positions.size() <= max_messages_) {
        return {};
    }

    const auto keep_from = positions[positions.size() - max_messages_];
    const auto messages_key = body.find(R"("messages")");
    if (messages_key == std::string::npos || messages_key >= keep_from) {
        return {};
    }

    const auto array_start = body.find('[', messages_key);
    if (array_start == std::string::npos || array_start >= keep_from) {
        return {};
    }

    const auto first_kept_object = body.rfind('{', keep_from);
    if (first_kept_object == std::string::npos || first_kept_object <= array_start) {
        return {};
    }

    body.erase(array_start + 1, first_kept_object - array_start - 1);
    return {.modified = true};
}

} // namespace fluxgate
