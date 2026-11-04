#include "pearl_tls_stream.h"
#include <algorithm>
#include <array>
#include <cstring>
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#if OPENSSL_VERSION_NUMBER < 0x30500000L
#error Native pool TLS development requires OpenSSL 3.5 or newer
#endif
#endif
namespace om {
namespace {
using S=PearlTlsState;using E=PearlTlsError;using T=PearlPoolTransferStatus;using I=PearlPoolInterest;
constexpr std::size_t recordLimit=16384,bufferLimit=65536;
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
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
#endif
}
struct PearlTlsStream::Impl {
    PearlPoolTransport &wire;const std::atomic<bool> &cancel;
    S state=S::Dormant;E error=E::None;uint64_t last=0,deadline=0;bool attempted=false,adopted=false;
    explicit Impl(PearlPoolTransport &w,const std::atomic<bool> &c):wire(w),cancel(c){}
    PearlPoolTransfer fail(E reason)noexcept{
        if(state!=S::Failed){state=S::Failed;error=reason;}
        if(adopted){try{wire.close();}catch(...){}}return {T::Error,0};
    }
    bool check(uint64_t now)noexcept{
        if(state==S::Failed||state==S::Closed)return false;
        if(cancel.load()){fail(E::Cancelled);return false;}
        if(!now||now<last){fail(E::Clock);return false;}
        last=now;
        if(state==S::Handshaking&&now>=deadline){fail(E::Timeout);return false;}
        return true;
    }
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
    SSL_CTX *ctx=nullptr;SSL *ssl=nullptr;BIO *network=nullptr;
    bool handshakeDone=false,eof=false;unsigned handshakeSteps=0;std::size_t handshakeBytes=0;
    std::array<char,recordLimit> outgoing{},plain{},writeCopy{};
    std::size_t outputSize=0,outputOffset=0,plainSize=0,plainOffset=0,writeSize=0,writeAccepted=0;
    const char *writePointer=nullptr;std::size_t writeLength=0;
    ~Impl(){SSL_free(ssl);BIO_free(network);SSL_CTX_free(ctx);}
    bool outputPending()const{return outputSize!=outputOffset||(network&&BIO_ctrl_pending(network)!=0);}
    // A zero-byte Progress is a private "nothing remains" sentinel here; callers distinguish it by outputPending().
    PearlPoolTransfer flush(){
        if(cancel.load())return fail(E::Cancelled);
        if(outputSize==outputOffset){outputSize=outputOffset=0;
            if(!BIO_ctrl_pending(network))return {T::Progress,0};
            const int n=BIO_read(network,outgoing.data(),static_cast<int>(outgoing.size()));
            if(n<=0)return fail(E::Protocol);
            outputSize=static_cast<std::size_t>(n);
        }
        const auto remaining=state==S::Handshaking?8*bufferLimit-handshakeBytes:bufferLimit;
        if(!remaining)return fail(E::Capacity);
        const auto request=std::min(outputSize-outputOffset,remaining);
        const auto sent=wire.send(outgoing.data()+outputOffset,request);
        if(cancel.load())return fail(E::Cancelled);
        if(sent.status==T::Bytes){
            if(!sent.bytes||sent.bytes>request||sent.interest!=I::None)return fail(E::Transport);
            outputOffset+=sent.bytes;
            if(state==S::Handshaking){handshakeBytes+=sent.bytes;if(handshakeBytes>8*bufferLimit)return fail(E::Capacity);}
            return {T::Progress,0};
        }
        if(sent.bytes)return fail(E::Transport);
        if(sent.status==T::WouldBlock){const auto interest=sent.interest==I::None&&!wire.progressIO()?I::Write:sent.interest;
            if(!validPearlPoolInterest(interest))return fail(E::Transport);
            return {T::WouldBlock,0,interest};}
        if(sent.status==T::Progress&&sent.interest==I::None)return {T::Progress,0};
        return fail(E::Transport);
    }
    PearlPoolTransfer feed(){
        if(cancel.load())return fail(E::Cancelled);
        if(eof)return fail(E::Protocol);
        const auto space=BIO_ctrl_get_write_guarantee(network);
        if(!space)return fail(E::Capacity);
        const auto remaining=state==S::Handshaking?8*bufferLimit-handshakeBytes:bufferLimit;
        if(!remaining)return fail(E::Capacity);
        std::array<char,4096> input;const auto maximum=std::min(input.size(),std::min(space,remaining));
        const auto got=wire.receive(input.data(),maximum);
        if(cancel.load())return fail(E::Cancelled);
        if(got.status==T::Bytes){
            if(!got.bytes||got.bytes>maximum||got.interest!=I::None||BIO_write(network,input.data(),static_cast<int>(got.bytes))!=static_cast<int>(got.bytes))return fail(E::Transport);
            if(state==S::Handshaking){handshakeBytes+=got.bytes;if(handshakeBytes>8*bufferLimit)return fail(E::Capacity);}
            return {T::Progress,0};
        }
        if(got.bytes)return fail(E::Transport);
        if(got.status==T::WouldBlock){const auto interest=got.interest==I::None&&!wire.progressIO()?I::Read:got.interest;
            if(!validPearlPoolInterest(interest))return fail(E::Transport);
            return {T::WouldBlock,0,interest};}
        if(got.status==T::Progress&&got.interest==I::None)return {T::Progress,0};
        if(got.status==T::EndOfStream&&got.interest==I::None){eof=true;if(BIO_shutdown_wr(network)!=1)return fail(E::Protocol);return {T::Progress,0};}
        return fail(E::Transport);
    }
    PearlPoolTransfer retry(int sslError){
        if(sslError==SSL_ERROR_WANT_READ){const auto flushed=flush();if(flushed.status==T::Error||flushed.status==T::WouldBlock||outputPending())return flushed;
            // With AUTO_RETRY off, a processed control record can yield WANT_READ while the bounded
            // input BIO still holds the next record. Retry that internal input before a socket read/wait.
            if(BIO_ctrl_pending(SSL_get_rbio(ssl))!=0||SSL_pending(ssl)>0)return {T::Progress,0};
            return feed();}
        if(sslError==SSL_ERROR_WANT_WRITE){if(!outputPending())return fail(E::Protocol);return flush();}
        if(sslError==SSL_ERROR_ZERO_RETURN&&state==S::VerifiedOpen){state=S::Closed;wire.close();return {T::EndOfStream,0};}
        return fail(SSL_get_verify_result(ssl)!=X509_V_OK?E::Verification:E::Protocol);
    }
    bool roots(const std::string &pem){
        if(pem.empty())return SSL_CTX_set_default_verify_paths(ctx)==1;
        // Only complete certificate PEM blocks and ASCII whitespace; no private keys, CRLs or trailing junk.
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
        ssl=SSL_new(ctx);if(!ssl)return false;
        // DNS or unbracketed numeric IP identity; numeric addresses are checked as IP SANs, never as DNS/CN names.
        std::array<unsigned char,16> address;const bool ip=inet_pton(AF_INET,p.serverName.c_str(),address.data())==1||inet_pton(AF_INET6,p.serverName.c_str(),address.data())==1;
        if(ip){if(X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl),p.serverName.c_str())!=1)return false;}
        else{if(!dnsName(p.serverName))return false;
            SSL_set_hostflags(ssl,X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS|X509_CHECK_FLAG_NEVER_CHECK_SUBJECT);
            if(SSL_set1_host(ssl,p.serverName.c_str())!=1||SSL_set_tlsext_host_name(ssl,p.serverName.c_str())!=1)return false;}
        BIO *internal=nullptr;
        if(BIO_new_bio_pair(&internal,bufferLimit,&network,bufferLimit)!=1)return false;
        SSL_set_bio(ssl,internal,internal);SSL_set_connect_state(ssl);return true;
    }
