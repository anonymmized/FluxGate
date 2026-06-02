#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace fluxgate {

class ICache {
public:
    virtual ~ICache() = default;
    virtual void put(std::string key, std::string value, std::chrono::seconds ttl) = 0;
    virtual std::optional<std::string> get(std::string_view key) = 0;
    virtual std::size_t size() const = 0;
    virtual void clear() = 0;
};

} // namespace fluxgate
