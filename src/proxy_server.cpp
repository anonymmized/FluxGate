#include "fluxgate/proxy_server.h"

#include "fluxgate/connect_parser.h"
#include "fluxgate/http_message.h"

#include <asio/ssl.hpp>
#include <array>
#include <iostream>
#include <memory>
#include <string>

namespace fluxgate {
namespace {

using asio::ip::tcp;

std::string buffer_to_string(const asio::streambuf::const_buffers_type& buffers, std::size_t bytes) {
    auto begin = asio::buffers_begin(buffers);
    return std::string(begin, begin + static_cast<std::ptrdiff_t>(bytes));
}

class Session final : public std::enable_shared_from_this<Session> {
public:
    Session(
        tcp::socket client_socket,
        AppConfig config,
        std::shared_ptr<Metrics> metrics,
        SharedMitmServices mitm_services,
        std::uint64_t id)
        : client_socket_(std::move(client_socket)),
          upstream_socket_(client_socket_.get_executor()),
          resolver_(client_socket_.get_executor()),
          strand_(asio::make_strand(client_socket_.get_executor())),
          config_(std::move(config)),
          metrics_(std::move(metrics)),
          mitm_services_(std::move(mitm_services)),
          client_to_upstream_(config_.relay_buffer_bytes),
          upstream_to_client_(config_.relay_buffer_bytes),
          id_(id) {}

    ~Session() {
        if (metrics_) {
            metrics_->on_session_closed();
        }
    }

