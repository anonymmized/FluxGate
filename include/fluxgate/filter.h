#pragma once

#include "fluxgate/http_message.h"
#include "fluxgate/runtime_controls.h"

#include <memory>
#include <regex>
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
    std::size_t redactions = 0;                // count of PII matches replaced
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

// A user-supplied redaction rule (regex → replacement), loaded from config.
struct CustomRedactionRule {
    std::string pattern;
    std::string replacement;
};

// Redacts personally-identifiable and secret-looking strings: emails, phone
// numbers, credit cards, IPv4 addresses, and common API-key/token shapes,
// plus any user-supplied custom rules. Honours RuntimeControls::pii_redaction
// so it can be toggled live from the dashboard.
class PiiRedactionFilter final : public TrafficFilter {
public:
    explicit PiiRedactionFilter(std::shared_ptr<RuntimeControls> controls,
                                const std::vector<CustomRedactionRule>& custom = {});
    std::string_view name() const override;
    FilterResult apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) override;

private:
    struct Rule {
        std::regex regex;
        std::string replacement;
    };
    std::shared_ptr<RuntimeControls> controls_;
    std::vector<Rule> rules_;
};

// Trims the chat `messages` array to the last N entries. Reads the live limit
// from RuntimeControls so it can be changed without a restart.
class ChatHistoryLimitFilter final : public TrafficFilter {
public:
    explicit ChatHistoryLimitFilter(std::shared_ptr<RuntimeControls> controls);
    std::string_view name() const override;
    FilterResult apply(const FilterContext& context, const HttpRequestHead& request, std::string& body) override;

private:
    std::shared_ptr<RuntimeControls> controls_;
};

} // namespace fluxgate
