/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// See
// https://wiki.mozilla.org/Security/Features/Application_Reputation_Design_Doc
// for a description of Chrome's implementation of this feature.
#include "ApplicationReputation.h"
#include "csd.pb.h"

#include "nsIArray.h"
#include "nsIApplicationReputation.h"
#include "nsIChannel.h"
#include "nsIHttpChannel.h"
#include "nsIIOService.h"
#include "nsIPrefService.h"
#include "nsIScriptSecurityManager.h"
#include "nsIStreamListener.h"
#include "nsIStringStream.h"
#include "nsIUploadChannel2.h"
#include "nsIURI.h"
#include "nsIUrlClassifierDBService.h"
#include "nsIX509Cert.h"
#include "nsIX509CertDB.h"
#include "nsIX509CertList.h"

#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/Telemetry.h"
#include "mozilla/TimeStamp.h"

#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsNetCID.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsXPCOMStrings.h"

using mozilla::Preferences;
using mozilla::TimeStamp;
using mozilla::Telemetry::Accumulate;
using safe_browsing::ClientDownloadRequest;
using safe_browsing::ClientDownloadRequest_SignatureInfo;
using safe_browsing::ClientDownloadRequest_CertificateChain;

// Preferences that we need to initialize the query. We may need another
// preference than browser.safebrowsing.malware.enabled, or simply use
// browser.safebrowsing.appRepURL. See bug 887041.
#define PREF_SB_APP_REP_URL "browser.safebrowsing.appRepURL"
#define PREF_SB_MALWARE_ENABLED "browser.safebrowsing.malware.enabled"
#define PREF_GENERAL_LOCALE "general.useragent.locale"
#define PREF_DOWNLOAD_BLOCK_TABLE "urlclassifier.downloadBlockTable"
#define PREF_DOWNLOAD_ALLOW_TABLE "urlclassifier.downloadAllowTable"

// NSPR_LOG_MODULES=ApplicationReputation:5
#if defined(PR_LOGGING)
PRLogModuleInfo *ApplicationReputationService::prlog = nullptr;
#define LOG(args) PR_LOG(ApplicationReputationService::prlog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(ApplicationReputationService::prlog, 4)
#else
#define LOG(args)
#define LOG_ENABLED() (false)
#endif

class PendingDBLookup;

// A single use class private to ApplicationReputationService encapsulating an
// nsIApplicationReputationQuery and an nsIApplicationReputationCallback. Once
// created by ApplicationReputationService, it is guaranteed to call mCallback.
// This class is private to ApplicationReputationService.
class PendingLookup MOZ_FINAL : public nsIStreamListener
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  // Constructor and destructor.
  PendingLookup(nsIApplicationReputationQuery* aQuery,
                nsIApplicationReputationCallback* aCallback);
  ~PendingLookup();

  // Start the lookup. The lookup may have 2 parts: local and remote. In the
  // local lookup, PendingDBLookups are created to query the local allow and
  // blocklists for various URIs associated with this downloaded file. In the
  // event that no results are found, a remote lookup is sent to the Application
  // Reputation server.
  nsresult StartLookup();

