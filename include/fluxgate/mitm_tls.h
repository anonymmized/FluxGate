#pragma once

#include "fluxgate/leaf_certificate_cache.h"

#include <asio/ssl.hpp>

#include <memory>
#include <string_view>

namespace fluxgate {

class MitmTlsFactory {
public:
    explicit MitmTlsFactory(LeafCertificateCache& certificates);

    std::unique_ptr<asio::ssl::context> create_server_context(std::string_view host) const;

private:
    LeafCertificateCache& certificates_;
};

} // namespace fluxgate
