#include <iostream>
#include <asio.hpp>
#include <memory>
#include <utility>

using asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
    public:
        explicit Session(tcp::socket socket) : socket_(std::move(socket)) {}
        void start() { do_read(); }
    private:
        void do_read() {
            auto self(shared_from_this());
            socket_.async_read_some(asio::buffer(data_), [this, self](std::error_code ec, std::size_t length) {
                if (!ec) {
                    std::cout << "Received " << length << " bytes from client." << '\n';
                    do_write(length);
                }
            });
        }

        void do_write(std::size_t length) {
            auto self(shared_from_this());
            asio::async_write(socket_, asio::buffer(data_, length), [this, self](std::error_code ec, std::size_t) {
                if (!ec) do_read();
            });
        }
        tcp::socket socket_;
        char data_[4096];
};

class Server {
    public:
        Server(asio::io_context& io_context, short port) : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
            do_accept();
        }
    private:
        void do_accept() {
            acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<Session>(std::move(socket))->start();
                }
                do_accept();
            });
        }
        tcp::acceptor acceptor_;
};

int main() {
    try {
        asio::io_context io_context;
        Server serv(io_context, 8080);

        std::cout << "FluxGate listening on port 8080..." << '\n';
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << '\n';
    }
}
