#include "fluxgate/leaf_certificate_cache.h"

#include <stdexcept>

namespace fluxgate {

LeafCertificateCache::LeafCertificateCache(const CertificateAuthority& authority, std::size_t max_entries, int valid_days)
    : authority_(authority),
      max_entries_(max_entries),
      valid_days_(valid_days) {
    if (max_entries_ == 0) {
        throw std::invalid_argument("leaf certificate cache requires at least one entry");
    }
    if (valid_days_ <= 0) {
        throw std::invalid_argument("leaf certificate validity must be positive");
    }
}

LeafCertificate LeafCertificateCache::get(std::string_view host) {
    if (host.empty()) {
        throw std::invalid_argument("leaf certificate host cannot be empty");
    }

    std::lock_guard lock(mutex_);
    const std::string key(host);
    const auto found = entries_.find(key);
    if (found != entries_.end()) {
        order_.erase(found->second.order_it);
        order_.push_front(found->first);
        found->second.order_it = order_.begin();
        return found->second.certificate;
    }

    order_.push_front(key);
    auto [inserted, _] = entries_.emplace(order_.front(), Entry{
        .certificate = authority_.issue_leaf(host, valid_days_),
        .order_it = order_.begin(),
    });
    evict_overflow_locked();
    return inserted->second.certificate;
}

std::size_t LeafCertificateCache::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

void LeafCertificateCache::evict_overflow_locked() {
    while (entries_.size() > max_entries_) {
        const auto& key = order_.back();
        entries_.erase(key);
        order_.pop_back();
    }
}

} // namespace fluxgate
