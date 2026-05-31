#include "fluxgate/cert_authority.h"

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <ctime>

namespace fluxgate {
namespace {

template <typename T, auto Deleter>
using OpenSslPtr = std::unique_ptr<T, decltype(Deleter)>;

using BioPtr = OpenSslPtr<BIO, BIO_free>;
using EvpKeyPtr = OpenSslPtr<EVP_PKEY, EVP_PKEY_free>;
using EvpKeyCtxPtr = OpenSslPtr<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
using X509Ptr = OpenSslPtr<X509, X509_free>;
using X509NamePtr = OpenSslPtr<X509_NAME, X509_NAME_free>;
using GeneralNamesPtr = OpenSslPtr<GENERAL_NAMES, GENERAL_NAMES_free>;

std::runtime_error openssl_error(std::string_view message) {
    return std::runtime_error(std::string(message));
}

EvpKeyPtr generate_rsa_key() {
    EvpKeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    if (!context) {
        throw openssl_error("failed to allocate OpenSSL key context");
    }
    if (EVP_PKEY_keygen_init(context.get()) != 1) {
        throw openssl_error("failed to initialize RSA key generation");
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) != 1) {
        throw openssl_error("failed to set RSA key size");
    }

    EVP_PKEY* raw_key = nullptr;
    if (EVP_PKEY_keygen(context.get(), &raw_key) != 1 || raw_key == nullptr) {
        throw openssl_error("failed to generate RSA key");
    }
    EvpKeyPtr key(raw_key, EVP_PKEY_free);
    return std::move(key);
}

std::string bio_to_string(BIO* bio) {
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(bio, &memory);
    if (memory == nullptr || memory->data == nullptr) {
        return {};
    }
    return std::string(memory->data, memory->length);
}

std::string private_key_to_pem(EVP_PKEY* key) {
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        throw openssl_error("failed to serialize private key");
    }
    return bio_to_string(bio.get());
}

std::string certificate_to_pem(X509* cert) {
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_X509(bio.get(), cert) != 1) {
        throw openssl_error("failed to serialize certificate");
    }
    return bio_to_string(bio.get());
}

EvpKeyPtr private_key_from_pem(std::string_view pem) {
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    if (!bio) {
        throw openssl_error("failed to allocate private key BIO");
    }
    EvpKeyPtr key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!key) {
        throw openssl_error("failed to parse private key PEM");
    }
    return std::move(key);
}

X509Ptr certificate_from_pem(std::string_view pem) {
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    if (!bio) {
        throw openssl_error("failed to allocate certificate BIO");
    }
    X509Ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!cert) {
        throw openssl_error("failed to parse certificate PEM");
    }
    return std::move(cert);
}

void set_subject_name(X509* cert, std::string_view common_name) {
    X509_NAME* name = X509_get_subject_name(cert);
    if (name == nullptr) {
        throw openssl_error("failed to get certificate subject name");
    }
    if (X509_NAME_add_entry_by_txt(
            name,
            "CN",
            MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>(common_name.data()),
            static_cast<int>(common_name.size()),
            -1,
            0)
        != 1) {
        throw openssl_error("failed to set certificate common name");
    }
}

void add_extension(X509* cert, X509* issuer, int nid, const std::string& value) {
    X509V3_CTX context;
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, issuer, cert, nullptr, nullptr, 0);
    X509_EXTENSION* raw = X509V3_EXT_conf_nid(nullptr, &context, nid, value.c_str());
    if (raw == nullptr) {
        throw openssl_error("failed to create X509 extension");
    }
    OpenSslPtr<X509_EXTENSION, X509_EXTENSION_free> extension(raw, X509_EXTENSION_free);
    if (X509_add_ext(cert, extension.get(), -1) != 1) {
        throw openssl_error("failed to add X509 extension");
    }
}

X509Ptr create_certificate(EVP_PKEY* key, std::string_view common_name, int valid_days) {
    X509Ptr cert(X509_new(), X509_free);
    if (!cert) {
        throw openssl_error("failed to allocate certificate");
    }
    if (X509_set_version(cert.get(), 2) != 1) {
        throw openssl_error("failed to set certificate version");
    }
    if (ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), static_cast<long>(std::time(nullptr))) != 1) {
        throw openssl_error("failed to set certificate serial");
    }
    if (X509_gmtime_adj(X509_get_notBefore(cert.get()), 0) == nullptr
        || X509_gmtime_adj(X509_get_notAfter(cert.get()), static_cast<long>(60 * 60 * 24 * valid_days)) == nullptr) {
        throw openssl_error("failed to set certificate validity");
    }
    if (X509_set_pubkey(cert.get(), key) != 1) {
        throw openssl_error("failed to set certificate public key");
    }
    set_subject_name(cert.get(), common_name);
    return std::move(cert);
}

bool looks_like_ip_address(std::string_view host) {
    return host.find(':') != std::string_view::npos
        || host.find_first_not_of("0123456789.") == std::string_view::npos;
}

