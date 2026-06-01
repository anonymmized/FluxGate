#pragma once

#include "fluxgate/metrics.h"

#include <asio.hpp>

#include <memory>
#include <string>

namespace fluxgate {

class AdminServer final {
public:
    AdminServer(asio::io_context& io_context, std::string host, unsigned short port,
                std::shared_ptr<Metrics> metrics, std::string token = {});

private:
    void do_accept();

    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<Metrics> metrics_;
    std::string token_;
};

} // namespace fluxgate