private:
  friend class PendingDBLookup;

  // Telemetry states.
  // Status of the remote response (valid or not).
  enum SERVER_RESPONSE_TYPES {
    SERVER_RESPONSE_VALID = 0,
    SERVER_RESPONSE_FAILED = 1,
    SERVER_RESPONSE_INVALID = 2,
  };

  // The query containing metadata about the downloaded file.
  nsCOMPtr<nsIApplicationReputationQuery> mQuery;

  // The callback with which to report the verdict.
  nsCOMPtr<nsIApplicationReputationCallback> mCallback;

  // An array of strings created from certificate information used to whitelist
  // the downloaded file.
  nsTArray<nsCString> mAllowlistSpecs;
  // The source URI of the download, the referrer and possibly any redirects.
  nsTArray<nsCString> mAnylistSpecs;

  // When we started this query
  TimeStamp mStartTime;

  // The protocol buffer used to store signature information extracted using
  // the Windows Authenticode API, if the binary is signed.
  ClientDownloadRequest_SignatureInfo mSignatureInfo;

  // The response from the application reputation query. This is read in chunks
  // as part of our nsIStreamListener implementation and may contain embedded
  // NULLs.
  nsCString mResponse;

  // Clean up and call the callback. PendingLookup must not be used after this
  // function is called.
  nsresult OnComplete(bool shouldBlock, nsresult rv);

  // Wrapper function for nsIStreamListener.onStopRequest to make it easy to
  // guarantee calling the callback
  nsresult OnStopRequestInternal(nsIRequest *aRequest,
                                 nsISupports *aContext,
                                 nsresult aResult,
                                 bool* aShouldBlock);

  // Escape '/' and '%' in certificate attribute values.
  nsCString EscapeCertificateAttribute(const nsACString& aAttribute);

  // Escape ':' in fingerprint values.
  nsCString EscapeFingerprint(const nsACString& aAttribute);

  // Generate whitelist strings for the given certificate pair from the same
  // certificate chain.
  nsresult GenerateWhitelistStringsForPair(
    nsIX509Cert* certificate, nsIX509Cert* issuer);

  // Generate whitelist strings for the given certificate chain, which starts
  // with the signer and may go all the way to the root cert.
  nsresult GenerateWhitelistStringsForChain(
    const ClientDownloadRequest_CertificateChain& aChain);

  // For signed binaries, generate strings of the form:
  // http://sb-ssl.google.com/safebrowsing/csd/certificate/
  //   <issuer_cert_sha1_fingerprint>[/CN=<cn>][/O=<org>][/OU=<unit>]
  // for each (cert, issuer) pair in each chain of certificates that is
  // associated with the binary.
  nsresult GenerateWhitelistStrings(
    const ClientDownloadRequest_SignatureInfo& aSignatureInfo);

  // Parse the XPCOM certificate lists and stick them into the protocol buffer
  // version.
  nsresult ParseCertificates(nsIArray* aSigArray,
                             ClientDownloadRequest_SignatureInfo* aSigInfo);

  // Helper function to ensure that we call PendingLookup::LookupNext or
  // PendingLookup::OnComplete.
  nsresult DoLookupInternal();

  // Looks up all the URIs that may be responsible for allowlisting or
  // blocklisting the downloaded file. These URIs may include whitelist strings
  // generated by certificates verifying the binary as well as the target URI
  // from which the file was downloaded.
  nsresult LookupNext();

  // Sends a query to the remote application reputation service. Returns NS_OK
  // on success.
  nsresult SendRemoteQuery();

  // Helper function to ensure that we always call the callback.
  nsresult SendRemoteQueryInternal();
};

// A single-use class for looking up a single URI in the safebrowsing DB. This
// class is private to PendingLookup.
class PendingDBLookup MOZ_FINAL : public nsIUrlClassifierCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIURLCLASSIFIERCALLBACK

  // Constructor and destructor
  PendingDBLookup(PendingLookup* aPendingLookup);
  ~PendingDBLookup();

  // Look up the given URI in the safebrowsing DBs, optionally on both the allow
  // list and the blocklist. If there is a match, call
  // PendingLookup::OnComplete. Otherwise, call PendingLookup::LookupNext.
  nsresult LookupSpec(const nsACString& aSpec, bool aAllowlistOnly);
private:
  // The download appeared on the allowlist, blocklist, or no list (and thus
  // could trigger a remote query.
  enum LIST_TYPES {
    ALLOW_LIST = 0,
    BLOCK_LIST = 1,
    NO_LIST = 2,
  };

  nsCString mSpec;
  bool mAllowlistOnly;
  nsRefPtr<PendingLookup> mPendingLookup;
  nsresult LookupSpecInternal(const nsACString& aSpec);
};

NS_IMPL_ISUPPORTS1(PendingDBLookup,
                   nsIUrlClassifierCallback)

