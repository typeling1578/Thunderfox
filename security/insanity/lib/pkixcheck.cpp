/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* Copyright 2013 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "insanity/pkix.h"
#include "pkixcheck.h"
#include "pkixder.h"
#include "pkixutil.h"
#include "secder.h"

namespace insanity { namespace pkix {

Result
CheckTimes(const CERTCertificate* cert, PRTime time)
{
  PR_ASSERT(cert);

  SECCertTimeValidity validity = CERT_CheckCertValidTimes(cert, time, false);
  if (validity != secCertTimeValid) {
    return Fail(RecoverableError, SEC_ERROR_EXPIRED_CERTIFICATE);
  }

  return Success;
}

// 4.2.1.3. Key Usage (id-ce-keyUsage)
// Modeled after GetKeyUsage in certdb.c
Result
CheckKeyUsage(EndEntityOrCA endEntityOrCA,
              bool isTrustAnchor,
              const SECItem* encodedKeyUsage,
              KeyUsages requiredKeyUsagesIfPresent,
              PLArenaPool* arena)
{
  if (!encodedKeyUsage) {
    // TODO: Reject certificates that are being used to verify certificate
    // signatures unless the certificate is a trust anchor, to reduce the
    // chances of an end-entity certificate being abused as a CA certificate.
    // if (endEntityOrCA == MustBeCA && !isTrustAnchor) {
    //   return Fail(RecoverableError, SEC_ERROR_INADEQUATE_KEY_USAGE);
    // }
    //
    // TODO: Users may configure arbitrary certificates as trust anchors, not
    // just roots. We should only allow a certificate without a key usage to be
    // used as a CA when it is self-issued and self-signed.
    return Success;
  }

  SECItem tmpItem;
  Result rv = MapSECStatus(SEC_QuickDERDecodeItem(arena, &tmpItem,
                              SEC_ASN1_GET(SEC_BitStringTemplate),
                              encodedKeyUsage));
  if (rv != Success) {
    return rv;
  }

  // TODO XXX: Why is tmpItem.len > 1?

  KeyUsages allowedKeyUsages = tmpItem.data[0];
  if ((allowedKeyUsages & requiredKeyUsagesIfPresent)
        != requiredKeyUsagesIfPresent) {
    return Fail(RecoverableError, SEC_ERROR_INADEQUATE_KEY_USAGE);
  }

  if (endEntityOrCA == MustBeCA) {
   // "If the keyUsage extension is present, then the subject public key
   //  MUST NOT be used to verify signatures on certificates or CRLs unless
   //  the corresponding keyCertSign or cRLSign bit is set."
   if ((allowedKeyUsages & KU_KEY_CERT_SIGN) == 0) {
      return Fail(RecoverableError, SEC_ERROR_INADEQUATE_KEY_USAGE);
    }
  } else {
    // "The keyCertSign bit is asserted when the subject public key is
    //  used for verifying signatures on public key certificates.  If the
    //  keyCertSign bit is asserted, then the cA bit in the basic
    //  constraints extension (Section 4.2.1.9) MUST also be asserted."
    // TODO XXX: commented out to match classic NSS behavior.
    //if ((allowedKeyUsages & KU_KEY_CERT_SIGN) != 0) {
    //  // XXX: better error code.
    //  return Fail(RecoverableError, SEC_ERROR_INADEQUATE_CERT_TYPE);
    //}
  }

  return Success;
}

// RFC5280 4.2.1.9. Basic Constraints (id-ce-basicConstraints)
Result
CheckBasicConstraints(const BackCert& cert,
                      EndEntityOrCA endEntityOrCA,
                      bool isTrustAnchor,
                      unsigned int subCACount)
{
  CERTBasicConstraints basicConstraints;
  if (cert.encodedBasicConstraints) {
    SECStatus rv = CERT_DecodeBasicConstraintValue(&basicConstraints,
                                                   cert.encodedBasicConstraints);
    if (rv != SECSuccess) {
      return MapSECStatus(rv);
    }
  } else {
    // Synthesize a non-CA basic constraints by default
    basicConstraints.isCA = false;
    basicConstraints.pathLenConstraint = 0;

    // "If the basic constraints extension is not present in a version 3
    //  certificate, or the extension is present but the cA boolean is not
    //  asserted, then the certified public key MUST NOT be used to verify
    //  certificate signatures."
    //
    // For compatibility, we must accept v1 trust anchors without basic
    // constraints as CAs.
    //
    // TODO: add check for self-signedness?
    if (endEntityOrCA == MustBeCA && isTrustAnchor) {
      const CERTCertificate* nssCert = cert.GetNSSCert();

      der::Input versionDer;
      if (versionDer.Init(nssCert->version.data, nssCert->version.len)
            != der::Success) {
        return RecoverableError;
      }
      uint8_t version;
      if (der::OptionalVersion(versionDer, version) || der::End(versionDer)
            != der::Success) {
        return RecoverableError;
      }
      if (version == 1) {
        basicConstraints.isCA = true;
        basicConstraints.pathLenConstraint = CERT_UNLIMITED_PATH_CONSTRAINT;
      }
    }
  }

  if (endEntityOrCA == MustBeEndEntity) {
    // CA certificates are not trusted as EE certs.

    if (basicConstraints.isCA) {
      // XXX: We use SEC_ERROR_CA_CERT_INVALID here so we can distinguish
      // this error from other errors, given that NSS does not have a "CA cert
      // used as end-entity" error code since it doesn't have such a
      // prohibition. We should add such an error code and stop abusing
      // SEC_ERROR_CA_CERT_INVALID this way.
      //
      // Note, in particular, that this check prevents a delegated OCSP
      // response signing certificate with the CA bit from successfully
      // validating when we check it from pkixocsp.cpp, which is a good thing.
      //
      return Fail(RecoverableError, SEC_ERROR_CA_CERT_INVALID);
    }

    return Success;
  }

  PORT_Assert(endEntityOrCA == MustBeCA);

  // End-entity certificates are not allowed to act as CA certs.
  if (!basicConstraints.isCA) {
    return Fail(RecoverableError, SEC_ERROR_CA_CERT_INVALID);
  }

  if (basicConstraints.pathLenConstraint >= 0) {
    if (subCACount >
           static_cast<unsigned int>(basicConstraints.pathLenConstraint)) {
      return Fail(RecoverableError, SEC_ERROR_PATH_LEN_CONSTRAINT_INVALID);
    }
  }

  return Success;
}

Result
BackCert::GetConstrainedNames(/*out*/ const CERTGeneralName** result)
{
  if (!constrainedNames) {
    if (!GetArena()) {
      return FatalError;
    }

    constrainedNames =
      CERT_GetConstrainedCertificateNames(nssCert, arena.get(),
                                          cnOptions == IncludeCN);
    if (!constrainedNames) {
      return MapSECStatus(SECFailure);
    }
  }

  *result = constrainedNames;
  return Success;
}

// 4.2.1.10. Name Constraints
Result
CheckNameConstraints(BackCert& cert)
{
  if (!cert.encodedNameConstraints) {
    return Success;
  }

  PLArenaPool* arena = cert.GetArena();
  if (!arena) {
    return FatalError;
  }

  // Owned by arena
  const CERTNameConstraints* constraints =
    CERT_DecodeNameConstraintsExtension(arena, cert.encodedNameConstraints);
  if (!constraints) {
    return MapSECStatus(SECFailure);
  }

  for (BackCert* prev = cert.childCert; prev; prev = prev->childCert) {
    const CERTGeneralName* names = nullptr;
    Result rv = prev->GetConstrainedNames(&names);
    if (rv != Success) {
      return rv;
    }
    PORT_Assert(names);
    CERTGeneralName* currentName = const_cast<CERTGeneralName*>(names);
    do {
      rv = MapSECStatus(CERT_CheckNameSpace(arena, constraints, currentName));
      if (rv != Success) {
        return rv;
      }
      currentName = CERT_GetNextGeneralName(currentName);
    } while (currentName != names);
  }

  return Success;
}

// 4.2.1.12. Extended Key Usage (id-ce-extKeyUsage)
// 4.2.1.12. Extended Key Usage (id-ce-extKeyUsage)
Result
CheckExtendedKeyUsage(EndEntityOrCA endEntityOrCA, const SECItem* encodedEKUs,
                      SECOidTag requiredEKU)
{
  // TODO: Either do not allow anyExtendedKeyUsage to be passed as requiredEKU,
  // or require that callers pass anyExtendedKeyUsage instead of
  // SEC_OID_UNKNWON and disallow SEC_OID_UNKNWON.

  // XXX: We're using SEC_ERROR_INADEQUATE_CERT_TYPE here so that callers can
  // distinguish EKU mismatch from KU mismatch from basic constraints mismatch.
  // We should probably add a new error code that is more clear for this type
  // of problem.

  bool foundOCSPSigning = false;

  if (encodedEKUs) {
    ScopedPtr<CERTOidSequence, CERT_DestroyOidSequence>
      seq(CERT_DecodeOidSequence(encodedEKUs));
    if (!seq) {
      PR_SetError(SEC_ERROR_INADEQUATE_CERT_TYPE, 0);
      return RecoverableError;
    }

    bool found = false;

    // XXX: We allow duplicate entries.
    for (const SECItem* const* oids = seq->oids; oids && *oids; ++oids) {
      SECOidTag oidTag = SECOID_FindOIDTag(*oids);
      if (requiredEKU != SEC_OID_UNKNOWN && oidTag == requiredEKU) {
        found = true;
      }
      if (oidTag == SEC_OID_OCSP_RESPONDER) {
        foundOCSPSigning = true;
      }
    }

    // If the EKU extension was included, then the required EKU must be in the
    // list.
    if (!found) {
      PR_SetError(SEC_ERROR_INADEQUATE_CERT_TYPE, 0);
      return RecoverableError;
    }
  }

  // pkixocsp.cpp depends on the following additional checks.

  if (foundOCSPSigning) {
    // When validating anything other than an delegated OCSP signing cert,
    // reject any cert that also claims to be an OCSP responder, because such
    // a cert does not make sense. For example, if an SSL certificate were to
    // assert id-kp-OCSPSigning then it could sign OCSP responses for itself,
    // if not for this check.
    if (requiredEKU != SEC_OID_OCSP_RESPONDER) {
      PR_SetError(SEC_ERROR_INADEQUATE_CERT_TYPE, 0);
      return RecoverableError;
    }
  } else if (requiredEKU == SEC_OID_OCSP_RESPONDER &&
             endEntityOrCA == MustBeEndEntity) {
    // http://tools.ietf.org/html/rfc6960#section-4.2.2.2:
    // "OCSP signing delegation SHALL be designated by the inclusion of
    // id-kp-OCSPSigning in an extended key usage certificate extension
    // included in the OCSP response signer's certificate."
    //
    // id-kp-OCSPSigning is the only EKU that isn't implicitly assumed when the
    // EKU extension is missing from an end-entity certificate. However, any CA
    // certificate can issue a delegated OCSP response signing certificate, so
    // we can't require the EKU be explicitly included for CA certificates.
    PR_SetError(SEC_ERROR_INADEQUATE_CERT_TYPE, 0);
    return RecoverableError;
  }

  return Success;
}

// Checks extensions that apply to both EE and intermediate certs,
// except for AIA, CRL, and AKI/SKI, which are handled elsewhere.
Result
CheckExtensions(BackCert& cert,
                EndEntityOrCA endEntityOrCA,
                bool isTrustAnchor,
                KeyUsages requiredKeyUsagesIfPresent,
                SECOidTag requiredEKUIfPresent,
                unsigned int subCACount)
{
  // 4.2.1.1. Authority Key Identifier dealt with as part of path building
  // 4.2.1.2. Subject Key Identifier dealt with as part of path building

  PLArenaPool* arena = cert.GetArena();
  if (!arena) {
    return FatalError;
  }

  Result rv;

  // 4.2.1.3. Key Usage

  rv = CheckKeyUsage(endEntityOrCA, isTrustAnchor, cert.encodedKeyUsage,
                     requiredKeyUsagesIfPresent, arena);
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.4. Certificate Policies
  // 4.2.1.5. Policy Mappings are rejected in BackCert::Init()
  // 4.2.1.6. Subject Alternative Name dealt with elsewhere
  // 4.2.1.7. Issuer Alternative Name is not something that needs checking
  // 4.2.1.8. Subject Directory Attributes is not something that needs checking

  // 4.2.1.9. Basic Constraints. We check basic constraints before other
  // properties that take endEntityOrCA so that those checks can use
  // endEntityOrCA as a proxy for the isCA bit from the basic constraints.
  rv = CheckBasicConstraints(cert, endEntityOrCA, isTrustAnchor, subCACount);
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.10. Name Constraints
  // 4.2.1.11. Policy Constraints

  // 4.2.1.12. Extended Key Usage
  rv = CheckExtendedKeyUsage(endEntityOrCA, cert.encodedExtendedKeyUsage,
                             requiredEKUIfPresent);
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.13. CRL Distribution Points will be dealt with elsewhere
  // 4.2.1.14. Inhibit anyPolicy

  return Success;
}

} } // namespace insanity::pkix
