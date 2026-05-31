#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fluxgate {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequestHead {
    std::string method;
    std::string target;
    std::string version;
    std::vector<HttpHeader> headers;
};

std::optional<HttpRequestHead> parse_http_request_head(std::string_view data);
std::optional<std::string_view> header_value(const HttpRequestHead& request, std::string_view name);

} // namespace fluxgate
