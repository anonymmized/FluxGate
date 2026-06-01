#include "fluxgate/redis_cache.h"

#include <hiredis/hiredis.h>

#include <stdexcept>

namespace fluxgate {
namespace {

struct Reply {
    explicit Reply(void* r) : ptr(static_cast<redisReply*>(r)) {}
    ~Reply() { if (ptr) freeReplyObject(ptr); }
    Reply(const Reply&) = delete;
    Reply& operator=(const Reply&) = delete;
    redisReply* ptr;
    explicit operator bool() const { return ptr && ptr->type != REDIS_REPLY_ERROR && ptr->type != REDIS_REPLY_NIL; }
};

// Parse "redis://host:port/db" → host, port, db
void parse_url(std::string_view url, std::string& host, int& port, int& db) {
    std::string_view rest = url;
    if (rest.starts_with("redis://")) rest.remove_prefix(8);

    const auto slash = rest.find('/');
    std::string_view host_port = (slash != std::string_view::npos) ? rest.substr(0, slash) : rest;
    if (slash != std::string_view::npos && slash + 1 < rest.size()) {
        db = std::stoi(std::string(rest.substr(slash + 1)));
    }

    const auto colon = host_port.rfind(':');
    if (colon != std::string_view::npos) {
        host = std::string(host_port.substr(0, colon));
        port = std::stoi(std::string(host_port.substr(colon + 1)));
    } else {
        host = std::string(host_port);
    }
}

} // namespace

RedisCache::RedisCache(std::string_view url) {
    parse_url(url, host_, port_, db_);
    reconnect_if_needed();
}

RedisCache::~RedisCache() {
    if (ctx_) redisFree(ctx_);
}

void RedisCache::reconnect_if_needed() {
    if (ctx_ && ctx_->err == REDIS_OK) return;
    if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }

    ctx_ = redisConnect(host_.c_str(), port_);
    if (!ctx_ || ctx_->err) return;  // silently fail — cache will be a no-op

    if (db_ != 0) {
        Reply r(redisCommand(ctx_, "SELECT %d", db_));
        if (!r.ptr || r.ptr->type == REDIS_REPLY_ERROR) {
            redisFree(ctx_);
            ctx_ = nullptr;
        }
    }
}

bool RedisCache::connected() const {
    std::lock_guard lock(mutex_);
    return ctx_ && ctx_->err == REDIS_OK;
}

void RedisCache::put(std::string key, std::string value, std::chrono::seconds ttl) {
    if (ttl.count() <= 0) return;
    std::lock_guard lock(mutex_);
    reconnect_if_needed();
    if (!ctx_) return;

    Reply r(redisCommand(ctx_, "SET %b %b EX %lld",
        key.data(), key.size(),
        value.data(), value.size(),
        static_cast<long long>(ttl.count())));
    (void)r;
}

std::optional<std::string> RedisCache::get(std::string_view key) {
    std::lock_guard lock(mutex_);
    reconnect_if_needed();
    if (!ctx_) return std::nullopt;

    Reply r(redisCommand(ctx_, "GET %b", key.data(), key.size()));
    if (!r || !r.ptr || r.ptr->type != REDIS_REPLY_STRING) return std::nullopt;
    return std::string(r.ptr->str, r.ptr->len);
}

std::size_t RedisCache::size() const {
    std::lock_guard lock(mutex_);
    if (!ctx_) return 0;
    Reply r(redisCommand(ctx_, "DBSIZE"));
    if (!r || !r.ptr || r.ptr->type != REDIS_REPLY_INTEGER) return 0;
    return static_cast<std::size_t>(r.ptr->integer);
}

} // namespace fluxgate
