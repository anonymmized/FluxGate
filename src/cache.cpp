#include "fluxgate/cache.h"

#include <simdjson.h>

#include <algorithm>
#include <functional>
#include <sstream>
#include <vector>

namespace fluxgate {

MemoryCache::MemoryCache(std::size_t max_entries)
    : max_entries_(max_entries) {}

void MemoryCache::put(std::string key, std::string value, std::chrono::seconds ttl) {
    if (max_entries_ == 0 || ttl.count() <= 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    evict_expired_locked(now);

    const auto found = entries_.find(key);
    if (found != entries_.end()) {
        order_.erase(found->second.order_it);
        entries_.erase(found);
    }

    order_.push_front(key);
    entries_.emplace(order_.front(), StoredEntry{
        .entry = {.value = std::move(value), .expires_at = now + ttl},
        .order_it = order_.begin(),
    });
    evict_overflow_locked();
}

std::optional<std::string> MemoryCache::get(std::string_view key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    evict_expired_locked(now);

    const auto found = entries_.find(std::string(key));
    if (found == entries_.end()) {
        return std::nullopt;
    }

    order_.erase(found->second.order_it);
    order_.push_front(found->first);
    found->second.order_it = order_.begin();
    return found->second.entry.value;
}

std::size_t MemoryCache::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

void MemoryCache::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    order_.clear();
}

void MemoryCache::evict_expired_locked(std::chrono::steady_clock::time_point now) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.entry.expires_at <= now) {
            order_.erase(it->second.order_it);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void MemoryCache::evict_overflow_locked() {
    while (entries_.size() > max_entries_) {
        const auto& key = order_.back();
        entries_.erase(key);
        order_.pop_back();
    }
}

std::string cache_key(std::string_view method, std::string_view target, std::string_view body) {
    const std::hash<std::string_view> hash;
    return std::to_string(hash(method)) + ':' + std::to_string(hash(target)) + ':' + std::to_string(hash(body));
}

namespace {

// Recursively serialize a simdjson DOM element into a canonical string:
// object keys sorted, no insignificant whitespace. Strings are emitted via
// simdjson's own raw_json_token to preserve exact escaping.
void canonicalize(simdjson::dom::element el, std::string& out) {
    switch (el.type()) {
        case simdjson::dom::element_type::OBJECT: {
            simdjson::dom::object obj = el.get_object();
            std::vector<std::pair<std::string_view, simdjson::dom::element>> kv;
            for (auto field : obj) kv.emplace_back(field.key, field.value);
            std::sort(kv.begin(), kv.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            out += '{';
            bool first = true;
            for (auto& [k, v] : kv) {
                if (!first) out += ',';
                first = false;
                out += '"';
                out.append(k.data(), k.size());
                out += "\":";
                canonicalize(v, out);
            }
            out += '}';
            break;
        }
        case simdjson::dom::element_type::ARRAY: {
            out += '[';
            bool first = true;
            for (auto child : el.get_array()) {
                if (!first) out += ',';
                first = false;
                canonicalize(child, out);
            }
            out += ']';
            break;
        }
        default: {
            // Scalars (string/number/bool/null): emit minified JSON text via
            // simdjson's DOM stream operator.
            std::ostringstream tmp;
            tmp << el;
            out += tmp.str();
            break;
        }
    }
}

} // namespace

std::string normalized_cache_key(std::string_view method, std::string_view target,
                                 std::string_view body) {
    // Pad the input — simdjson requires SIMDJSON_PADDING trailing bytes.
    simdjson::dom::parser parser;
    simdjson::padded_string padded(body);
    simdjson::dom::element root;
    if (parser.parse(padded).get(root) == simdjson::SUCCESS) {
        std::string canon;
        canon.reserve(body.size());
        try {
            canonicalize(root, canon);
            return cache_key(method, target, canon);
        } catch (...) {
            // fall through to raw-body key on any serialization hiccup
        }
    }
    return cache_key(method, target, body);
}

} // namespace fluxgate
