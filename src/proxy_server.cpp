#include "fluxgate/proxy_server.h"

#include "fluxgate/cache.h"
#include "fluxgate/connect_parser.h"
#include "fluxgate/filter.h"
#include "fluxgate/http_message.h"

#include <asio/ssl.hpp>
#include <openssl/ssl.h>

#include <cctype>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fluxgate {
namespace {

using asio::ip::tcp;

std::string buffer_to_string(const asio::streambuf::const_buffers_type& buffers, std::size_t bytes) {
    auto begin = asio::buffers_begin(buffers);
    return std::string(begin, begin + static_cast<std::ptrdiff_t>(bytes));
}

// Rebuilds a wire-format HTTP/1.1 request. Updates Content-Length to match body.
std::string rebuild_http_request(const HttpRequestHead& head, const std::string& host, const std::string& body) {
    std::string out;
    out.reserve(512 + body.size());
    out += head.method;
    out += ' ';
    out += head.target;
    out += " HTTP/1.1\r\n";

    for (const auto& h : head.headers) {
        std::string lower = h.name;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "content-length" || lower == "host" || lower == "connection"
            || lower == "proxy-connection" || lower == "keep-alive") {
            continue;
        }
        out += h.name;
        out += ": ";
        out += h.value;
        out += "\r\n";
    }

    out += "Host: ";
    out += host;
    out += "\r\nConnection: close\r\n";

    if (!body.empty()) {
        out += "Content-Length: ";
        out += std::to_string(body.size());
        out += "\r\n";
    }
    out += "\r\n";
    out += body;
    return out;
}

class Session final : public std::enable_shared_from_this<Session> {
public:
    Session(
        tcp::socket client_socket,
        AppConfig config,
        std::shared_ptr<Metrics> metrics,
        SharedMitmServices mitm_services,
        std::shared_ptr<MemoryCache> cache,
        std::shared_ptr<FilterPipeline> filter_pipeline,
        std::uint64_t id)
        : client_socket_(std::move(client_socket)),
          upstream_socket_(client_socket_.get_executor()),
          resolver_(client_socket_.get_executor()),
          strand_(asio::make_strand(client_socket_.get_executor())),
          config_(std::move(config)),
          metrics_(std::move(metrics)),
          mitm_services_(std::move(mitm_services)),
          cache_(std::move(cache)),
          filter_pipeline_(std::move(filter_pipeline)),
          client_to_upstream_(config_.relay_buffer_bytes),
          upstream_to_client_(config_.relay_buffer_bytes),
          id_(id) {}

    ~Session() {
        if (metrics_) metrics_->on_session_closed();
    }

    void start() { read_connect_header(); }

private:
    // ── Plain tunnel path ────────────────────────────────────────────────────

