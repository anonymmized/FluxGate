#include "fluxgate/connect_parser.h"

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

} // namespace

std::optional<ConnectTarget> parse_authority(std::string_view authority) {
    authority = trim(authority);
    if (authority.empty()) {
        return std::nullopt;
    }

    ConnectTarget target;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        target.host = std::string(authority.substr(1, close - 1));
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                return std::nullopt;
            }
            target.port = std::string(authority.substr(close + 2));
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon == std::string_view::npos) {
            target.host = std::string(authority);
        } else {
            target.host = std::string(authority.substr(0, colon));
            target.port = std::string(authority.substr(colon + 1));
        }
    }

    if (target.host.empty() || target.port.empty()) {
        return std::nullopt;
    }
    return target;
}

std::optional<ConnectTarget> parse_connect_request(std::string_view request) {
    const auto line_end = request.find("\r\n");
    if (line_end == std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view request_line = request.substr(0, line_end);
    const auto method_end = request_line.find(' ');
    if (method_end == std::string_view::npos) {
        return std::nullopt;
    }

    if (request_line.substr(0, method_end) != "CONNECT") {
        return std::nullopt;
    }

    const auto authority_start = method_end + 1;
    const auto authority_end = request_line.find(' ', authority_start);
    if (authority_end == std::string_view::npos) {
        return std::nullopt;
    }

    return parse_authority(request_line.substr(authority_start, authority_end - authority_start));
}

} // namespace fluxgate
