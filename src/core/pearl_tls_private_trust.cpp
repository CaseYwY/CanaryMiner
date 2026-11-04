#include "pearl_tls_private_trust.h"
#include <algorithm>
#if defined(_WIN32) && defined(CANARY_POOL_TLS_PRIVATE_TRUST_WINDOWS)
#define CERT_CHAIN_PARA_HAS_EXTRA_FIELDS
#include <windows.h>
#include <wincrypt.h>
#endif
namespace om {
namespace {
using S=PearlTlsPrivateTrustStatus;
bool dns(const std::string &s){
    if(s.empty()||s.size()>253||s.back()=='.')return false;
    std::size_t start=0;
    for(std::size_t i=0;i<=s.size();++i){
        if(i==s.size()||s[i]=='.'){if(i==start||i-start>63||s[start]=='-'||s[i-1]=='-')return false;start=i+1;}
        else{const auto c=static_cast<unsigned char>(s[i]);if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}
    }
    return true;
}
bool bounded(const PearlTlsPrivateTrustInput &p){
    if(p.leaf.empty()||p.leaf.size()>65536||p.anchors.empty()||p.anchors.size()>16||p.intermediates.size()>16||p.crls.size()>16)return false;
    std::size_t total=p.leaf.size();
    for(const auto *v:{&p.anchors,&p.intermediates,&p.crls})for(const auto &der:*v){
        if(der.empty()||der.size()>65536)return false;
        total+=der.size();if(total>512*1024)return false;
    }
    return true;
}
#if defined(_WIN32) && defined(CANARY_POOL_TLS_PRIVATE_TRUST_WINDOWS)
struct Store {HCERTSTORE value=nullptr;~Store(){if(value)CertCloseStore(value,0);}
    bool open(){value=CertOpenStore(CERT_STORE_PROV_MEMORY,0,0,CERT_STORE_CREATE_NEW_FLAG,nullptr);return value!=nullptr;}};
struct Certificate {PCCERT_CONTEXT value=nullptr;~Certificate(){if(value)CertFreeCertificateContext(value);}};
struct Chain {PCCERT_CHAIN_CONTEXT value=nullptr;~Chain(){if(value)CertFreeCertificateChain(value);}};
struct Engine {HCERTCHAINENGINE value=nullptr;~Engine(){if(value)CertFreeCertificateChainEngine(value);}};
bool caRole(PCCERT_CONTEXT cert){
    const auto *ext=CertFindExtension(szOID_BASIC_CONSTRAINTS2,cert->pCertInfo->cExtension,cert->pCertInfo->rgExtension);
    if(!ext)return false;
    CERT_BASIC_CONSTRAINTS2_INFO info{};DWORD size=sizeof(info);
    if(!CryptDecodeObjectEx(X509_ASN_ENCODING,X509_BASIC_CONSTRAINTS2,ext->Value.pbData,ext->Value.cbData,0,nullptr,&info,&size)||!info.fCA)return false;
    BYTE usage=0;return CertGetIntendedKeyUsage(X509_ASN_ENCODING,cert->pCertInfo,&usage,sizeof(usage))&&(usage&CERT_KEY_CERT_SIGN_KEY_USAGE)!=0;
}
bool addCertificate(HCERTSTORE store,const PearlTlsDer &der,bool anchor){
    Certificate cert;cert.value=CertCreateCertificateContext(X509_ASN_ENCODING,der.data(),static_cast<DWORD>(der.size()));if(!cert.value)return false;
    if(anchor&&(!caRole(cert.value)||!CertCompareCertificateName(X509_ASN_ENCODING,&cert.value->pCertInfo->Subject,&cert.value->pCertInfo->Issuer)||
        !CryptVerifyCertificateSignatureEx(0,X509_ASN_ENCODING,CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,const_cast<PCERT_CONTEXT>(cert.value),
            CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT,const_cast<PCERT_CONTEXT>(cert.value),0,nullptr)))return false;
    return CertAddCertificateContextToStore(store,cert.value,CERT_STORE_ADD_NEW,nullptr)!=FALSE;
}
#endif
}
PearlTlsPrivateTrustResult inspectPearlTlsPrivateTrust(const PearlTlsPrivateTrustInput &p,const std::atomic<bool> &cancel)noexcept{
    PearlTlsPrivateTrustResult out;
    const auto result=[&](S s){out.status=s;return out;};
    try{
        if(!p.enabled)return result(S::Disabled);
        if(cancel.load())return result(S::Cancelled);
        if(!bounded(p))return result(S::InvalidInput);
        if(p.referenceDnsName.find(':')!=p.referenceDnsName.npos||(!p.referenceDnsName.empty()&&p.referenceDnsName.find_first_not_of("0123456789.")==p.referenceDnsName.npos))
            return result(S::UnsupportedReference);
        if(!dns(p.referenceDnsName))return result(S::InvalidInput);
#if defined(_WIN32) && defined(CANARY_POOL_TLS_PRIVATE_TRUST_WINDOWS)
        const auto nativeFailure=[&](PearlTlsPrivateTrustStage stage){out.nativeStage=stage;out.nativeError=GetLastError();return result(S::NativeFailure);};
        Store anchors,support;if(!anchors.open()||!support.open())return nativeFailure(PearlTlsPrivateTrustStage::Store);
        for(const auto &der:p.anchors)if(!addCertificate(anchors.value,der,true))return result(S::InvalidInput);
        for(const auto &der:p.intermediates)if(!addCertificate(support.value,der,false))return result(S::InvalidInput);
        for(const auto &der:p.crls)if(!CertAddEncodedCRLToStore(support.value,X509_ASN_ENCODING,der.data(),static_cast<DWORD>(der.size()),CERT_STORE_ADD_NEW,nullptr))return result(S::InvalidInput);
        Certificate leaf;leaf.value=CertCreateCertificateContext(X509_ASN_ENCODING,p.leaf.data(),static_cast<DWORD>(p.leaf.size()));
        if(!leaf.value)return result(S::InvalidInput);
        if(caRole(leaf.value))return result(S::Rejected); // root-as-leaf cannot prove non-root revocation
        Engine engine;CERT_CHAIN_ENGINE_CONFIG config{};config.cbSize=sizeof(config);config.hExclusiveRoot=anchors.value;
        // SDK: exclusive anchors and ALL restricted-store handles are mutually exclusive. Supporting
        // objects are additional input, never trust anchors. This is not a hermetic OS-store sandbox.
        config.cAdditionalStore=1;config.rghAdditionalStore=&support.value;
        config.dwFlags=CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL|CERT_CHAIN_DISABLE_AIA;config.MaximumCachedCertificates=64;
        if(!CertCreateCertificateChainEngine(&config,&engine.value)||!engine.value)return nativeFailure(PearlTlsPrivateTrustStage::Engine);
        CERT_STRONG_SIGN_PARA strong{};strong.cbSize=sizeof(strong);strong.dwInfoChoice=CERT_STRONG_SIGN_OID_INFO_CHOICE;strong.pszOID=szOID_CERT_STRONG_SIGN_OS_CURRENT;
        CERT_CHAIN_PARA parameters{};parameters.cbSize=sizeof(parameters);parameters.pStrongSignPara=&strong;
        LPSTR usage=const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);parameters.RequestedUsage.dwType=USAGE_MATCH_TYPE_AND;
        parameters.RequestedUsage.Usage.cUsageIdentifier=1;parameters.RequestedUsage.Usage.rgpszUsageIdentifier=&usage;
        constexpr DWORD flags=CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL|CERT_CHAIN_DISABLE_AIA|CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE|
            CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY|CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT|CERT_CHAIN_OPT_IN_WEAK_SIGNATURE;
        if(cancel.load())return result(S::Cancelled);
        Chain chain;if(!CertGetCertificateChain(engine.value,leaf.value,nullptr,support.value,&parameters,flags,nullptr,&chain.value)||!chain.value)return nativeFailure(PearlTlsPrivateTrustStage::Chain);
        out.chainEvaluated=true;out.chainErrors=chain.value->TrustStatus.dwErrorStatus;
        if(cancel.load())return result(S::Cancelled);
        if(out.chainErrors||!chain.value->cChain||!chain.value->rgpChain||!chain.value->rgpChain[0]||!chain.value->rgpChain[0]->cElement)return result(S::Rejected);
        std::wstring name(p.referenceDnsName.begin(),p.referenceDnsName.end());
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};ssl.cbSize=sizeof(ssl);ssl.dwAuthType=AUTHTYPE_SERVER;ssl.pwszServerName=name.data();
        CERT_CHAIN_POLICY_PARA policy{};policy.cbSize=sizeof(policy);policy.pvExtraPolicyPara=&ssl;
        CERT_CHAIN_POLICY_STATUS status{};status.cbSize=sizeof(status);
        if(!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL,chain.value,&policy,&status))return nativeFailure(PearlTlsPrivateTrustStage::SslPolicy);
        out.policyEvaluated=true;out.policyError=status.dwError;
        if(cancel.load())return result(S::Cancelled);
        return result(status.dwError?S::Rejected:S::Verified);
#else
        return result(S::Unavailable);
#endif
    }catch(...){return result(S::NativeFailure);}
}
} // namespace om
