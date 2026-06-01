#pragma once

#include "fluxgate/http_message.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fluxgate {

struct FilterContext {
    std::string upstream_host;
    std::string upstream_port;
};

struct FilterResult {
    bool modified = false;
    bool rejected = false;
    std::string reject_reason;
    std::size_t estimated_tokens_removed = 0;  // rough estimate: chars/4
};

class TrafficFilter {
public:
    virtual ~TrafficFilter() = default;
    virtual std::string_view name() const = 0;
    virtual FilterResult apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) = 0;
};

class FilterPipeline {
public:
    void add(std::unique_ptr<TrafficFilter> filter);
    FilterResult apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) const;
    std::size_t size() const noexcept;

private:
    std::vector<std::unique_ptr<TrafficFilter>> filters_;
};

class PiiRedactionFilter final : public TrafficFilter {
public:
    std::string_view name() const override;
    FilterResult apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) override;
};

class ChatHistoryLimitFilter final : public TrafficFilter {
public:
    explicit ChatHistoryLimitFilter(std::size_t max_messages);
    std::string_view name() const override;
    FilterResult apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) override;

private:
    std::size_t max_messages_;
};

} // namespace fluxgate
