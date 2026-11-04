#include "pearl_tls_engine.h"
#include "pearl_tls_private_trust.h"
#if defined(_WIN32) && defined(CANARY_POOL_TLS_SCHANNEL)
#define SECURITY_WIN32
#define SCHANNEL_USE_BLACKLISTS
#include <winternl.h>
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
namespace om {
namespace {
using K=PearlTlsEngineStatus;using E=PearlTlsError;
constexpr std::size_t limit=65536;
struct SchannelEngine final:PearlTlsEngine {
    const std::atomic<bool> &cancel;CredHandle credentials;CtxtHandle context;
    E failure=E::Protocol;std::wstring name;std::unique_ptr<PearlTlsPrivateTrustInput> trust;
    std::array<char,limit> incoming{},record{};std::size_t inputSize=0;
    std::vector<char> outgoing;std::size_t outputOffset=0;
    SecPkgContext_StreamSizes sizes{};SecBuffer reauth[4]{};bool reauthFirst=false;
    bool first=true,needMore=false,eof=false,authorized=false,localComplete=false,control=false;
    uint64_t now=0,controlDeadline=0;unsigned controlSteps=0;std::size_t controlTraffic=0;
    explicit SchannelEngine(const std::atomic<bool> &c):cancel(c){SecInvalidateHandle(&credentials);SecInvalidateHandle(&context);}
    ~SchannelEngine()override{if(SecIsValidHandle(&context))DeleteSecurityContext(&context);if(SecIsValidHandle(&credentials))FreeCredentialsHandle(&credentials);}
    PearlTlsEngineStep fail(E e){failure=e;authorized=false;return {K::Error,0};}
    bool configure(const PearlTlsPolicy &p){
        if(!p.caPem.empty()){failure=E::Unavailable;return false;}
        if(p.serverName.find(':')!=p.serverName.npos||p.serverName.find_first_not_of("0123456789.")==p.serverName.npos){failure=E::Unavailable;return false;}
        // Strict ASCII DNS reference, never a URL, socket address, peer name or credential-store discovery key.
        std::size_t begin=0;
        for(std::size_t i=0;i<=p.serverName.size();++i){if(i==p.serverName.size()||p.serverName[i]=='.'){
                if(i==begin||i-begin>63||p.serverName[begin]=='-'||p.serverName[i-1]=='-'){failure=E::InvalidPolicy;return false;}begin=i+1;
            }else{const auto c=static_cast<unsigned char>(p.serverName[i]);if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')){failure=E::InvalidPolicy;return false;}}}
        name.assign(p.serverName.begin(),p.serverName.end());
        if(p.windowsPrivateTrust){
#if defined(CANARY_POOL_TLS_PRIVATE_TRUST_WINDOWS)
            const auto &t=*p.windowsPrivateTrust;std::size_t total=65536;
            if(!t.enabled||!t.leaf.empty()||t.referenceDnsName!=p.serverName||t.anchors.empty()||t.anchors.size()>16||t.intermediates.size()>16||t.crls.size()>16){failure=E::InvalidPolicy;return false;}
            for(const auto *v:{&t.anchors,&t.intermediates,&t.crls})for(const auto &der:*v){if(der.empty()||der.size()>limit||(total+=der.size())>8*limit){failure=E::InvalidPolicy;return false;}}
            trust=std::make_unique<PearlTlsPrivateTrustInput>(t);
#else
            failure=E::Unavailable;return false;
#endif
        }
        TLS_PARAMETERS tls{};tls.grbitDisabledProtocols=SP_PROT_SSL2_CLIENT|SP_PROT_SSL3_CLIENT|SP_PROT_TLS1_CLIENT|SP_PROT_TLS1_1_CLIENT;
        SCH_CREDENTIALS cred{};cred.dwVersion=SCH_CREDENTIALS_VERSION;cred.cTlsParameters=1;cred.pTlsParameters=&tls;
        cred.dwFlags=SCH_CRED_NO_DEFAULT_CREDS|(trust?SCH_CRED_MANUAL_CRED_VALIDATION:SCH_CRED_AUTO_CRED_VALIDATION|SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT);
        TimeStamp expiry{};const auto r=AcquireCredentialsHandleW(nullptr,const_cast<wchar_t *>(UNISP_NAME_W),SECPKG_CRED_OUTBOUND,nullptr,&cred,nullptr,nullptr,&credentials,&expiry);
        if(r!=SEC_E_OK){failure=E::Unavailable;return false;}return true;
    }
    bool ownRange(const void *p,std::size_t n,std::size_t original)const{
        const auto base=reinterpret_cast<uintptr_t>(incoming.data()),value=reinterpret_cast<uintptr_t>(p);
        return value>=base&&value-base<=original&&n<=original-(value-base);
    }
    bool keepExtra(const SecBuffer *buffers,std::size_t count,std::size_t original){
        std::size_t extra=0;unsigned found=0;
        for(std::size_t i=0;i<count;++i)if((buffers[i].BufferType&~SECBUFFER_ATTRMASK)==SECBUFFER_EXTRA){if(++found>1||buffers[i].cbBuffer>original)return false;extra=buffers[i].cbBuffer;}
        if(extra)std::memmove(incoming.data(),incoming.data()+original-extra,extra);inputSize=extra;needMore=false;return true;
    }
    bool appendTokens(const SecBuffer *buffers,std::size_t count){
        bool ok=true;
        for(std::size_t i=0;i<count;++i){auto &b=buffers[i];
            if(b.cbBuffer){if(!b.pvBuffer||b.cbBuffer>limit-outgoing.size())ok=false;
                else if(ok){const auto *p=static_cast<const char *>(b.pvBuffer);outgoing.insert(outgoing.end(),p,p+b.cbBuffer);}}
        }
        return ok;
    }
    bool verifyPeer(){
        SecPkgContext_ConnectionInfo connection{};
        if(QueryContextAttributesW(&context,SECPKG_ATTR_CONNECTION_INFO,&connection)!=SEC_E_OK||
            !(connection.dwProtocol&(SP_PROT_TLS1_2_CLIENT|SP_PROT_TLS1_3_CLIENT)))return false;
        PCCERT_CONTEXT peer=nullptr;if(QueryContextAttributesW(&context,SECPKG_ATTR_REMOTE_CERT_CONTEXT,&peer)!=SEC_E_OK||!peer)return false;
        bool ok=false;
        try{
            const auto *san=CertFindExtension(szOID_SUBJECT_ALT_NAME2,peer->pCertInfo->cExtension,peer->pCertInfo->rgExtension);
            DWORD count=0;
            if(san&&CryptDecodeObjectEx(X509_ASN_ENCODING,X509_ALTERNATE_NAME,san->Value.pbData,san->Value.cbData,0,nullptr,nullptr,&count)&&count&&count<=limit){
                std::vector<unsigned char> decoded(count);
                if(CryptDecodeObjectEx(X509_ASN_ENCODING,X509_ALTERNATE_NAME,san->Value.pbData,san->Value.cbData,0,nullptr,decoded.data(),&count)){
                    const auto *alt=reinterpret_cast<const CERT_ALT_NAME_INFO *>(decoded.data());
                    if(alt->cAltEntry<=64)for(DWORD i=0;i<alt->cAltEntry;++i)if(alt->rgAltEntry[i].dwAltNameChoice==CERT_ALT_NAME_DNS_NAME&&alt->rgAltEntry[i].pwszDNSName)ok=true;
                }
            }
            if(ok&&trust){if(!peer->cbCertEncoded||peer->cbCertEncoded>limit)ok=false;
                else{auto observed=*trust;observed.leaf.assign(peer->pbCertEncoded,peer->pbCertEncoded+peer->cbCertEncoded);
                    ok=inspectPearlTlsPrivateTrust(observed,cancel).status==PearlTlsPrivateTrustStatus::Verified;}}
        }catch(...){CertFreeCertificateContext(peer);throw;}
        CertFreeCertificateContext(peer);return ok;
    }
    PearlTlsEngineStep handshake()override{
        if(localComplete)return {K::Complete,0};
        if(cancel.load())return fail(E::Cancelled);
        if(!first&&!inputSize&&!reauthFirst)return eof?fail(E::Protocol):PearlTlsEngineStep{K::NeedRead,0};
        SecBuffer input[2]{{static_cast<ULONG>(inputSize),SECBUFFER_TOKEN,incoming.data()},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc in{SECBUFFER_VERSION,2,input};
        if(reauthFirst){in.cBuffers=4;in.pBuffers=reauth;reauthFirst=false;}
        struct Tokens {SecBuffer buffers[2]{{0,SECBUFFER_TOKEN,nullptr},{0,SECBUFFER_ALERT,nullptr}};
            ~Tokens(){for(const auto &b:buffers)if(b.pvBuffer)FreeContextBuffer(b.pvBuffer);}} tokens;
        SecBufferDesc out{SECBUFFER_VERSION,2,tokens.buffers};
        const auto original=inputSize;ULONG attributes=0;TimeStamp expiry{};
        constexpr ULONG wanted=ISC_REQ_STREAM|ISC_REQ_CONFIDENTIALITY|ISC_REQ_ALLOCATE_MEMORY|ISC_REQ_USE_SUPPLIED_CREDS|ISC_REQ_EXTENDED_ERROR|ISC_REQ_REPLAY_DETECT|ISC_REQ_SEQUENCE_DETECT;
        const auto r=InitializeSecurityContextW(&credentials,first?nullptr:&context,name.data(),wanted,0,SECURITY_NATIVE_DREP,
            first?nullptr:&in,0,&context,&out,&attributes,&expiry);first=false;
        if(!appendTokens(tokens.buffers,2))return fail(E::Capacity);
        if(r==SEC_E_INCOMPLETE_MESSAGE){needMore=true;return eof?fail(E::Protocol):PearlTlsEngineStep{K::NeedRead,0};}
        if(r!=SEC_E_OK&&r!=SEC_I_CONTINUE_NEEDED)return fail(r==SEC_I_INCOMPLETE_CREDENTIALS?E::Unavailable:E::Verification);
        if(!keepExtra(in.pBuffers,in.cBuffers,original))return fail(E::Protocol);
        if(r==SEC_I_CONTINUE_NEEDED)return outputSize()?PearlTlsEngineStep{K::NeedWrite,0}:PearlTlsEngineStep{K::NeedRead,0};
        if((attributes&(ISC_RET_STREAM|ISC_RET_CONFIDENTIALITY))!=(ISC_RET_STREAM|ISC_RET_CONFIDENTIALITY))return fail(E::Verification);
        if(!verifyPeer())return fail(cancel.load()?E::Cancelled:E::Verification);
        if(QueryContextAttributesW(&context,SECPKG_ATTR_STREAM_SIZES,&sizes)!=SEC_E_OK||!sizes.cbMaximumMessage||
            static_cast<std::size_t>(sizes.cbHeader)+sizes.cbMaximumMessage+sizes.cbTrailer>limit)return fail(E::Unavailable);
        localComplete=true;return {K::Complete,0};
    }
    PearlTlsEngineStep controlStep(){const auto r=handshake();return r.status==K::Complete?PearlTlsEngineStep{K::Progress,0}:r;}
    PearlTlsEngineStep send(const char *data,std::size_t size)override{
        if(!authorized)return controlStep();
        if(outputSize()||!size||size>maximumPlaintext())return fail(E::Protocol);
        const auto header=static_cast<std::size_t>(sizes.cbHeader),trailer=static_cast<std::size_t>(sizes.cbTrailer);
        std::memcpy(record.data()+header,data,size);
        SecBuffer buffers[4]{{sizes.cbHeader,SECBUFFER_STREAM_HEADER,record.data()},{static_cast<ULONG>(size),SECBUFFER_DATA,record.data()+header},
            {sizes.cbTrailer,SECBUFFER_STREAM_TRAILER,record.data()+header+size},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc message{SECBUFFER_VERSION,4,buffers};if(EncryptMessage(&context,0,&message,0)!=SEC_E_OK)return fail(E::Protocol);
        for(unsigned i=0;i<3;++i){const auto &b=buffers[i];const auto base=reinterpret_cast<uintptr_t>(record.data()),value=reinterpret_cast<uintptr_t>(b.pvBuffer);
            if(value<base||value-base>header+size+trailer||b.cbBuffer>header+size+trailer-(value-base)||b.cbBuffer>limit-outgoing.size())return fail(E::Protocol);
            const auto *p=static_cast<const char *>(b.pvBuffer);outgoing.insert(outgoing.end(),p,p+b.cbBuffer);}
        return {K::Bytes,size};
    }
    PearlTlsEngineStep receive(char *out,std::size_t maximum)override{
        if(!authorized)return controlStep();
        if(!inputSize)return eof?fail(E::Protocol):PearlTlsEngineStep{K::NeedRead,0};
        const auto original=inputSize;SecBuffer buffers[4]{{static_cast<ULONG>(inputSize),SECBUFFER_DATA,incoming.data()},
            {0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc message{SECBUFFER_VERSION,4,buffers};ULONG quality=0;const auto r=DecryptMessage(&context,&message,0,&quality);
        if(r==SEC_E_INCOMPLETE_MESSAGE){needMore=true;return eof?fail(E::Protocol):PearlTlsEngineStep{K::NeedRead,0};}
        if(r==SEC_I_RENEGOTIATE){
            if(now>std::numeric_limits<uint64_t>::max()-10000)return fail(E::Clock);
            control=true;authorized=localComplete=false;controlDeadline=now+10000;controlSteps=0;controlTraffic=0;
            // The first descriptor may now be only STREAM_HEADER (five bytes), not the control token.
            // Prefer the returned unconsumed EXTRA tail; otherwise preserve an actual modified DATA/TOKEN
            // (including an empty control token). Never label a header/trailer alone as a whole token.
            const SecBuffer *token=nullptr;unsigned extras=0;
            for(const auto &b:buffers)if((b.BufferType&~SECBUFFER_ATTRMASK)==SECBUFFER_EXTRA){if(++extras>1||b.cbBuffer>original)return fail(E::Protocol);token=&b;}
            if(token){inputSize=token->cbBuffer;if(inputSize)std::memmove(incoming.data(),incoming.data()+original-inputSize,inputSize);}
            else{for(const auto &b:buffers){const auto type=b.BufferType&~SECBUFFER_ATTRMASK;
                    if((type==SECBUFFER_DATA||type==SECBUFFER_TOKEN)&&b.cbBuffer){if(token||!ownRange(b.pvBuffer,b.cbBuffer,original))return fail(E::Protocol);token=&b;}}
                if(token){inputSize=token->cbBuffer;std::memmove(incoming.data(),token->pvBuffer,inputSize);}else inputSize=0;}
            reauth[0]={static_cast<ULONG>(inputSize),SECBUFFER_TOKEN,incoming.data()};
            for(unsigned i=1;i<4;++i)reauth[i]={0,SECBUFFER_EMPTY,nullptr};
            reauthFirst=true;needMore=false;
            return {K::Progress,0};
        }
        if(r==SEC_I_CONTEXT_EXPIRED){
            // Schannel can leave the ORIGINAL encrypted DATA descriptor untouched on authenticated
            // close-notify. It is not plaintext. Only SEC_E_OK yields application DATA below.
            for(const auto &b:buffers)if((b.BufferType&~SECBUFFER_ATTRMASK)==SECBUFFER_EXTRA&&b.cbBuffer)return fail(E::Protocol);
            inputSize=0;needMore=false;return {K::Closed,0};
        }
        if(r!=SEC_E_OK)return fail(E::Protocol);
        std::size_t count=0;
        for(const auto &b:buffers)if((b.BufferType&~SECBUFFER_ATTRMASK)==SECBUFFER_DATA&&b.cbBuffer){
            if(!ownRange(b.pvBuffer,b.cbBuffer,original)||b.cbBuffer>maximum-count)return fail(E::Protocol);
            std::memcpy(out+count,b.pvBuffer,b.cbBuffer);count+=b.cbBuffer;}
        if(!keepExtra(buffers,4,original))return fail(E::Protocol);
        return count?PearlTlsEngineStep{K::Bytes,count}:PearlTlsEngineStep{K::Progress,0};
    }
    std::size_t inputCapacity()const override{return limit-inputSize;}
    bool input(const char *p,std::size_t n)override{if(n>inputCapacity()){failure=E::Capacity;return false;}
        if(control&&(controlTraffic+=n)>8*limit){failure=E::Capacity;return false;}
        std::memcpy(incoming.data()+inputSize,p,n);inputSize+=n;needMore=false;return true;}
    bool endInput()override{eof=true;return true;}
    std::size_t outputSize()const override{return outgoing.size()-outputOffset;}
    std::size_t takeOutput(char *p,std::size_t maximum)override{const auto n=std::min(maximum,outputSize());
        if(control&&(controlTraffic+=n)>8*limit){failure=E::Capacity;return 0;}
        std::memcpy(p,outgoing.data()+outputOffset,n);outputOffset+=n;if(outputOffset==outgoing.size()){outgoing.clear();outputOffset=0;}return n;}
    bool retryBufferedInput()const override{return (control&&localComplete)||((reauthFirst||inputSize)&&!needMore);}
    bool bufferedRead()const override{return control||inputSize||outputSize();}
    std::size_t maximumPlaintext()const override{return std::min<std::size_t>(16384,sizes.cbMaximumMessage);}
    bool verified()const override{return authorized;}
    bool tick(uint64_t observed)override{now=observed;
        if(control&&(now>=controlDeadline||++controlSteps>8192)){failure=now>=controlDeadline?E::Timeout:E::Capacity;return false;}return true;}
    void outputFlushed()override{if(localComplete&&!outputSize()){authorized=true;control=false;}}
    E error()const override{return failure;}
};
}
std::unique_ptr<PearlTlsEngine> createPearlSchannelEngine(const PearlTlsPolicy &p,const std::atomic<bool> &cancel,E *error){
    auto engine=std::make_unique<SchannelEngine>(cancel);if(!engine->configure(p)){*error=engine->error();return {};}return engine;
}
} // namespace om
#else
namespace om {std::unique_ptr<PearlTlsEngine> createPearlSchannelEngine(const PearlTlsPolicy &,const std::atomic<bool> &,PearlTlsError *error){*error=PearlTlsError::Unavailable;return {};}}
#endif