#else
    ~Impl()=default;
#endif
};
PearlTlsStream::PearlTlsStream(PearlPoolTransport &w,const std::atomic<bool> &cancel):impl_(new Impl(w,cancel)){}
PearlTlsStream::~PearlTlsStream(){abort();}
bool PearlTlsStream::begin(const PearlTlsPolicy &p,uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(s.attempted){s.fail(E::InvalidPolicy);return false;}s.attempted=true;
        if(!p.enabled){s.fail(E::Disabled);return false;}
        if(p.serverName.empty()||p.serverName.size()>253||p.serverName.find('\0')!=p.serverName.npos||p.caPem.size()>bufferLimit||
            !now||p.handshakeDeadlineMonoMs<=now||p.handshakeDeadlineMonoMs-now>60000){s.fail(E::InvalidPolicy);return false;}
        s.adopted=true;
        s.state=S::Handshaking;s.deadline=p.handshakeDeadlineMonoMs;
        if(!s.check(now))return false;
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
        if(OpenSSL_version_num()<0x30500000L){s.fail(E::Unavailable);return false;}
        if(!s.configure(p)){s.fail(E::InvalidPolicy);return false;}
        if(!s.wire.isOpen()||!s.wire.setNonblocking(true)){s.fail(E::Transport);return false;}
        return true;
#else
        s.fail(E::Unavailable);return false;
#endif
    }catch(...){s.fail(E::Protocol);return false;}
}
PearlPoolTransfer PearlTlsStream::handshake(uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(!s.check(now))return {T::Error,0};
        if(s.state!=S::Handshaking)return s.fail(E::Protocol);
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
        if(++s.handshakeSteps>8192)return s.fail(E::Capacity);
        auto result=s.flush();if(result.status==T::Error||result.status==T::WouldBlock||s.outputPending())return result;
        if(!s.handshakeDone){ERR_clear_error();const int ret=SSL_do_handshake(s.ssl);const int err=SSL_get_error(s.ssl,ret);
            if(ret!=1)return s.retry(err);
            if(SSL_get_verify_result(s.ssl)!=X509_V_OK)return s.fail(E::Verification);
            X509 *peer=SSL_get1_peer_certificate(s.ssl);if(!peer)return s.fail(E::Verification);X509_free(peer);
            s.handshakeDone=true;result=s.flush();
            if(result.status==T::Error||result.status==T::WouldBlock||s.outputPending())return result;
        }
        if(s.cancel.load())return s.fail(E::Cancelled);
        s.state=S::VerifiedOpen;return {T::Progress,0};
#else
        return s.fail(E::Unavailable);
#endif
    }catch(...){return s.fail(E::Protocol);}
}
PearlPoolTransfer PearlTlsStream::send(const char *data,std::size_t length,uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(!s.check(now))return {T::Error,0};
        if(s.state!=S::VerifiedOpen||!data||!length)return s.fail(E::Protocol);
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
        if(s.writePointer&&(data!=s.writePointer||length!=s.writeLength||std::memcmp(data,s.writeCopy.data(),s.writeSize)!=0))return s.fail(E::Protocol);
        auto flushed=s.flush();if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;
        if(!s.writePointer){s.writePointer=data;s.writeLength=length;s.writeSize=std::min(length,recordLimit);std::memcpy(s.writeCopy.data(),data,s.writeSize);}
        if(!s.writeAccepted){std::size_t n=0;ERR_clear_error();const int ret=SSL_write_ex(s.ssl,s.writeCopy.data(),s.writeSize,&n);const int err=SSL_get_error(s.ssl,ret);
            if(ret!=1)return s.retry(err);
            if(n!=s.writeSize)return s.fail(E::Protocol);
            s.writeAccepted=n;flushed=s.flush();
            if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;
        }
        if(s.cancel.load())return s.fail(E::Cancelled);
        const auto accepted=s.writeAccepted;s.writePointer=nullptr;s.writeSize=s.writeLength=s.writeAccepted=0;
        return {T::Bytes,accepted};
#else
        return s.fail(E::Unavailable);
#endif
    }catch(...){return s.fail(E::Protocol);}
}
PearlPoolTransfer PearlTlsStream::receive(char *out,std::size_t maximum,uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(!s.check(now))return {T::Error,0};
        if(s.state!=S::VerifiedOpen||!out||!maximum)return s.fail(E::Protocol);
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
        if(s.writePointer)return s.fail(E::Protocol);
        auto flushed=s.flush();if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;
        if(s.plainOffset==s.plainSize){std::size_t n=0;ERR_clear_error();const int ret=SSL_read_ex(s.ssl,s.plain.data(),s.plain.size(),&n);const int err=SSL_get_error(s.ssl,ret);
            if(ret!=1)return s.retry(err);
            if(!n||n>s.plain.size())return s.fail(E::Protocol);
            s.plainOffset=0;s.plainSize=n;flushed=s.flush();
            if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;
        }
        if(s.cancel.load())return s.fail(E::Cancelled);
        const auto count=std::min(maximum,s.plainSize-s.plainOffset);std::memcpy(out,s.plain.data()+s.plainOffset,count);s.plainOffset+=count;
        return {T::Bytes,count};
#else
        return s.fail(E::Unavailable);
#endif
    }catch(...){return s.fail(E::Protocol);}
}
bool PearlTlsStream::bufferedRead()const noexcept{
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
    const auto &s=*impl_;return s.state==S::VerifiedOpen&&(s.plainOffset!=s.plainSize||s.outputPending()||s.eof||
        SSL_pending(s.ssl)!=0||SSL_has_pending(s.ssl)!=0||BIO_ctrl_pending(SSL_get_rbio(s.ssl))!=0);
#else
    return false;
#endif
}
PearlTlsState PearlTlsStream::state()const noexcept{return impl_->state;}
PearlTlsError PearlTlsStream::error()const noexcept{return impl_->error;}
void PearlTlsStream::abort()noexcept{auto &s=*impl_;s.attempted=true;if(s.state!=S::Closed&&s.state!=S::Failed){s.state=S::Closed;if(s.adopted){try{s.wire.close();}catch(...){}}}}
} // namespace om
