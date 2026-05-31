#include "fluxgate/http_message.h"

#include <algorithm>
#include <cctype>

namespace fluxgate {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool iequals(std::string_view lhs, std::string_view rhs) {
    return lhs.size() == rhs.size()
        && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

} // namespace

std::optional<HttpRequestHead> parse_http_request_head(std::string_view data) {
    const auto head_end = data.find("\r\n\r\n");
    if (head_end == std::string_view::npos) {
        return std::nullopt;
    }

    const auto request_line_end = data.find("\r\n");
    if (request_line_end == std::string_view::npos || request_line_end > head_end) {
        return std::nullopt;
    }

    const auto request_line = data.substr(0, request_line_end);
    const auto method_end = request_line.find(' ');
    if (method_end == std::string_view::npos) {
        return std::nullopt;
    }
    const auto target_start = method_end + 1;
    const auto target_end = request_line.find(' ', target_start);
    if (target_end == std::string_view::npos) {
        return std::nullopt;
    }

    HttpRequestHead request;
    request.method = std::string(request_line.substr(0, method_end));
    request.target = std::string(request_line.substr(target_start, target_end - target_start));
    request.version = std::string(request_line.substr(target_end + 1));

    std::size_t cursor = request_line_end + 2;
    while (cursor < head_end) {
        const auto line_end = data.find("\r\n", cursor);
        if (line_end == std::string_view::npos || line_end > head_end) {
            return std::nullopt;
        }
        const auto line = data.substr(cursor, line_end - cursor);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        request.headers.push_back({
            std::string(trim(line.substr(0, colon))),
            std::string(trim(line.substr(colon + 1))),
        });
        cursor = line_end + 2;
    }
    return request;
}

std::optional<std::string_view> header_value(const HttpRequestHead& request, std::string_view name) {
    for (const auto& header : request.headers) {
        if (iequals(header.name, name)) {
            return header.value;
        }
    }
    return std::nullopt;
}

} // namespace fluxgate