    void read_connect_header() {
        asio::async_read_until(client_socket_, client_header_, "\r\n\r\n",
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec) return close();
                    if (client_header_.size() > config_.max_header_bytes)
                        return reject("431 Request Header Fields Too Large");

                    const auto header = buffer_to_string(client_header_.data(), bytes);
                    auto target = parse_connect_request(header);
                    if (!target) return reject("400 Bad Request");

                    target_ = std::move(*target);
                    client_header_.consume(bytes);
                    if (mitm_services_ && should_mitm(target_.host))
                        return accept_mitm_tunnel();
                    connect_to_upstream();
                }));
    }

    void connect_to_upstream() {
        resolver_.async_resolve(target_.host, target_.port,
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, tcp::resolver::results_type results) {
                    if (ec) {
                        metrics_->on_upstream_connect_failure();
                        return reject("502 Bad Gateway");
                    }
                    asio::async_connect(upstream_socket_, results,
                        asio::bind_executor(strand_,
                            [this, self](std::error_code connect_ec, const tcp::endpoint&) {
                                if (connect_ec) {
                                    metrics_->on_upstream_connect_failure();
                                    return reject("502 Bad Gateway");
                                }
                                accept_tunnel();
                            }));
                }));
    }

    void accept_tunnel() {
        static constexpr char response[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
        asio::async_write(client_socket_, asio::buffer(response),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t) {
                    if (ec) return close();
                    std::cout << '[' << id_ << "] tunnel "
                              << target_.host << ':' << target_.port << '\n';
                    flush_prefetched_client_bytes();
                    relay_upstream_to_client();
                }));
    }

    void flush_prefetched_client_bytes() {
        if (client_header_.size() == 0) return relay_client_to_upstream();
        asio::async_write(upstream_socket_, client_header_.data(),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec) return close();
                    client_header_.consume(bytes);
                    relay_client_to_upstream();
                }));
    }

    void relay_client_to_upstream() {
        client_socket_.async_read_some(asio::buffer(client_to_upstream_),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec || bytes == 0) return shutdown_socket(upstream_socket_);
                    metrics_->add_client_to_upstream_bytes(bytes);
                    asio::async_write(upstream_socket_, asio::buffer(client_to_upstream_.data(), bytes),
                        asio::bind_executor(strand_,
                            [this, self](std::error_code write_ec, std::size_t) {
                                if (write_ec) return close();
                                relay_client_to_upstream();
                            }));
                }));
    }

    void relay_upstream_to_client() {
        upstream_socket_.async_read_some(asio::buffer(upstream_to_client_),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec || bytes == 0) return shutdown_socket(client_socket_);
                    metrics_->add_upstream_to_client_bytes(bytes);
                    asio::async_write(client_socket_, asio::buffer(upstream_to_client_.data(), bytes),
                        asio::bind_executor(strand_,
                            [this, self](std::error_code write_ec, std::size_t) {
                                if (write_ec) return close();
                                relay_upstream_to_client();
                            }));
                }));
    }

    void reject(std::string_view status) {
        metrics_->on_session_rejected();
        response_ = "HTTP/1.1 ";
        response_ += status;
        response_ += "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        asio::async_write(client_socket_, asio::buffer(response_),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code, std::size_t) { close(); }));
    }

    void shutdown_socket(tcp::socket& socket) {
        std::error_code ignored;
        socket.shutdown(tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
    }

    void close() {
        shutdown_socket(client_socket_);
        shutdown_socket(upstream_socket_);
        resolver_.cancel();
    }

    // Returns true if this host should be intercepted via MITM.
    bool should_mitm(const std::string& host) const {
        for (const auto& h : config_.provider_denylist)
            if (h == host) return false;
        if (!config_.provider_allowlist.empty()) {
            for (const auto& h : config_.provider_allowlist)
                if (h == host) return true;
            return false;
        }
        return true;
    }

    // ── MITM path ────────────────────────────────────────────────────────────

    void accept_mitm_tunnel() {
        static constexpr char response[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
        asio::async_write(client_socket_, asio::buffer(response),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t) {
                    if (ec) return close();
                    start_client_tls_handshake();
                }));
    }

    void start_client_tls_handshake() {
        client_ssl_ctx_ = mitm_services_->tls_factory.create_server_context(target_.host);
        client_ssl_ = std::make_shared<asio::ssl::stream<tcp::socket>>(
            std::move(client_socket_), *client_ssl_ctx_);

        client_ssl_->async_handshake(asio::ssl::stream_base::server,
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec) {
                    if (ec) return;
                    std::cout << '[' << id_ << "] mitm tls established for "
                              << target_.host << ':' << target_.port << '\n';
                    read_mitm_http_head();
                }));
    }

    void read_mitm_http_head() {
        auto buf = std::make_shared<asio::streambuf>();
        asio::async_read_until(*client_ssl_, *buf, "\r\n\r\n",
            asio::bind_executor(strand_,
                [this, self = shared_from_this(), buf](std::error_code ec, std::size_t bytes) mutable {
                    if (ec) return shutdown_mitm_client();
                    if (buf->size() > config_.max_header_bytes)
                        return write_mitm_error("431 Request Header Fields Too Large");

                    const auto header_str = buffer_to_string(buf->data(), bytes);
                    auto req = parse_http_request_head(header_str);
                    if (!req) return write_mitm_error("400 Bad Request");

                    std::cout << '[' << id_ << "] intercepted "
                              << req->method << ' ' << req->target
                              << " for " << target_.host << '\n';

                    buf->consume(bytes);
                    read_mitm_body(std::move(buf), std::move(*req));
                }));
    }

    void read_mitm_body(std::shared_ptr<asio::streambuf> buf, HttpRequestHead request) {
        std::size_t content_length = 0;
        if (auto cl = header_value(request, "content-length")) {
            try {
                content_length = std::stoull(std::string(*cl));
            } catch (...) {
                return write_mitm_error("400 Bad Request");
            }
        }

        if (content_length > config_.max_body_bytes)
            return write_mitm_error("413 Content Too Large");

        const auto already_have = std::min(buf->size(), content_length);
        const auto need_more = content_length - already_have;

        auto body_data = buf->data();
        auto pre = std::make_shared<std::string>(
            asio::buffers_begin(body_data),
            asio::buffers_begin(body_data) + static_cast<std::ptrdiff_t>(already_have));
        buf->consume(already_have);

        if (need_more == 0) {
            return process_mitm_request(std::move(request), std::move(*pre));
        }

        pre->reserve(content_length);
        asio::async_read(*client_ssl_, *buf, asio::transfer_exactly(need_more),
            asio::bind_executor(strand_,
                [this, self = shared_from_this(),
                 request = std::move(request), pre, buf](std::error_code ec, std::size_t) mutable {
                    if (ec) return shutdown_mitm_client();
                    auto tail = buf->data();
                    pre->append(asio::buffers_begin(tail), asio::buffers_end(tail));
                    process_mitm_request(std::move(request), std::move(*pre));
                }));
    }

    void process_mitm_request(HttpRequestHead request, std::string body) {
        FilterContext ctx{target_.host, target_.port};
        if (filter_pipeline_) {
            auto result = filter_pipeline_->apply(ctx, request, body);
            if (result.rejected)
                return write_mitm_error("403 Forbidden");
            if (result.modified)
                metrics_->on_request_filtered();
        }

        if (cache_) {
            const auto key = cache_key(request.method, target_.host + request.target, body);
            if (auto cached = cache_->get(key)) {
                metrics_->on_cache_hit();
                std::cout << '[' << id_ << "] cache hit "
                          << request.method << ' ' << target_.host << request.target << '\n';
                return send_cached_response(std::make_shared<std::string>(std::move(*cached)));
            }
            metrics_->on_cache_miss();
            pending_cache_key_ = key;
        }

        connect_mitm_upstream(std::move(request), std::move(body));
    }

    void send_cached_response(std::shared_ptr<std::string> response) {
        asio::async_write(*client_ssl_, asio::buffer(*response),
            asio::bind_executor(strand_,
                [this, self = shared_from_this(), response](std::error_code, std::size_t bytes) {
                    metrics_->add_upstream_to_client_bytes(bytes);
                    shutdown_mitm_client();
                }));
    }

    void connect_mitm_upstream(HttpRequestHead request, std::string body) {
        upstream_ssl_ctx_ = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
        upstream_ssl_ctx_->set_default_verify_paths();
        upstream_ssl_ctx_->set_verify_mode(asio::ssl::verify_peer);

        upstream_ssl_ = std::make_shared<asio::ssl::stream<tcp::socket>>(
            tcp::socket(client_ssl_->get_executor()), *upstream_ssl_ctx_);

        SSL_set_tlsext_host_name(upstream_ssl_->native_handle(), target_.host.c_str());
        upstream_ssl_->set_verify_callback(asio::ssl::host_name_verification(target_.host));

        resolver_.async_resolve(target_.host, target_.port,
            asio::bind_executor(strand_,
                [this, self = shared_from_this(),
                 request = std::move(request),
                 body = std::move(body)](std::error_code ec, tcp::resolver::results_type results) mutable {
                    if (ec) {
                        metrics_->on_upstream_connect_failure();
                        return write_mitm_error("502 Bad Gateway");
                    }
                    asio::async_connect(upstream_ssl_->lowest_layer(), results,
                        asio::bind_executor(strand_,
                            [this, self,
                             request = std::move(request),
                             body = std::move(body)](std::error_code connect_ec, const tcp::endpoint&) mutable {
                                if (connect_ec) {
                                    metrics_->on_upstream_connect_failure();
                                    return write_mitm_error("502 Bad Gateway");
                                }
                                do_upstream_tls_handshake(std::move(request), std::move(body));
                            }));
                }));
    }

    void do_upstream_tls_handshake(HttpRequestHead request, std::string body) {
        upstream_ssl_->async_handshake(asio::ssl::stream_base::client,
            asio::bind_executor(strand_,
                [this, self = shared_from_this(),
                 request = std::move(request),
                 body = std::move(body)](std::error_code ec) mutable {
                    if (ec) {
                        metrics_->on_upstream_connect_failure();
                        return write_mitm_error("502 Bad Gateway");
                    }
                    forward_mitm_request(std::move(request), std::move(body));
                }));
    }

    void forward_mitm_request(HttpRequestHead request, std::string body) {
        auto wire = std::make_shared<std::string>(
            rebuild_http_request(request, target_.host, body));

        asio::async_write(*upstream_ssl_, asio::buffer(*wire),
            asio::bind_executor(strand_,
                [this, self = shared_from_this(), wire](std::error_code ec, std::size_t bytes) {
                    if (ec) return write_mitm_error("502 Bad Gateway");
                    metrics_->add_client_to_upstream_bytes(bytes);
                    relay_mitm_response(std::make_shared<std::string>());
                }));
    }

    void relay_mitm_response(std::shared_ptr<std::string> response_buf) {
        auto chunk = std::make_shared<std::vector<char>>(config_.relay_buffer_bytes);
        upstream_ssl_->async_read_some(asio::buffer(*chunk),
            asio::bind_executor(strand_,
                [this, self = shared_from_this(), response_buf, chunk](
                    std::error_code ec, std::size_t bytes) mutable {
                    if (ec) {
                        maybe_cache_response(*response_buf);
                        return shutdown_mitm_both();
                    }

                    metrics_->add_upstream_to_client_bytes(bytes);

                    if (cache_ && !pending_cache_key_.empty()
                        && response_buf->size() + bytes <= config_.max_body_bytes) {
                        response_buf->append(chunk->data(), bytes);
                    } else if (cache_ && !pending_cache_key_.empty()) {
                        pending_cache_key_.clear();  // response too large to cache
                    }

                    asio::async_write(*client_ssl_, asio::buffer(chunk->data(), bytes),
                        asio::bind_executor(strand_,
                            [this, self, response_buf, chunk](
                                std::error_code write_ec, std::size_t) mutable {
                                if (write_ec) {
                                    shutdown_mitm_both();
                                    return;
                                }
                                relay_mitm_response(std::move(response_buf));
                            }));
                }));
    }

    void maybe_cache_response(const std::string& response) {
        if (!cache_ || pending_cache_key_.empty() || response.size() < 16) return;
        if (!response.starts_with("HTTP/")) return;
        if (response.find("text/event-stream") != std::string::npos) return;
        cache_->put(pending_cache_key_, response,
                    std::chrono::seconds(config_.cache_ttl_seconds));
        pending_cache_key_.clear();
    }

    void write_mitm_error(std::string_view status) {
        if (!client_ssl_) return;
        static constexpr std::string_view body = "FluxGate proxy error\n";
        auto resp = std::make_shared<std::string>();
        *resp = "HTTP/1.1 ";
        *resp += status;
        *resp += "\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: ";
        *resp += std::to_string(body.size());
        *resp += "\r\n\r\n";
        *resp += body;

        asio::async_write(*client_ssl_, asio::buffer(*resp),
            asio::bind_executor(strand_,
                [this, self = shared_from_this(), resp](std::error_code, std::size_t) {
                    shutdown_mitm_client();
                }));
    }

    void shutdown_mitm_client() {
        if (!client_ssl_) return;
        auto ssl = client_ssl_;
        ssl->async_shutdown(asio::bind_executor(strand_,
            [self = shared_from_this(), ssl](std::error_code) {
                std::error_code ignored;
                ssl->lowest_layer().close(ignored);
            }));
    }

    void shutdown_mitm_both() {
        if (upstream_ssl_) {
            auto up = upstream_ssl_;
            up->async_shutdown(asio::bind_executor(strand_,
                [this, self = shared_from_this(), up](std::error_code) mutable {
                    std::error_code ignored;
                    up->lowest_layer().close(ignored);
                    shutdown_mitm_client();
                }));
        } else {
            shutdown_mitm_client();
        }
    }

    // ── Members ──────────────────────────────────────────────────────────────

    tcp::socket client_socket_;
    tcp::socket upstream_socket_;
    tcp::resolver resolver_;
    asio::strand<asio::any_io_executor> strand_;
    asio::streambuf client_header_;
    AppConfig config_;
    std::shared_ptr<Metrics> metrics_;
    SharedMitmServices mitm_services_;
    std::shared_ptr<MemoryCache> cache_;
    std::shared_ptr<FilterPipeline> filter_pipeline_;
    std::vector<char> client_to_upstream_;
    std::vector<char> upstream_to_client_;
    ConnectTarget target_;
    std::string response_;
    std::uint64_t id_;

    // MITM-specific
    std::shared_ptr<asio::ssl::context> client_ssl_ctx_;
    std::shared_ptr<asio::ssl::stream<tcp::socket>> client_ssl_;
    std::shared_ptr<asio::ssl::context> upstream_ssl_ctx_;
    std::shared_ptr<asio::ssl::stream<tcp::socket>> upstream_ssl_;
    std::string pending_cache_key_;
};

} // namespace

