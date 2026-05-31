#include "fluxgate/mitm_tls.h"

#include <stdexcept>

namespace fluxgate {

MitmTlsFactory::MitmTlsFactory(LeafCertificateCache& certificates)
    : certificates_(certificates) {}

std::unique_ptr<asio::ssl::context> MitmTlsFactory::create_server_context(std::string_view host) const {
    auto certificate = certificates_.get(host);
    auto context = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);

    context->set_options(
        asio::ssl::context::default_workarounds
        | asio::ssl::context::no_sslv2
        | asio::ssl::context::no_sslv3
        | asio::ssl::context::no_tlsv1
        | asio::ssl::context::no_tlsv1_1
        | asio::ssl::context::single_dh_use);

    context->use_certificate_chain(
        asio::buffer(certificate.chain_pem.data(), certificate.chain_pem.size()));
    context->use_private_key(
        asio::buffer(certificate.private_key_pem.data(), certificate.private_key_pem.size()),
        asio::ssl::context::pem);

    return context;
}

} // namespace fluxgate