PendingDBLookup::PendingDBLookup(PendingLookup* aPendingLookup) :
  mAllowlistOnly(false),
  mPendingLookup(aPendingLookup)
{
  LOG(("Created pending DB lookup [this = %p]", this));
}

PendingDBLookup::~PendingDBLookup()
{
  LOG(("Destroying pending DB lookup [this = %p]", this));
  mPendingLookup = nullptr;
}

nsresult
PendingDBLookup::LookupSpec(const nsACString& aSpec,
                            bool aAllowlistOnly)
{
  LOG(("Checking principal %s", aSpec.Data()));
  mSpec = aSpec;
  mAllowlistOnly = aAllowlistOnly;
  nsresult rv = LookupSpecInternal(aSpec);
  if (NS_FAILED(rv)) {
    LOG(("Error in LookupSpecInternal"));
    return mPendingLookup->OnComplete(false, NS_OK);
  }
  // LookupSpecInternal has called nsIUrlClassifierCallback.lookup, which is
  // guaranteed to call HandleEvent.
  return rv;
}

nsresult
PendingDBLookup::LookupSpecInternal(const nsACString& aSpec)
{
  nsresult rv;

  nsCOMPtr<nsIURI> uri;
  nsCOMPtr<nsIIOService> ios = do_GetService(NS_IOSERVICE_CONTRACTID, &rv);
  rv = ios->NewURI(aSpec, nullptr, nullptr, getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> principal;
  nsCOMPtr<nsIScriptSecurityManager> secMan =
    do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = secMan->GetNoAppCodebasePrincipal(uri, getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, rv);

  // Check local lists to see if the URI has already been whitelisted or
  // blacklisted.
  LOG(("Checking DB service for principal %s [this = %p]", mSpec.get(), this));
  nsCOMPtr<nsIUrlClassifierDBService> dbService =
    do_GetService(NS_URLCLASSIFIERDBSERVICE_CONTRACTID, &rv);
  nsAutoCString tables;
  nsAutoCString allowlist;
  Preferences::GetCString(PREF_DOWNLOAD_ALLOW_TABLE, &allowlist);
  if (!allowlist.IsEmpty()) {
    tables.Append(allowlist);
  }
  nsAutoCString blocklist;
  Preferences::GetCString(PREF_DOWNLOAD_BLOCK_TABLE, &blocklist);
  if (!mAllowlistOnly && !blocklist.IsEmpty()) {
    tables.Append(",");
    tables.Append(blocklist);
  }
  return dbService->Lookup(principal, tables, this);
}

NS_IMETHODIMP
PendingDBLookup::HandleEvent(const nsACString& tables)
{
  // HandleEvent is guaranteed to call either:
  // 1) PendingLookup::OnComplete if the URL can be classified locally, or
  // 2) PendingLookup::LookupNext if the URL can be cannot classified locally.
  // Blocklisting trumps allowlisting.
  nsAutoCString blockList;
  Preferences::GetCString(PREF_DOWNLOAD_BLOCK_TABLE, &blockList);
  if (!mAllowlistOnly && FindInReadable(tables, blockList)) {
    Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_LOCAL, BLOCK_LIST);
    LOG(("Found principal %s on blocklist [this = %p]", mSpec.get(), this));
    return mPendingLookup->OnComplete(true, NS_OK);
  }

  nsAutoCString allowList;
  Preferences::GetCString(PREF_DOWNLOAD_ALLOW_TABLE, &allowList);
  if (FindInReadable(tables, allowList)) {
    Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_LOCAL, ALLOW_LIST);
    LOG(("Found principal %s on allowlist [this = %p]", mSpec.get(), this));
    return mPendingLookup->OnComplete(false, NS_OK);
  }

  LOG(("Didn't find principal %s on any list [this = %p]", mSpec.get(), this));
  Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_LOCAL, NO_LIST);
  return mPendingLookup->LookupNext();
}

NS_IMPL_ISUPPORTS2(PendingLookup,
                   nsIStreamListener,
                   nsIRequestObserver)

