#include "fluxgate/mitm_tls.h"

#include <openssl/ssl.h>

namespace fluxgate {

MitmTlsFactory::MitmTlsFactory(LeafCertificateCache& certificates)
    : certificates_(certificates) {}

std::unique_ptr<asio::ssl::context> MitmTlsFactory::create_server_context(std::string_view host) const {
    auto certificate = certificates_.get(host);
    auto context = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);

    // Only default_workarounds — let OpenSSL negotiate TLS version freely
    // (restricting versions here breaks LibreSSL clients like macOS curl).
    context->set_options(asio::ssl::context::default_workarounds);

    // Explicit TLS 1.2 minimum via native handle — works with both
    // OpenSSL and LibreSSL builds on the client side.
    SSL_CTX_set_min_proto_version(context->native_handle(), TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(context->native_handle(), 0); // 0 = max supported

    // ALPN: advertise http/1.1. Without this callback, some LibreSSL/OpenSSL
    // combinations send a protocol_version alert on ALPN mismatch.
    static const unsigned char h11[] = {8,'h','t','t','p','/','1','.','1'};
    SSL_CTX_set_alpn_select_cb(context->native_handle(),
        [](SSL*, const unsigned char** out, unsigned char* outlen,
           const unsigned char* in, unsigned int inlen, void*) -> int {
            // Try to negotiate http/1.1 from client's list.
            // If not offered, select it anyway — we speak HTTP/1.1 only.
            if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen,
                    h11, sizeof(h11), in, inlen) != OPENSSL_NPN_NEGOTIATED) {
                *out = h11 + 1;   // skip the length byte
                *outlen = 8;       // "http/1.1"
            }
            return SSL_TLSEXT_ERR_OK;
        }, nullptr);

    context->use_certificate_chain(
        asio::buffer(certificate.chain_pem.data(), certificate.chain_pem.size()));
    context->use_private_key(
        asio::buffer(certificate.private_key_pem.data(), certificate.private_key_pem.size()),
        asio::ssl::context::pem);

    return context;
}

} // namespace fluxgate