std::string san_value(std::string_view host) {
    std::string value = looks_like_ip_address(host) ? "IP:" : "DNS:";
    value += host;
    return value;
}

} // namespace

struct CertificateAuthority::Impl {
    EvpKeyPtr key;
    X509Ptr cert;

    Impl(EvpKeyPtr key_in, X509Ptr cert_in)
        : key(std::move(key_in)),
          cert(std::move(cert_in)) {}
};

CertificateAuthority::CertificateAuthority(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

CertificateAuthority::CertificateAuthority(CertificateAuthority&&) noexcept = default;
CertificateAuthority& CertificateAuthority::operator=(CertificateAuthority&&) noexcept = default;
CertificateAuthority::~CertificateAuthority() = default;

CertificateAuthority CertificateAuthority::create_self_signed(std::string common_name, int valid_days) {
    if (common_name.empty() || valid_days <= 0) {
        throw std::invalid_argument("certificate authority requires a common name and positive validity");
    }

    auto key = generate_rsa_key();
    auto cert = create_certificate(key.get(), common_name, valid_days);
    if (X509_set_issuer_name(cert.get(), X509_get_subject_name(cert.get())) != 1) {
        throw openssl_error("failed to set CA issuer");
    }
    add_extension(cert.get(), cert.get(), NID_basic_constraints, "critical,CA:TRUE,pathlen:1");
    add_extension(cert.get(), cert.get(), NID_key_usage, "critical,keyCertSign,cRLSign");
    add_extension(cert.get(), cert.get(), NID_subject_key_identifier, "hash");

    if (X509_sign(cert.get(), key.get(), EVP_sha256()) == 0) {
        throw openssl_error("failed to sign CA certificate");
    }

    return CertificateAuthority(std::make_unique<Impl>(std::move(key), std::move(cert)));
}

CertificateAuthority CertificateAuthority::from_pem(std::string private_key_pem, std::string certificate_pem) {
    auto key = private_key_from_pem(private_key_pem);
    auto cert = certificate_from_pem(certificate_pem);
    return CertificateAuthority(std::make_unique<Impl>(std::move(key), std::move(cert)));
}

PemKeyPair CertificateAuthority::pem() const {
    return {
        .private_key_pem = private_key_to_pem(impl_->key.get()),
        .certificate_pem = certificate_to_pem(impl_->cert.get()),
    };
}

LeafCertificate CertificateAuthority::issue_leaf(std::string_view host, int valid_days) const {
    if (host.empty() || valid_days <= 0) {
        throw std::invalid_argument("leaf certificate requires a host and positive validity");
    }
    if (!can_sign()) {
        throw std::runtime_error("certificate authority cannot sign certificates");
    }

    auto key = generate_rsa_key();
    auto cert = create_certificate(key.get(), host, valid_days);
    if (X509_set_issuer_name(cert.get(), X509_get_subject_name(impl_->cert.get())) != 1) {
        throw openssl_error("failed to set leaf issuer");
    }
    add_extension(cert.get(), impl_->cert.get(), NID_basic_constraints, "critical,CA:FALSE");
    add_extension(cert.get(), impl_->cert.get(), NID_key_usage, "critical,digitalSignature,keyEncipherment");
    add_extension(cert.get(), impl_->cert.get(), NID_ext_key_usage, "serverAuth");
    add_extension(cert.get(), impl_->cert.get(), NID_subject_alt_name, san_value(host));

    if (X509_sign(cert.get(), impl_->key.get(), EVP_sha256()) == 0) {
        throw openssl_error("failed to sign leaf certificate");
    }

    const auto leaf_pem = certificate_to_pem(cert.get());
    const auto ca_pem = certificate_to_pem(impl_->cert.get());
    return {
        .private_key_pem = private_key_to_pem(key.get()),
        .certificate_pem = leaf_pem,
        .chain_pem = leaf_pem + ca_pem,
    };
}

bool CertificateAuthority::can_sign() const {
    return impl_ && impl_->key && impl_->cert && X509_check_ca(impl_->cert.get()) == 1;
}

bool certificate_has_dns_subject_alt_name(std::string_view certificate_pem, std::string_view dns_name) {
    auto cert = certificate_from_pem(certificate_pem);
    GeneralNamesPtr names(
        static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr)),
        GENERAL_NAMES_free);
    if (!names) {
        return false;
    }

    const int count = sk_GENERAL_NAME_num(names.get());
    for (int i = 0; i < count; ++i) {
        const GENERAL_NAME* name = sk_GENERAL_NAME_value(names.get(), i);
        if (name->type != GEN_DNS) {
            continue;
        }
        const auto* data = ASN1_STRING_get0_data(name->d.dNSName);
        const auto length = ASN1_STRING_length(name->d.dNSName);
        if (std::string_view(reinterpret_cast<const char*>(data), static_cast<std::size_t>(length)) == dns_name) {
            return true;
        }
    }
    return false;
}

} // namespace fluxgate
