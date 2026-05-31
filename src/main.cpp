#include <asio.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using asio::ip::tcp;

namespace {

constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
constexpr std::size_t kRelayBufferBytes = 32 * 1024;

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

struct ConnectTarget {
    std::string host;
    std::string port = "443";
};

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

unsigned short parse_port(int argc, char* argv[]) {
    if (argc < 2) {
        return 8080;
    }

    int parsed = 0;
    const std::string_view arg(argv[1]);
    const auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), parsed);
    if (ec != std::errc{} || ptr != arg.data() + arg.size() || parsed <= 0 || parsed > 65535) {
        throw std::invalid_argument("usage: FluxGate [listen-port]");
    }
    return static_cast<unsigned short>(parsed);
}

std::size_t worker_count() {
    const auto detected = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, detected == 0 ? 1 : detected);
}

} // namespace

class Session final : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket client_socket, std::string id)
        : client_socket_(std::move(client_socket)),
          upstream_socket_(client_socket_.get_executor()),
          resolver_(client_socket_.get_executor()),
          strand_(asio::make_strand(client_socket_.get_executor())),
          id_(std::move(id)) {}

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
                    if (client_header_.size() > kMaxHeaderBytes) {
                        return reject("431 Request Header Fields Too Large");
                    }

                    const auto header = buffer_to_string(client_header_.data(), bytes);
                    auto target = parse_connect_request(header);
                    if (!target) {
                        return reject("400 Bad Request");
                    }

                    target_ = std::move(*target);
                    client_header_.consume(bytes);
                    connect_to_upstream();
                }));
    }

    static std::string buffer_to_string(const asio::streambuf::const_buffers_type& buffers, std::size_t bytes) {
        auto begin = asio::buffers_begin(buffers);
        return std::string(begin, begin + static_cast<std::ptrdiff_t>(bytes));
    }

    void connect_to_upstream() {
        resolver_.async_resolve(target_.host, target_.port,
            asio::bind_executor(strand_,
                [this, self = shared_from_this()](std::error_code ec, tcp::resolver::results_type results) {
                    if (ec) {
                        return reject("502 Bad Gateway");
                    }

                    asio::async_connect(upstream_socket_, results,
                        asio::bind_executor(strand_,
                            [this, self](std::error_code connect_ec, const tcp::endpoint&) {
                                if (connect_ec) {
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

                    asio::async_write(upstream_socket_, asio::buffer(client_to_upstream_, bytes),
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

                    asio::async_write(client_socket_, asio::buffer(upstream_to_client_, bytes),
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

    tcp::socket client_socket_;
    tcp::socket upstream_socket_;
    tcp::resolver resolver_;
    asio::strand<asio::any_io_executor> strand_;
    asio::streambuf client_header_;
    std::array<char, kRelayBufferBytes> client_to_upstream_{};
    std::array<char, kRelayBufferBytes> upstream_to_client_{};
    ConnectTarget target_;
    std::string response_;
    std::string id_;
};

class Server final {
public:
    Server(asio::io_context& io_context, unsigned short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
            if (!ec) {
                auto id = std::to_string(++session_id_);
                std::make_shared<Session>(std::move(socket), std::move(id))->start();
            } else {
                std::cerr << "accept failed: " << ec.message() << '\n';
            }
            do_accept();
        });
    }

    tcp::acceptor acceptor_;
    std::uint64_t session_id_ = 0;
};

int main(int argc, char* argv[]) {
    try {
        const auto port = parse_port(argc, argv);
        asio::io_context io_context;

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context](const std::error_code&, int) {
            io_context.stop();
        });

        Server server(io_context, port);
        const auto threads = worker_count();
        std::cout << "FluxGate CONNECT gateway listening on 0.0.0.0:" << port
                  << " with " << threads << " worker(s)" << '\n';

        std::vector<std::thread> workers;
        workers.reserve(threads > 0 ? threads - 1 : 0);
        for (std::size_t i = 1; i < threads; ++i) {
            workers.emplace_back([&io_context] {
                io_context.run();
            });
        }

        io_context.run();
        for (auto& worker : workers) {
            worker.join();
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
