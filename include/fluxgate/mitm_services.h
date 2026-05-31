#pragma once

#include "fluxgate/cert_authority.h"
#include "fluxgate/leaf_certificate_cache.h"
#include "fluxgate/mitm_tls.h"

#include <memory>

namespace fluxgate {

struct MitmServices {
    CertificateAuthority authority;
    LeafCertificateCache leaf_certificates;
    MitmTlsFactory tls_factory;

    MitmServices(CertificateAuthority authority_in, std::size_t leaf_cache_entries, int leaf_valid_days)
        : authority(std::move(authority_in)),
          leaf_certificates(authority, leaf_cache_entries, leaf_valid_days),
          tls_factory(leaf_certificates) {}
};

using SharedMitmServices = std::shared_ptr<MitmServices>;

} // namespace fluxgate
