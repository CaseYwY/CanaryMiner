#include "pearl_tls_engine.h"
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
#include <algorithm>
#include <array>
#include <ctime>
#include <vector>
#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#if OPENSSL_VERSION_NUMBER < 0x30500000L
#error Native pool TLS development requires OpenSSL 3.5 or newer
#endif
namespace om {
namespace {
using K=PearlTlsEngineStatus;using E=PearlTlsError;
constexpr std::size_t bufferLimit=65536;
constexpr long ocspClockSkewSeconds = 300;
constexpr long ocspWithNextUpdateMaxAgeSeconds = 10 * 86400;
constexpr long ocspWithoutNextUpdateMaxAgeSeconds = 86400;
constexpr int maximumOcspEntries = 64;
constexpr int maximumTlsFeatures = 64;
bool dnsName(const std::string &name){
    if(name.empty()||name.size()>253||name.back()=='.')return false;
    std::size_t begin=0;
    for(std::size_t i=0;i<=name.size();++i){
        if(i==name.size()||name[i]=='.'){
            if(i==begin||i-begin>63||name[begin]=='-'||name[i-1]=='-')return false;
            begin=i+1;
        }else{const auto c=static_cast<unsigned char>(name[i]);
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return false;}
    }
    return true;
}

bool readStaplingFeature(X509 *certificate, bool &required) {
    required = false;
    int critical = -1;
    const std::unique_ptr<TLS_FEATURE, decltype(&TLS_FEATURE_free)> features(
        static_cast<TLS_FEATURE *>(X509_get_ext_d2i(certificate, NID_tlsfeature, &critical, nullptr)), TLS_FEATURE_free);
    if (!features) return critical == -1;  // Absent extension, as opposed to an invalid or duplicated one.
    const int count = sk_ASN1_INTEGER_num(features.get());
    if (count < 1 || count > maximumTlsFeatures) return false;
    auto *original = X509_get_ext(certificate, X509_get_ext_by_NID(certificate, NID_tlsfeature, -1));
    const std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> canonical(
        X509V3_EXT_i2d(NID_tlsfeature, critical, features.get()), X509_EXTENSION_free);
    if (!original || !canonical ||
        ASN1_OCTET_STRING_cmp(X509_EXTENSION_get_data(original), X509_EXTENSION_get_data(canonical.get())) != 0)
        return false;
    for (int i = 0; i < count; ++i) {
        int64_t feature = 0;
        if (!ASN1_INTEGER_get_int64(&feature, sk_ASN1_INTEGER_value(features.get(), i)) ||
            feature != TLSEXT_TYPE_status_request) return false;
    }
    required = true;
    return true;
}

const EVP_MD *ocspIdentityDigest(OCSP_CERTID *identity) {
    ASN1_OBJECT *algorithm = nullptr;
    if (!OCSP_id_get0_info(nullptr, &algorithm, nullptr, nullptr, identity) || !algorithm) return nullptr;
    switch (OBJ_obj2nid(algorithm)) {
        case NID_sha1: return EVP_sha1();
        case NID_sha224: return EVP_sha224();
        case NID_sha256: return EVP_sha256();
        case NID_sha384: return EVP_sha384();
        case NID_sha512: return EVP_sha512();
        default: return nullptr;
    }
}

bool exactOcspEncoding(OCSP_BASICRESP *basic, const unsigned char *encoded, long size) {
    // OpenSSL's decoder for the embedded BasicOCSPResponse does not check for trailing bytes.
    const std::unique_ptr<OCSP_RESPONSE, decltype(&OCSP_RESPONSE_free)> reconstructed(
        OCSP_response_create(OCSP_RESPONSE_STATUS_SUCCESSFUL, basic), OCSP_RESPONSE_free);
    if (!reconstructed || i2d_OCSP_RESPONSE(reconstructed.get(), nullptr) != size) return false;
    std::vector<unsigned char> canonical(static_cast<std::size_t>(size));
    auto *cursor = canonical.data();
    return i2d_OCSP_RESPONSE(reconstructed.get(), &cursor) == size &&
        std::equal(canonical.begin(), canonical.end(), encoded);
}

bool verifyOcspStaple(SSL *ssl, X509_STORE *trust) {
    auto *chain = SSL_get0_verified_chain(ssl);
    const int chainLength = sk_X509_num(chain);
    auto *leaf = sk_X509_value(chain, 0);
    bool required = false;
    if (!leaf || !readStaplingFeature(leaf, required)) return false;
    // RFC 7633 requires each issued certificate to retain its signer's declared features.
    bool childRequiresStaple = required;
    for (int i = 1; i < chainLength; ++i) {
        bool issuerRequiresStaple = false;
        if (!readStaplingFeature(sk_X509_value(chain, i), issuerRequiresStaple) ||
            (issuerRequiresStaple && !childRequiresStaple)) return false;
        childRequiresStaple = issuerRequiresStaple;
    }
    unsigned char *encoded = nullptr;
    const long size = SSL_get_tlsext_status_ocsp_resp(ssl, &encoded);
    if (size == -1 && !encoded) return !required;
    if (!encoded || size <= 0 || size > static_cast<long>(bufferLimit)) return false;

    const unsigned char *cursor = encoded;
    const std::unique_ptr<OCSP_RESPONSE, decltype(&OCSP_RESPONSE_free)> response(
        d2i_OCSP_RESPONSE(nullptr, &cursor, size), OCSP_RESPONSE_free);
    if (!response || cursor != encoded + size ||
        OCSP_response_status(response.get()) != OCSP_RESPONSE_STATUS_SUCCESSFUL) return false;
    const std::unique_ptr<OCSP_BASICRESP, decltype(&OCSP_BASICRESP_free)> basic(
        OCSP_response_get1_basic(response.get()), OCSP_BASICRESP_free);
    if (!basic) return false;
    const int responses = OCSP_resp_count(basic.get());
    if (responses < 1 || responses > maximumOcspEntries ||
        OCSP_BASICRESP_get_ext_by_critical(basic.get(), 1, -1) >= 0 ||
        !exactOcspEncoding(basic.get(), encoded, size)) return false;
    auto *issuer = sk_X509_value(chain, 1);
    if (!issuer) return false;

    OCSP_SINGLERESP *match = nullptr;
    for (int i = 0; i < responses; ++i) {
        auto *entry = OCSP_resp_get0(basic.get(), i);
        if (!entry) return false;
        // The legacy accessor reads the ID but takes a non-const pointer.
        auto *identity = const_cast<OCSP_CERTID *>(OCSP_SINGLERESP_get0_id(entry));
        const auto *digest = identity ? ocspIdentityDigest(identity) : nullptr;
        if (!digest) return false;
        const std::unique_ptr<OCSP_CERTID, decltype(&OCSP_CERTID_free)> expected(
            OCSP_cert_to_id(digest, leaf, issuer), OCSP_CERTID_free);
        if (!expected) return false;
        if (OCSP_id_cmp(identity, expected.get()) != 0) continue;
        if (match) return false;
        match = entry;
    }
    if (!match || OCSP_SINGLERESP_get_ext_by_critical(match, 1, -1) >= 0 ||
        OCSP_basic_verify(basic.get(), chain, trust, OCSP_NOEXPLICIT) != 1) return false;

    const auto *produced = OCSP_resp_get0_produced_at(basic.get());
    auto latest = std::time(nullptr);
    if (latest == static_cast<std::time_t>(-1) || !produced) return false;
    latest += ocspClockSkewSeconds;
    if (X509_cmp_time(produced, &latest) >= 0) return false;
    ASN1_GENERALIZEDTIME *thisUpdate = nullptr, *nextUpdate = nullptr;
    const int status = OCSP_single_get0_status(match, nullptr, nullptr, &thisUpdate, &nextUpdate);
    return status == V_OCSP_CERTSTATUS_GOOD && thisUpdate &&
        OCSP_check_validity(thisUpdate, nextUpdate, ocspClockSkewSeconds,
                            nextUpdate ? ocspWithNextUpdateMaxAgeSeconds : ocspWithoutNextUpdateMaxAgeSeconds) == 1;
}

struct OpenSslEngine final:PearlTlsEngine {
    SSL_CTX *ctx=nullptr;SSL *ssl=nullptr;BIO *network=nullptr;E failure=E::Protocol;
    ~OpenSslEngine()override{SSL_free(ssl);BIO_free(network);SSL_CTX_free(ctx);}
    bool roots(const std::string &pem){
        if(pem.empty())return SSL_CTX_set_default_verify_paths(ctx)==1;
        const std::string start="-----BEGIN CERTIFICATE-----",end="-----END CERTIFICATE-----";
        std::size_t pos=0;unsigned count=0;
        while(pos<pem.size()){
            while(pos<pem.size()&&(pem[pos]==' '||pem[pos]=='\n'||pem[pos]=='\r'||pem[pos]=='\t'))++pos;
            if(pos==pem.size())break;
            if(pem.compare(pos,start.size(),start)!=0||++count>64)return false;
            const auto finish=pem.find(end,pos+start.size());if(finish==pem.npos)return false;
            const auto size=finish+end.size()-pos;
            BIO *source=BIO_new_mem_buf(pem.data()+pos,static_cast<int>(size));if(!source)return false;
            X509 *cert=PEM_read_bio_X509(source,nullptr,nullptr,nullptr);BIO_free(source);if(!cert)return false;
            EVP_PKEY *key=X509_get_pubkey(cert);
            const bool anchor=key&&X509_check_ca(cert)>0&&X509_NAME_cmp(X509_get_subject_name(cert),X509_get_issuer_name(cert))==0&&X509_verify(cert,key)==1;
            EVP_PKEY_free(key);
            const bool ok=anchor&&X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx),cert)==1;X509_free(cert);if(!ok)return false;
            pos=finish+end.size();
        }
        return count!=0;
    }
    bool configure(const PearlTlsPolicy &p){
        ctx=SSL_CTX_new(TLS_client_method());if(!ctx)return false;
        SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,nullptr);SSL_CTX_set_verify_depth(ctx,10);
        SSL_CTX_set_session_cache_mode(ctx,SSL_SESS_CACHE_OFF);
        SSL_CTX_set_options(ctx,SSL_OP_NO_COMPRESSION|SSL_OP_NO_RENEGOTIATION);
        SSL_CTX_clear_mode(ctx,SSL_MODE_AUTO_RETRY|SSL_MODE_ENABLE_PARTIAL_WRITE|SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        SSL_CTX_set_max_cert_list(ctx,bufferLimit);
        if(SSL_CTX_set_min_proto_version(ctx,TLS1_2_VERSION)!=1||!roots(p.caPem))return false;
        ssl=SSL_new(ctx);if(!ssl||SSL_set_tlsext_status_type(ssl,TLSEXT_STATUSTYPE_ocsp)!=1)return false;
        std::array<unsigned char,16> address;const bool ip=inet_pton(AF_INET,p.serverName.c_str(),address.data())==1||inet_pton(AF_INET6,p.serverName.c_str(),address.data())==1;
        if(ip){if(X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl),p.serverName.c_str())!=1)return false;}
        else{if(!dnsName(p.serverName))return false;
            SSL_set_hostflags(ssl,X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS|X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
            if(SSL_set1_host(ssl,p.serverName.c_str())!=1||SSL_set_tlsext_host_name(ssl,p.serverName.c_str())!=1)return false;}
        BIO *internal=nullptr;if(BIO_new_bio_pair(&internal,bufferLimit,&network,bufferLimit)!=1)return false;
        SSL_set_bio(ssl,internal,internal);SSL_set_connect_state(ssl);return true;
    }
    PearlTlsEngineStep mapped(int e){
        if(e==SSL_ERROR_WANT_READ)return {K::NeedRead,0};
        if(e==SSL_ERROR_WANT_WRITE)return {K::NeedWrite,0};
        if(e==SSL_ERROR_ZERO_RETURN)return {K::Closed,0};
        failure=SSL_get_verify_result(ssl)!=X509_V_OK?E::Verification:E::Protocol;return {K::Error,0};
    }
    PearlTlsEngineStep handshake()override{
        ERR_clear_error();const int ret=SSL_do_handshake(ssl);const int err=SSL_get_error(ssl,ret);
        if(ret!=1)return mapped(err);
        if(SSL_get_verify_result(ssl)!=X509_V_OK){failure=E::Verification;return {K::Error,0};}
        if(!verifyOcspStaple(ssl,SSL_CTX_get_cert_store(ctx))){failure=E::Verification;return {K::Error,0};}
        return {K::Complete,0};
    }
    PearlTlsEngineStep send(const char *p,std::size_t size)override{
        std::size_t n=0;ERR_clear_error();const int ret=SSL_write_ex(ssl,p,size,&n);const int err=SSL_get_error(ssl,ret);
        return ret==1?PearlTlsEngineStep{K::Bytes,n}:mapped(err);
    }
    PearlTlsEngineStep receive(char *p,std::size_t maximum)override{
        std::size_t n=0;ERR_clear_error();const int ret=SSL_read_ex(ssl,p,maximum,&n);const int err=SSL_get_error(ssl,ret);
        return ret==1?PearlTlsEngineStep{K::Bytes,n}:mapped(err);
    }
    std::size_t inputCapacity()const override{return BIO_ctrl_get_write_guarantee(network);}
    bool input(const char *p,std::size_t n)override{return n<=bufferLimit&&BIO_write(network,p,static_cast<int>(n))==static_cast<int>(n);}
    bool endInput()override{return BIO_shutdown_wr(network)==1;}
    std::size_t outputSize()const override{return BIO_ctrl_pending(network);}
    std::size_t takeOutput(char *p,std::size_t maximum)override{const int n=BIO_read(network,p,static_cast<int>(maximum));return n>0?static_cast<std::size_t>(n):0;}
    bool retryBufferedInput()const override{return BIO_ctrl_pending(SSL_get_rbio(ssl))!=0||SSL_pending(ssl)>0;}
    bool bufferedRead()const override{return retryBufferedInput()||SSL_has_pending(ssl)!=0;}
    E error()const override{return failure;}
};
}
std::unique_ptr<PearlTlsEngine> createPearlOpenSslEngine(const PearlTlsPolicy &p,E *error){
    if(p.windowsPrivateTrust){*error=E::Unavailable;return {};}
    if(OpenSSL_version_num()<0x30500000L){*error=E::Unavailable;return {};}
    auto engine=std::make_unique<OpenSslEngine>();if(!engine->configure(p)){*error=E::InvalidPolicy;return {};}return engine;
}
} // namespace om
#else
namespace om {
std::unique_ptr<PearlTlsEngine> createPearlOpenSslEngine(const PearlTlsPolicy &,PearlTlsError *error){*error=PearlTlsError::Unavailable;return {};}
}
#endif