PendingLookup::PendingLookup(nsIApplicationReputationQuery* aQuery,
                             nsIApplicationReputationCallback* aCallback) :
  mQuery(aQuery),
  mCallback(aCallback)
{
  LOG(("Created pending lookup [this = %p]", this));
}

PendingLookup::~PendingLookup()
{
  LOG(("Destroying pending lookup [this = %p]", this));
}

nsresult
PendingLookup::LookupNext()
{
  // We must call LookupNext or SendRemoteQuery upon return.
  // Look up all of the URLs that could whitelist this download.
  // Blacklist first.
  int index = mAnylistSpecs.Length() - 1;
  nsCString spec;
  bool allowlistOnly = false;
  if (index >= 0) {
    // Check the source URI and referrer.
    spec = mAnylistSpecs[index];
    mAnylistSpecs.RemoveElementAt(index);
  } else {
    // Check the allowlists next.
    index = mAllowlistSpecs.Length() - 1;
    if (index >= 0) {
      allowlistOnly = true;
      spec = mAllowlistSpecs[index];
      mAllowlistSpecs.RemoveElementAt(index);
    }
  }
  if (index >= 0) {
    nsRefPtr<PendingDBLookup> lookup(new PendingDBLookup(this));
    return lookup->LookupSpec(spec, allowlistOnly);
  }
  // There are no more URIs to check against local list, so send the remote
  // query if we can.
  // Revert to just ifdef XP_WIN when remote lookups are enabled (bug 933432)
#if 0
  nsresult rv = SendRemoteQuery();
  if (NS_FAILED(rv)) {
    return OnComplete(false, rv);
  }
  return NS_OK;
#else
  return OnComplete(false, NS_OK);
#endif
}

nsCString
PendingLookup::EscapeCertificateAttribute(const nsACString& aAttribute)
{
  // Escape '/' because it's a field separator, and '%' because Chrome does
  nsCString escaped;
  escaped.SetCapacity(aAttribute.Length());
  for (unsigned int i = 0; i < aAttribute.Length(); ++i) {
    if (aAttribute.Data()[i] == '%') {
      escaped.Append("%25");
    } else if (aAttribute.Data()[i] == '/') {
      escaped.Append("%2F");
    } else if (aAttribute.Data()[i] == ' ') {
      escaped.Append("%20");
    } else {
      escaped.Append(aAttribute.Data()[i]);
    }
  }
  return escaped;
}

nsCString
PendingLookup::EscapeFingerprint(const nsACString& aFingerprint)
{
  // Google's fingerprint doesn't have colons
  nsCString escaped;
  escaped.SetCapacity(aFingerprint.Length());
  for (unsigned int i = 0; i < aFingerprint.Length(); ++i) {
    if (aFingerprint.Data()[i] != ':') {
      escaped.Append(aFingerprint.Data()[i]);
    }
  }
  return escaped;
}

nsresult
PendingLookup::GenerateWhitelistStringsForPair(
  nsIX509Cert* certificate,
  nsIX509Cert* issuer)
{
  // The whitelist paths have format:
  // http://sb-ssl.google.com/safebrowsing/csd/certificate/<issuer_cert_fingerprint>[/CN=<cn>][/O=<org>][/OU=<unit>]
  // Any of CN, O, or OU may be omitted from the whitelist entry. Unfortunately
  // this is not publicly documented, but the Chrome implementation can be found
  // here:
  // https://code.google.com/p/chromium/codesearch#search/&q=GetCertificateWhitelistStrings
  nsCString whitelistString(
    "http://sb-ssl.google.com/safebrowsing/csd/certificate/");

  nsString fingerprint;
  nsresult rv = issuer->GetSha1Fingerprint(fingerprint);
  NS_ENSURE_SUCCESS(rv, rv);
  whitelistString.Append(
    EscapeFingerprint(NS_ConvertUTF16toUTF8(fingerprint)));

  nsString commonName;
  rv = certificate->GetCommonName(commonName);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!commonName.IsEmpty()) {
    whitelistString.Append("/CN=");
    whitelistString.Append(
      EscapeCertificateAttribute(NS_ConvertUTF16toUTF8(commonName)));
  }

  nsString organization;
  rv = certificate->GetOrganization(organization);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!organization.IsEmpty()) {
    whitelistString.Append("/O=");
    whitelistString.Append(
      EscapeCertificateAttribute(NS_ConvertUTF16toUTF8(organization)));
  }

  nsString organizationalUnit;
  rv = certificate->GetOrganizationalUnit(organizationalUnit);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!organizationalUnit.IsEmpty()) {
    whitelistString.Append("/OU=");
    whitelistString.Append(
      EscapeCertificateAttribute(NS_ConvertUTF16toUTF8(organizationalUnit)));
  }
  LOG(("Whitelisting %s", whitelistString.get()));

  mAllowlistSpecs.AppendElement(whitelistString);
  return NS_OK;
}

