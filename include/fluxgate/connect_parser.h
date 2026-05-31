#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fluxgate {

struct ConnectTarget {
    std::string host;
    std::string port = "443";
};

std::optional<ConnectTarget> parse_authority(std::string_view authority);
std::optional<ConnectTarget> parse_connect_request(std::string_view request);

} // namespace fluxgate
