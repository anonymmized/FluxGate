#include "fluxgate/admin_server.h"

#include <iostream>
#include <memory>
#include <string>

namespace fluxgate {
namespace {

using asio::ip::tcp;

class AdminSession final : public std::enable_shared_from_this<AdminSession> {
public:
    AdminSession(tcp::socket socket, std::shared_ptr<Metrics> metrics)
        : socket_(std::move(socket)),
          metrics_(std::move(metrics)) {}

    void start() {
        asio::async_read_until(socket_, request_, "\r\n\r\n",
            [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                if (ec) {
                    return close();
                }
                handle_request(bytes);
            });
    }

private:
    void handle_request(std::size_t bytes) {
        const auto data = request_.data();
        const auto begin = asio::buffers_begin(data);
        const std::string request(begin, begin + static_cast<std::ptrdiff_t>(bytes));

        if (request.starts_with("GET /healthz ")) {
            write_response("200 OK", "text/plain", "ok\n");
        } else if (request.starts_with("GET /metrics ")) {
            write_response("200 OK", "text/plain; version=0.0.4", to_prometheus_text(metrics_->snapshot()));
        } else {
            write_response("404 Not Found", "text/plain", "not found\n");
        }
    }

    void write_response(std::string_view status, std::string_view content_type, std::string body) {
        response_ = "HTTP/1.1 ";
        response_ += status;
        response_ += "\r\nContent-Type: ";
        response_ += content_type;
        response_ += "\r\nConnection: close\r\nContent-Length: ";
        response_ += std::to_string(body.size());
        response_ += "\r\n\r\n";
        response_ += body;

        asio::async_write(socket_, asio::buffer(response_),
            [this, self = shared_from_this()](std::error_code, std::size_t) {
                close();
            });
    }

    void close() {
        std::error_code ignored;
        socket_.shutdown(tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }

    tcp::socket socket_;
    asio::streambuf request_;
    std::shared_ptr<Metrics> metrics_;
    std::string response_;
};

} // namespace

AdminServer::AdminServer(asio::io_context& io_context, std::string host, unsigned short port, std::shared_ptr<Metrics> metrics)
    : acceptor_(io_context),
      metrics_(std::move(metrics)) {
    tcp::resolver resolver(io_context);
    const auto endpoints = resolver.resolve(host, std::to_string(port));
    const auto endpoint = *endpoints.begin();

    acceptor_.open(endpoint.endpoint().protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint.endpoint());
    acceptor_.listen(asio::socket_base::max_listen_connections);
    do_accept();
}

void AdminServer::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<AdminSession>(std::move(socket), metrics_)->start();
        } else {
            std::cerr << "admin accept failed: " << ec.message() << '\n';
        }
        do_accept();
    });
}

} // namespace fluxgate
