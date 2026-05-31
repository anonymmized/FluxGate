#pragma once

#include "fluxgate/cert_authority.h"

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fluxgate {

class LeafCertificateCache {
public:
    LeafCertificateCache(const CertificateAuthority& authority, std::size_t max_entries, int valid_days);

    LeafCertificate get(std::string_view host);
    std::size_t size() const;

private:
    using Order = std::list<std::string>;

    struct Entry {
        LeafCertificate certificate;
        Order::iterator order_it;
    };

    void evict_overflow_locked();

    const CertificateAuthority& authority_;
    std::size_t max_entries_;
    int valid_days_;
    mutable std::mutex mutex_;
    Order order_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace fluxgate