    void start() {
        read_connect_header();
    }

private:
    void read_connect_header() {
        asio::async_read_until(client_socket_, client_header_, "\r\n\r\n",
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec) {
                        return close();
                    }
                    if (client_header_.size() > config_.max_header_bytes) {
                        return reject("431 Request Header Fields Too Large");
                    }

                    const auto header = buffer_to_string(client_header_.data(), bytes);
                    auto target = parse_connect_request(header);
                    if (!target) {
                        return reject("400 Bad Request");
                    }

                    target_ = std::move(*target);
                    client_header_.consume(bytes);
                    if (mitm_services_) {
                        return accept_mitm_tunnel();
                    }
                    connect_to_upstream();
                }));
    }

    void accept_mitm_tunnel() {
        static constexpr char response[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
        asio::async_write(client_socket_, asio::buffer(response),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t) {
                    if (ec) {
                        return close();
                    }
                    start_client_tls_handshake();
                }));
    }

    void start_client_tls_handshake() {
        auto ssl_context = mitm_services_->tls_factory.create_server_context(target_.host);
        auto ssl_stream = std::make_shared<asio::ssl::stream<tcp::socket>>(
            std::move(client_socket_),
            *ssl_context);
        auto tls_buffer = std::make_shared<asio::streambuf>();

        ssl_stream->async_handshake(asio::ssl::stream_base::server,
            asio::bind_executor(strand_,
                [this,
                 self = shared_from_this(),
                 ssl_context = std::move(ssl_context),
                 ssl_stream,
                 tls_buffer](std::error_code ec) mutable {
                    if (ec) {
                        return close_upstream_only();
                    }

                    std::cout << '[' << id_ << "] mitm tls established for "
                              << target_.host << ':' << target_.port << '\n';
                    read_mitm_http_head(std::move(ssl_context), ssl_stream, tls_buffer);
                }));
    }

    void read_mitm_http_head(
        std::unique_ptr<asio::ssl::context> ssl_context,
        std::shared_ptr<asio::ssl::stream<tcp::socket>> ssl_stream,
        std::shared_ptr<asio::streambuf> tls_buffer) {
        asio::async_read_until(*ssl_stream, *tls_buffer, "\r\n\r\n",
            asio::bind_executor(strand_,
                [this,
                 self = shared_from_this(),
                 ssl_context = std::move(ssl_context),
                 ssl_stream,
                 tls_buffer](std::error_code ec, std::size_t bytes) mutable {
                    if (ec) {
                        return close_upstream_only();
                    }
                    if (tls_buffer->size() > config_.max_header_bytes) {
                        return write_mitm_response(std::move(ssl_context), ssl_stream, "431 Request Header Fields Too Large");
                    }

                    const auto header = buffer_to_string(tls_buffer->data(), bytes);
                    const auto request = parse_http_request_head(header);
                    if (!request) {
                        return write_mitm_response(std::move(ssl_context), ssl_stream, "400 Bad Request");
                    }

                    std::cout << '[' << id_ << "] intercepted "
                              << request->method << ' ' << request->target
                              << " for " << target_.host << '\n';
                    write_mitm_response(std::move(ssl_context), ssl_stream, "501 Not Implemented");
                }));
    }

    void write_mitm_response(
        std::unique_ptr<asio::ssl::context> ssl_context,
        std::shared_ptr<asio::ssl::stream<tcp::socket>> ssl_stream,
        std::string_view status) {
        static constexpr std::string_view body = "FluxGate MITM pipeline is pending\n";
        auto response = std::make_shared<std::string>();
        *response = "HTTP/1.1 ";
        *response += status;
        *response += "\r\nConnection: close\r\nContent-Type: text/plain\r\nContent-Length: ";
        *response += std::to_string(body.size());
        *response += "\r\n\r\n";
        *response += body;

        asio::async_write(*ssl_stream, asio::buffer(*response),
            asio::bind_executor(strand_,
                [this,
                 self = shared_from_this(),
                 ssl_context = std::move(ssl_context),
                 ssl_stream,
                 response](std::error_code, std::size_t) mutable {
                    ssl_stream->async_shutdown(asio::bind_executor(strand_,
                        [this, self, ssl_context = std::move(ssl_context), ssl_stream](std::error_code) mutable {
                            close_upstream_only();
                            std::error_code ignored;
                            ssl_stream->lowest_layer().close(ignored);
                        }));
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
                    if (ec) {
                        return close();
                    }

                    std::cout << '[' << id_ << "] tunnel " << target_.host << ':' << target_.port << '\n';
                    flush_prefetched_client_bytes();
                    relay_upstream_to_client();
                }));
    }

    void flush_prefetched_client_bytes() {
        if (client_header_.size() == 0) {
            return relay_client_to_upstream();
        }

        asio::async_write(upstream_socket_, client_header_.data(),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec) {
                        return close();
                    }
                    client_header_.consume(bytes);
                    relay_client_to_upstream();
                }));
    }

    void relay_client_to_upstream() {
        client_socket_.async_read_some(asio::buffer(client_to_upstream_),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec || bytes == 0) {
                        return shutdown_socket(upstream_socket_);
                    }

                    metrics_->add_client_to_upstream_bytes(bytes);
                    asio::async_write(upstream_socket_, asio::buffer(client_to_upstream_.data(), bytes),
                        asio::bind_executor(strand_,
                            [this, self](std::error_code write_ec, std::size_t) {
                                if (write_ec) {
                                    return close();
                                }
                                relay_client_to_upstream();
                            }));
                }));
    }

    void relay_upstream_to_client() {
        upstream_socket_.async_read_some(asio::buffer(upstream_to_client_),
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                    if (ec || bytes == 0) {
                        return shutdown_socket(client_socket_);
                    }

                    metrics_->add_upstream_to_client_bytes(bytes);
                    asio::async_write(client_socket_, asio::buffer(upstream_to_client_.data(), bytes),
                        asio::bind_executor(strand_,
                            [this, self](std::error_code write_ec, std::size_t) {
                                if (write_ec) {
                                    return close();
                                }
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
                [this, self = shared_from_this()](std::error_code, std::size_t) {
                    close();
                }));
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

    void close_upstream_only() {
        shutdown_socket(upstream_socket_);
        resolver_.cancel();
    }

    tcp::socket client_socket_;
    tcp::socket upstream_socket_;
    tcp::resolver resolver_;
    asio::strand<asio::any_io_executor> strand_;
    asio::streambuf client_header_;
    AppConfig config_;
    std::shared_ptr<Metrics> metrics_;
    SharedMitmServices mitm_services_;
    std::vector<char> client_to_upstream_;
    std::vector<char> upstream_to_client_;
    ConnectTarget target_;
    std::string response_;
    std::uint64_t id_;
};

} // namespace

ProxyServer::ProxyServer(asio::io_context& io_context, AppConfig config, SharedMitmServices mitm_services)
    : config_(std::move(config)),
      acceptor_(io_context),
      metrics_(std::make_shared<Metrics>()),
      mitm_services_(std::move(mitm_services)) {
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
            std::make_shared<Session>(std::move(socket), config_, metrics_, mitm_services_, id)->start();
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