nsresult
PendingLookup::GenerateWhitelistStringsForChain(
  const safe_browsing::ClientDownloadRequest_CertificateChain& aChain)
{
  // We need a signing certificate and an issuer to construct a whitelist
  // entry.
  if (aChain.element_size() < 2) {
    return NS_OK;
  }

  // Get the signer.
  nsresult rv;
  nsCOMPtr<nsIX509CertDB> certDB = do_GetService(NS_X509CERTDB_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIX509Cert> signer;
  rv = certDB->ConstructX509(
    const_cast<char *>(aChain.element(0).certificate().data()),
    aChain.element(0).certificate().size(), getter_AddRefs(signer));
  NS_ENSURE_SUCCESS(rv, rv);

  for (int i = 1; i < aChain.element_size(); ++i) {
    // Get the issuer.
    nsCOMPtr<nsIX509Cert> issuer;
    rv = certDB->ConstructX509(
      const_cast<char *>(aChain.element(i).certificate().data()),
      aChain.element(i).certificate().size(), getter_AddRefs(issuer));
    NS_ENSURE_SUCCESS(rv, rv);

    nsresult rv = GenerateWhitelistStringsForPair(signer, issuer);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult
PendingLookup::GenerateWhitelistStrings(
  const safe_browsing::ClientDownloadRequest_SignatureInfo& aSignatureInfo)
{
  for (int i = 0; i < aSignatureInfo.certificate_chain_size(); ++i) {
    nsresult rv = GenerateWhitelistStringsForChain(
      aSignatureInfo.certificate_chain(i));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult
PendingLookup::StartLookup()
{
  mStartTime = TimeStamp::Now();
  nsresult rv = DoLookupInternal();
  if (NS_FAILED(rv)) {
    return OnComplete(false, NS_OK);
  };
  return rv;
}

nsresult
PendingLookup::DoLookupInternal()
{
  // We want to check the target URI against the local lists.
  nsCOMPtr<nsIURI> uri;
  nsresult rv = mQuery->GetSourceURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString spec;
  rv = uri->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);
  mAnylistSpecs.AppendElement(spec);

  nsCOMPtr<nsIURI> referrer = nullptr;
  rv = mQuery->GetReferrerURI(getter_AddRefs(referrer));
  if (referrer) {
    nsCString spec;
    rv = referrer->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);
    mAnylistSpecs.AppendElement(spec);
  }

  // Extract the signature and parse certificates so we can use it to check
  // whitelists.
  nsCOMPtr<nsIArray> sigArray;
  rv = mQuery->GetSignatureInfo(getter_AddRefs(sigArray));
  NS_ENSURE_SUCCESS(rv, rv);

  if (sigArray) {
    rv = ParseCertificates(sigArray, &mSignatureInfo);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = GenerateWhitelistStrings(mSignatureInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  // Start the call chain.
  return LookupNext();
}

nsresult
PendingLookup::OnComplete(bool shouldBlock, nsresult rv)
{
  Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_SHOULD_BLOCK,
    shouldBlock);
#if defined(PR_LOGGING)
  double t = (TimeStamp::Now() - mStartTime).ToMilliseconds();
#endif
  if (shouldBlock) {
    LOG(("Application Reputation check failed, blocking bad binary in %f ms "
         "[this = %p]", t, this));
  } else {
    LOG(("Application Reputation check passed in %f ms [this = %p]", t, this));
  }
  nsresult res = mCallback->OnComplete(shouldBlock, rv);
  return res;
}

nsresult
PendingLookup::ParseCertificates(
  nsIArray* aSigArray,
  ClientDownloadRequest_SignatureInfo* aSignatureInfo)
{
  // If we haven't been set for any reason, bail.
  NS_ENSURE_ARG_POINTER(aSigArray);

  // Binaries may be signed by multiple chains of certificates. If there are no
  // chains, the binary is unsigned (or we were unable to extract signature
  // information on a non-Windows platform)
  nsCOMPtr<nsISimpleEnumerator> chains;
  nsresult rv = aSigArray->Enumerate(getter_AddRefs(chains));
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasMoreChains = false;
  rv = chains->HasMoreElements(&hasMoreChains);
  NS_ENSURE_SUCCESS(rv, rv);

  while (hasMoreChains) {
    nsCOMPtr<nsISupports> supports;
    rv = chains->GetNext(getter_AddRefs(supports));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIX509CertList> certList = do_QueryInterface(supports, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    safe_browsing::ClientDownloadRequest_CertificateChain* certChain =
      aSignatureInfo->add_certificate_chain();
    nsCOMPtr<nsISimpleEnumerator> chainElt;
    rv = certList->GetEnumerator(getter_AddRefs(chainElt));
    NS_ENSURE_SUCCESS(rv, rv);

    // Each chain may have multiple certificates.
    bool hasMoreCerts = false;
    rv = chainElt->HasMoreElements(&hasMoreCerts);
    while (hasMoreCerts) {
      nsCOMPtr<nsISupports> supports;
      rv = chainElt->GetNext(getter_AddRefs(supports));
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIX509Cert> cert = do_QueryInterface(supports, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      uint8_t* data = nullptr;
      uint32_t len = 0;
      rv = cert->GetRawDER(&len, &data);
      NS_ENSURE_SUCCESS(rv, rv);

      // Add this certificate to the protobuf to send remotely.
      certChain->add_element()->set_certificate(data, len);
      nsMemory::Free(data);

      rv = chainElt->HasMoreElements(&hasMoreCerts);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    rv = chains->HasMoreElements(&hasMoreChains);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (aSignatureInfo->certificate_chain_size() > 0) {
    aSignatureInfo->set_trusted(true);
  }
  return NS_OK;
}

nsresult
PendingLookup::SendRemoteQuery()
{
  nsresult rv = SendRemoteQueryInternal();
  if (NS_FAILED(rv)) {
    return OnComplete(false, NS_OK);
  }
  // SendRemoteQueryInternal has fired off the query and we call OnComplete in
  // the nsIStreamListener.onStopRequest.
  return rv;
}

nsresult
PendingLookup::SendRemoteQueryInternal()
{
  LOG(("Sending remote query for application reputation [this = %p]", this));
  // We did not find a local result, so fire off the query to the application
  // reputation service.
  safe_browsing::ClientDownloadRequest req;
  nsCOMPtr<nsIURI> uri;
  nsresult rv;
  rv = mQuery->GetSourceURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString spec;
  rv = uri->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);
  req.set_url(spec.get());

  uint32_t fileSize;
  rv = mQuery->GetFileSize(&fileSize);
  NS_ENSURE_SUCCESS(rv, rv);
  req.set_length(fileSize);
  // We have no way of knowing whether or not a user initiated the download.
  req.set_user_initiated(false);

  nsCString locale;
  NS_ENSURE_SUCCESS(Preferences::GetCString(PREF_GENERAL_LOCALE, &locale),
                    NS_ERROR_NOT_AVAILABLE);
  req.set_locale(locale.get());
  nsCString sha256Hash;
  rv = mQuery->GetSha256Hash(sha256Hash);
  NS_ENSURE_SUCCESS(rv, rv);
  req.mutable_digests()->set_sha256(sha256Hash.Data());
  nsString fileName;
  rv = mQuery->GetSuggestedFileName(fileName);
  NS_ENSURE_SUCCESS(rv, rv);
  req.set_file_basename(NS_ConvertUTF16toUTF8(fileName).get());
  req.mutable_signature()->CopyFrom(mSignatureInfo);

  if (req.signature().trusted()) {
    LOG(("Got signed binary for remote application reputation check "
         "[this = %p]", this));
  } else {
    LOG(("Got unsigned binary for remote application reputation check "
         "[this = %p]", this));
  }

  // Serialize the protocol buffer to a string. This can only fail if we are
  // out of memory, or if the protocol buffer req is missing required fields
  // (only the URL for now).
  std::string serialized;
  if (!req.SerializeToString(&serialized)) {
    return NS_ERROR_UNEXPECTED;
  }

  // Set the input stream to the serialized protocol buffer
  nsCOMPtr<nsIStringInputStream> sstream =
    do_CreateInstance("@mozilla.org/io/string-input-stream;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = sstream->SetData(serialized.c_str(), serialized.length());
  NS_ENSURE_SUCCESS(rv, rv);

  // Set up the channel to transmit the request to the service.
  nsCOMPtr<nsIChannel> channel;
  nsCString serviceUrl;
  NS_ENSURE_SUCCESS(Preferences::GetCString(PREF_SB_APP_REP_URL, &serviceUrl),
                    NS_ERROR_NOT_AVAILABLE);
  nsCOMPtr<nsIIOService> ios = do_GetService(NS_IOSERVICE_CONTRACTID, &rv);
  rv = ios->NewChannel(serviceUrl, nullptr, nullptr, getter_AddRefs(channel));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // Upload the protobuf to the application reputation service.
  nsCOMPtr<nsIUploadChannel2> uploadChannel = do_QueryInterface(channel, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = uploadChannel->ExplicitSetUploadStream(sstream,
    NS_LITERAL_CSTRING("application/octet-stream"), serialized.size(),
    NS_LITERAL_CSTRING("POST"), false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = channel->AsyncOpen(this, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsIStreamListener
static NS_METHOD
AppendSegmentToString(nsIInputStream* inputStream,
                      void *closure,
                      const char *rawSegment,
                      uint32_t toOffset,
                      uint32_t count,
                      uint32_t *writeCount) {
  nsAutoCString* decodedData = static_cast<nsAutoCString*>(closure);
  decodedData->Append(rawSegment, count);
  *writeCount = count;
  return NS_OK;
}

NS_IMETHODIMP
PendingLookup::OnDataAvailable(nsIRequest *aRequest,
                               nsISupports *aContext,
                               nsIInputStream *aStream,
                               uint64_t offset,
                               uint32_t count) {
  uint32_t read;
  return aStream->ReadSegments(AppendSegmentToString, &mResponse, count, &read);
}

NS_IMETHODIMP
PendingLookup::OnStartRequest(nsIRequest *aRequest,
                              nsISupports *aContext) {
  return NS_OK;
}

NS_IMETHODIMP
PendingLookup::OnStopRequest(nsIRequest *aRequest,
                             nsISupports *aContext,
                             nsresult aResult) {
  NS_ENSURE_STATE(mCallback);

  bool shouldBlock = false;
  nsresult rv = OnStopRequestInternal(aRequest, aContext, aResult,
                                      &shouldBlock);
  OnComplete(shouldBlock, rv);
  return rv;
}

nsresult
PendingLookup::OnStopRequestInternal(nsIRequest *aRequest,
                                     nsISupports *aContext,
                                     nsresult aResult,
                                     bool* aShouldBlock) {
  if (NS_FAILED(aResult)) {
    Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_SERVER,
      SERVER_RESPONSE_FAILED);
    return aResult;
  }

  *aShouldBlock = false;
  nsresult rv;
  nsCOMPtr<nsIHttpChannel> channel = do_QueryInterface(aRequest, &rv);
  if (NS_FAILED(rv)) {
    Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_SERVER,
      SERVER_RESPONSE_FAILED);
    return rv;
  }

  uint32_t status = 0;
  rv = channel->GetResponseStatus(&status);
  if (NS_FAILED(rv)) {
    Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_SERVER,
      SERVER_RESPONSE_FAILED);
    return rv;
  }

  if (status != 200) {
    Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_SERVER,
      SERVER_RESPONSE_FAILED);
    return NS_ERROR_NOT_AVAILABLE;
  }

  std::string buf(mResponse.Data(), mResponse.Length());
  safe_browsing::ClientDownloadResponse response;
  if (!response.ParseFromString(buf)) {
    NS_WARNING("Could not parse protocol buffer");
    Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_SERVER,
                                   SERVER_RESPONSE_INVALID);
    return NS_ERROR_CANNOT_CONVERT_DATA;
  }

  // There are several more verdicts, but we only respect one for now and treat
  // everything else as SAFE.
  Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_SERVER,
    SERVER_RESPONSE_VALID);
  if (response.verdict() == safe_browsing::ClientDownloadResponse::DANGEROUS) {
    *aShouldBlock = true;
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS1(ApplicationReputationService,
                   nsIApplicationReputationService)

ApplicationReputationService*
  ApplicationReputationService::gApplicationReputationService = nullptr;

ApplicationReputationService*
ApplicationReputationService::GetSingleton()
{
  if (gApplicationReputationService) {
    NS_ADDREF(gApplicationReputationService);
    return gApplicationReputationService;
  }

  // We're not initialized yet.
  gApplicationReputationService = new ApplicationReputationService();
  if (gApplicationReputationService) {
    NS_ADDREF(gApplicationReputationService);
  }

  return gApplicationReputationService;
}

ApplicationReputationService::ApplicationReputationService()
{
#if defined(PR_LOGGING)
  if (!prlog) {
    prlog = PR_NewLogModule("ApplicationReputation");
  }
#endif
  LOG(("Application reputation service started up"));
}

ApplicationReputationService::~ApplicationReputationService() {
  LOG(("Application reputation service shutting down"));
}

NS_IMETHODIMP
ApplicationReputationService::QueryReputation(
    nsIApplicationReputationQuery* aQuery,
    nsIApplicationReputationCallback* aCallback) {
  NS_ENSURE_ARG_POINTER(aQuery);
  NS_ENSURE_ARG_POINTER(aCallback);

  Accumulate(mozilla::Telemetry::APPLICATION_REPUTATION_COUNT, true);
  nsresult rv = QueryReputationInternal(aQuery, aCallback);
  if (NS_FAILED(rv)) {
    aCallback->OnComplete(false, rv);
  }
  return NS_OK;
}

nsresult ApplicationReputationService::QueryReputationInternal(
  nsIApplicationReputationQuery* aQuery,
  nsIApplicationReputationCallback* aCallback) {
  nsresult rv;
  // If malware checks aren't enabled, don't query application reputation.
  if (!Preferences::GetBool(PREF_SB_MALWARE_ENABLED, false)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // If there is no service URL for querying application reputation, abort.
  nsCString serviceUrl;
  NS_ENSURE_SUCCESS(Preferences::GetCString(PREF_SB_APP_REP_URL, &serviceUrl),
                    NS_ERROR_NOT_AVAILABLE);
  if (serviceUrl.EqualsLiteral("")) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIURI> uri;
  rv = aQuery->GetSourceURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  // Bail if the URI hasn't been set.
  NS_ENSURE_STATE(uri);

  // Create a new pending lookup and start the call chain.
  nsRefPtr<PendingLookup> lookup(new PendingLookup(aQuery, aCallback));
  NS_ENSURE_STATE(lookup);

  return lookup->StartLookup();
}