ProxyServer::ProxyServer(asio::io_context& io_context, AppConfig config, SharedMitmServices mitm_services)
    : config_(std::move(config)),
      acceptor_(io_context),
      metrics_(std::make_shared<Metrics>()),
      mitm_services_(std::move(mitm_services)) {

    filter_pipeline_ = std::make_shared<FilterPipeline>();
    if (config_.enable_pii_redaction)
        filter_pipeline_->add(std::make_unique<PiiRedactionFilter>());
    if (config_.max_chat_history > 0)
        filter_pipeline_->add(std::make_unique<ChatHistoryLimitFilter>(config_.max_chat_history));

    if (config_.enable_cache)
        cache_ = std::make_shared<MemoryCache>(config_.cache_max_entries);

    tcp::resolver resolver(io_context);
    const auto endpoints = resolver.resolve(config_.listen_host, std::to_string(config_.listen_port));
    const auto endpoint = *endpoints.begin();

    acceptor_.open(endpoint.endpoint().protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint.endpoint());
    acceptor_.listen(asio::socket_base::max_listen_connections);
    do_accept();
}

void ProxyServer::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!ec) {
            const auto id = ++session_id_;
            metrics_->on_session_accepted();
            std::make_shared<Session>(
                std::move(socket), config_, metrics_, mitm_services_,
                cache_, filter_pipeline_, id)->start();
        } else {
            std::cerr << "accept failed: " << ec.message() << '\n';
        }
        do_accept();
    });
}

MetricsSnapshot ProxyServer::metrics() const {
    return metrics_->snapshot();
}

std::shared_ptr<Metrics> ProxyServer::shared_metrics() const {
    return metrics_;
}

} // namespace fluxgate
