#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fluxgate {

struct PemKeyPair {
    std::string private_key_pem;
    std::string certificate_pem;
};

struct LeafCertificate {
    std::string private_key_pem;
    std::string certificate_pem;
    std::string chain_pem;
};

class CertificateAuthority {
public:
    static CertificateAuthority create_self_signed(std::string common_name, int valid_days);
    static CertificateAuthority from_pem(std::string private_key_pem, std::string certificate_pem);

    CertificateAuthority(CertificateAuthority&&) noexcept;
    CertificateAuthority& operator=(CertificateAuthority&&) noexcept;
    CertificateAuthority(const CertificateAuthority&) = delete;
    CertificateAuthority& operator=(const CertificateAuthority&) = delete;
    ~CertificateAuthority();

    PemKeyPair pem() const;
    LeafCertificate issue_leaf(std::string_view host, int valid_days) const;
    bool can_sign() const;

private:
    struct Impl;
    explicit CertificateAuthority(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

bool certificate_has_dns_subject_alt_name(std::string_view certificate_pem, std::string_view dns_name);

} // namespace fluxgate
