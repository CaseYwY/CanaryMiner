// CPU/in-memory native TLS records with ephemeral synthetic certificates. No sockets, stores, pool or GPU.
#include "core/pearl_tls_stream.h"
#include "core/pearl_tls_private_trust.h"
#include "core/pearl_pool.h"
#include "product_test_util.h"
#include <functional>
#include <stdexcept>
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif
using namespace om;
namespace {
using T=PearlPoolTransferStatus;using I=PearlPoolInterest;using S=PearlTlsState;using E=PearlTlsError;
struct QuietWire: PearlPoolTransport {
    bool opened=true;unsigned writes=0,reads=0,modes=0;
    PearlPoolConnectStatus open(const std::string &,int)override{return PearlPoolConnectStatus::ConnectFailure;}
    bool isOpen()const override{return opened;}
    void close()override{opened=false;}
    PearlPoolWaitStatus waitReadable(int)override{return PearlPoolWaitStatus::Timeout;}
    PearlPoolWaitStatus waitWritable(int)override{return PearlPoolWaitStatus::Ready;}
    PearlPoolTransfer receive(char *,std::size_t)override{++reads;return {T::WouldBlock,0};}
    PearlPoolTransfer send(const char *,std::size_t)override{++writes;return {T::WouldBlock,0};}
    bool setNonblocking(bool b)override{++modes;return b;}
};
PearlTlsPolicy policy(){return {true,"tls-fixture.test",{},5000};}
void unavailable(){
    std::atomic<bool> cancel{false};
    {QuietWire w;PearlTlsStream c(w,cancel);CHECK(c.state()==S::Dormant&&!c.bufferedRead()&&!c.readProgressReady());PearlTlsPolicy p;
        CHECK(!c.begin(p,1000)&&c.error()==E::Disabled&&!w.writes&&!w.modes);
        p=policy();CHECK(!c.begin(p,1000)&&c.state()==S::Failed&&!w.writes);}
    for(unsigned mode=0;mode<6;++mode){QuietWire w;PearlTlsStream c(w,cancel);auto p=policy();
        if(mode==0)p.serverName.clear();
        if(mode==1)p.serverName=std::string("fixture\0.test",13);
        if(mode==2)p.caPem=std::string(65537,'x');
        if(mode==3)p.handshakeDeadlineMonoMs=1000;
        if(mode==4)p.handshakeDeadlineMonoMs=61001;
        if(mode==5)p.serverName=std::string(254,'x');
        CHECK(!c.begin(p,1000)&&c.error()==E::InvalidPolicy&&!w.modes&&!w.writes);}
    {QuietWire w;PearlTlsStream c(w,cancel);CHECK(c.send("secret",6,1000).status==T::Error&&c.state()==S::Failed&&!w.writes);}
    {QuietWire w;PearlTlsStream c(w,cancel);char output[8];CHECK(c.receive(output,sizeof(output),1000).status==T::Error&&!w.reads);}
    {QuietWire w;PearlTlsStream c(w,cancel);c.abort();CHECK(c.state()==S::Closed&&!c.begin(policy(),1000)&&!w.writes&&!w.modes);}
    {QuietWire w;{PearlTlsStream c(w,cancel);}CHECK(w.opened&&!w.writes&&!w.reads&&!w.modes);}
    {QuietWire w;PearlTlsStream c(w,cancel);c.abort();CHECK(w.opened);}
#if !defined(CANARY_POOL_TLS_OPENSSL) && !defined(CANARY_POOL_TLS_SCHANNEL)
    {QuietWire w;PearlTlsStream c(w,cancel);CHECK(!c.begin(policy(),1000)&&c.error()==E::Unavailable&&!w.modes&&!w.writes);}
#endif
}
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
void require(bool ok){if(!ok)throw std::runtime_error("synthetic TLS fixture setup failed");}
using Key=std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)>;
using Cert=std::unique_ptr<X509,decltype(&X509_free)>;
Key key(){Key k(EVP_PKEY_Q_keygen(nullptr,nullptr,"EC","prime256v1"),EVP_PKEY_free);require(bool(k));return k;}
void extension(X509 *cert,X509 *issuer,int nid,const char *value){
    X509V3_CTX ctx;X509V3_set_ctx(&ctx,issuer,cert,nullptr,nullptr,0);
    X509_EXTENSION *ext=X509V3_EXT_conf_nid(nullptr,&ctx,nid,value);require(ext!=nullptr);
    const bool ok=X509_add_ext(cert,ext,-1)==1;X509_EXTENSION_free(ext);require(ok);
}
Cert certificate(EVP_PKEY *pub,X509 *issuer,EVP_PKEY *sign,bool root=false,unsigned variant=0){
    Cert c(X509_new(),X509_free);require(bool(c));require(X509_set_version(c.get(),2)==1);
    require(ASN1_INTEGER_set(X509_get_serialNumber(c.get()),root?1:static_cast<long>(variant+2))==1);
    const long begin=variant==3?3600:-86400,end=variant==2?-3600:86400;
    require(X509_gmtime_adj(X509_getm_notBefore(c.get()),begin)&&X509_gmtime_adj(X509_getm_notAfter(c.get()),end));
    require(X509_set_pubkey(c.get(),pub)==1);auto *subject=X509_get_subject_name(c.get());
    const auto *name=root?"fixture-root":"tls-fixture.test";
    require(X509_NAME_add_entry_by_txt(subject,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char *>(name),-1,-1,0)==1);
    require(X509_set_issuer_name(c.get(),issuer?X509_get_subject_name(issuer):subject)==1);
    extension(c.get(),issuer?issuer:c.get(),NID_basic_constraints,root?"critical,CA:TRUE,pathlen:1":"critical,CA:FALSE");
    extension(c.get(),issuer?issuer:c.get(),NID_key_usage,root?"critical,keyCertSign,cRLSign":"critical,digitalSignature");
    if(!root){extension(c.get(),issuer,NID_ext_key_usage,variant==4?"clientAuth":"serverAuth");
        extension(c.get(),issuer,NID_subject_alt_name,variant==1?"DNS:wrong-fixture.test":variant==5?"DNS:127.0.0.1":"DNS:tls-fixture.test,IP:127.0.0.1,IP:::1");}
    require(X509_sign(c.get(),sign,EVP_sha256())>0);return c;
}
std::string pem(X509 *cert){BIO *b=BIO_new(BIO_s_mem());require(b!=nullptr);require(PEM_write_bio_X509(b,cert)==1);
    char *p=nullptr;const auto n=BIO_get_mem_data(b,&p);std::string out(p,static_cast<std::size_t>(n));BIO_free(b);return out;}
struct Material {
    Key rootKey=key(),leafKey=key();Cert root=certificate(rootKey.get(),nullptr,rootKey.get(),true);
    Cert leaf=certificate(leafKey.get(),root.get(),rootKey.get());
    std::string anchors=pem(root.get());
};
struct Peer final:QuietWire {
    SSL_CTX *ctx=nullptr;SSL *ssl=nullptr;BIO *input=nullptr,*output=nullptr;
    bool fatal=false,abrupt=false,holdWrites=false,holdReads=false,echo=true,throwSend=false,throwReceive=false;
    unsigned blockWriteAt=0;std::function<void()> afterSend;
    std::size_t sendLimit=65536,readLimit=65536;std::string application,allWire,pendingLines;
    std::function<void(const std::string &)> line;
    explicit Peer(Material &m,X509 *leaf=nullptr,bool clientAuth=false,int maximumVersion=0){
        ctx=SSL_CTX_new(TLS_server_method());require(ctx!=nullptr);
        require(SSL_CTX_set_min_proto_version(ctx,maximumVersion&&maximumVersion<TLS1_2_VERSION?TLS1_VERSION:TLS1_2_VERSION)==1);
        if(maximumVersion)require(SSL_CTX_set_max_proto_version(ctx,maximumVersion)==1);
        require(SSL_CTX_use_certificate(ctx,leaf?leaf:m.leaf.get())==1&&SSL_CTX_use_PrivateKey(ctx,m.leafKey.get())==1);
        if(clientAuth)SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT,nullptr);
        ssl=SSL_new(ctx);require(ssl!=nullptr);input=BIO_new(BIO_s_mem());output=BIO_new(BIO_s_mem());require(input&&output);
        BIO_set_mem_eof_return(input,-1);SSL_set_bio(ssl,input,output);SSL_set_accept_state(ssl);
    }
    ~Peer()override{SSL_free(ssl);SSL_CTX_free(ctx);}
    void text(const std::string &bytes){std::size_t n=0;ERR_clear_error();require(SSL_write_ex(ssl,bytes.data(),bytes.size(),&n)==1&&n==bytes.size());}
    void drive(){
        if(fatal)return;
        if(!SSL_is_init_finished(ssl)){ERR_clear_error();const int r=SSL_do_handshake(ssl);const int e=SSL_get_error(ssl,r);
            if(r!=1){if(e!=SSL_ERROR_WANT_READ&&e!=SSL_ERROR_WANT_WRITE)fatal=true;return;}}
        for(unsigned i=0;i<32;++i){char buffer[4096];std::size_t n=0;ERR_clear_error();const int r=SSL_read_ex(ssl,buffer,sizeof(buffer),&n);const int e=SSL_get_error(ssl,r);
            if(r!=1){if(e!=SSL_ERROR_WANT_READ&&e!=SSL_ERROR_WANT_WRITE&&e!=SSL_ERROR_ZERO_RETURN)fatal=true;return;}
            require(n>0&&application.size()+n<2*1024*1024);application.append(buffer,n);
            if(echo)text(std::string(buffer,n));
            if(line){pendingLines.append(buffer,n);for(std::size_t end;(end=pendingLines.find('\n'))!=pendingLines.npos;){
                const auto next=pendingLines.substr(0,end);pendingLines.erase(0,end+1);line(next);}}
        }
    }
    PearlPoolTransfer send(const char *bytes,std::size_t n)override{
        ++writes;if(throwSend)throw std::runtime_error("fixture send");if(holdWrites||(blockWriteAt&&writes>=blockWriteAt))return {T::WouldBlock,0};
        const auto take=std::min(n,sendLimit);require(take!=0&&take<=65536);allWire.append(bytes,take);
        require(BIO_write(input,bytes,static_cast<int>(take))==static_cast<int>(take));drive();if(afterSend)afterSend();return {T::Bytes,take};
    }
    PearlPoolTransfer receive(char *bytes,std::size_t n)override{
        ++reads;if(throwReceive)throw std::runtime_error("fixture read");if(holdReads)return {T::WouldBlock,0};drive();
        if(BIO_ctrl_pending(output)){const auto take=std::min(n,readLimit);const int got=BIO_read(output,bytes,static_cast<int>(take));require(got>0);return {T::Bytes,static_cast<std::size_t>(got)};}
        return {abrupt||fatal?T::EndOfStream:T::WouldBlock,0};
    }
    PearlPoolWaitStatus waitReadable(int)override{drive();return BIO_ctrl_pending(output)||abrupt||fatal?PearlPoolWaitStatus::Ready:PearlPoolWaitStatus::Timeout;}
};
bool connect(PearlTlsStream &c,unsigned cap=8192){
    for(unsigned i=0;i<cap&&c.state()==S::Handshaking;++i){const auto r=c.handshake(1000);CHECK(r.bytes==0);if(r.status==T::Error)break;}
    return c.state()==S::VerifiedOpen;
}
void native(Material &m){
    std::atomic<bool> cancel{false};
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;p.windowsPrivateTrust=std::make_shared<PearlTlsPrivateTrustInput>();
        CHECK(!c.begin(p,1000)&&c.error()==E::Unavailable&&!w.writes&&!w.modes);}
    for(unsigned mode=0;mode<9;++mode){auto leaf=certificate(m.leafKey.get(),m.root.get(),m.rootKey.get(),false,mode<6?mode:0);
        Peer w(m,leaf.get());PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;
        if(mode==6)p.caPem.clear();
        if(mode==5||mode==7)p.serverName="127.0.0.1";
        if(mode==8)p.serverName="::1";
        require(c.begin(p,1000));const bool opened=connect(c);
        const bool expected=mode==0||mode>=7;
        CHECK(opened==expected&&w.application.empty()&&w.modes==1);
        if(!expected)CHECK(c.state()==S::Failed&&c.error()==E::Verification&&!w.opened);
        else{const char *sni=SSL_get_servername(w.ssl,TLSEXT_NAMETYPE_host_name);
            CHECK(mode==0?(sni&&std::string(sni)==p.serverName):sni==nullptr);}
    }
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;p.serverName="other-fixture.test";
        CHECK(c.begin(p,1000)&&!connect(c)&&c.error()==E::Verification&&w.application.empty());}
    {Material other;Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=other.anchors;
        CHECK(c.begin(p,1000)&&!connect(c)&&c.error()==E::Verification&&w.application.empty());}
    for(int maximum:{TLS1_1_VERSION,TLS1_2_VERSION}){Peer w(m,nullptr,false,maximum);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;
        CHECK(c.begin(p,1000)&&connect(c)==(maximum==TLS1_2_VERSION)&&w.application.empty());
        if(maximum==TLS1_2_VERSION)CHECK(SSL_version(w.ssl)==TLS1_2_VERSION);}
    for(const auto &anchors:{std::string("garbage"),m.anchors+"junk",pem(m.leaf.get()),std::string("\n\t")}){
        Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=anchors;CHECK(!c.begin(p,1000)&&!w.writes&&!w.modes);}
    for(const auto *name:{"bad..test","-bad.test","bad-.test","https://fixture.test","*.test","fixture.test:443","fixture.test."}){
        Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;p.serverName=name;CHECK(!c.begin(p,1000)&&!w.writes);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000));
        CHECK(c.handshake(5000).status==T::Error&&c.error()==E::Timeout&&!w.writes);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000));
        CHECK(c.handshake(999).status==T::Error&&c.error()==E::Clock&&!w.writes);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000));
        CHECK(c.send("no-before-auth",14,1000).status==T::Error&&w.application.empty()&&!w.writes);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;w.holdWrites=true;CHECK(c.begin(p,1000));
        for(unsigned i=0;i<10;++i)CHECK(c.handshake(1000).status==T::WouldBlock&&c.state()==S::Handshaking&&w.application.empty());
        w.holdWrites=false;CHECK(connect(c));}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;w.holdWrites=true;CHECK(c.begin(p,1000));
        unsigned calls=0;while(c.state()==S::Handshaking&&calls<8200){++calls;c.handshake(1000);}
        CHECK(calls==8193&&c.state()==S::Failed&&c.error()==E::Capacity&&w.allWire.empty());}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;w.blockWriteAt=2;CHECK(c.begin(p,1000));
        bool blocked=false;for(unsigned i=0;i<32;++i){const auto r=c.handshake(1000);if(r.status==T::WouldBlock&&r.interest==I::Write){blocked=true;break;}if(r.status==T::Error)break;}
        CHECK(blocked&&c.state()==S::Handshaking&&w.application.empty());
        w.blockWriteAt=0;CHECK(connect(c));}
    for(std::size_t fragment:{1u,7u,4096u}){Peer w(m);w.sendLimit=w.readLimit=fragment;PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;
        CHECK(c.begin(p,1000)&&connect(c));
        const std::string message="synthetic-account.never-public\n";std::size_t sent=0;unsigned steps=0;
        while(sent<message.size()&&++steps<10000){const auto r=c.send(message.data()+sent,message.size()-sent,1000);
            CHECK(r.status!=T::Error);if(r.status==T::Error)break;if(r.status==T::Bytes)sent+=r.bytes;else CHECK(!r.bytes);}
        CHECK(sent==message.size()&&w.application==message&&w.allWire.find(message)==w.allWire.npos);
        std::string received;char out[3];steps=0;
        while(received.size()<message.size()&&++steps<10000){const auto r=c.receive(out,sizeof(out),1000);CHECK(r.status!=T::Error);
            if(r.status==T::Error)break;
            if(r.status==T::Bytes)received.append(out,r.bytes);}
        CHECK(received==message&&w.modes==1);
        ERR_clear_error();CHECK(SSL_shutdown(w.ssl)>=0);bool clean=false;
        for(unsigned i=0;i<1000;++i){const auto r=c.receive(out,sizeof(out),1000);if(r.status==T::EndOfStream){clean=true;break;}if(r.status==T::Error)break;}
        CHECK(clean&&c.state()==S::Closed&&!w.opened);
    }
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        w.holdWrites=true;std::string bytes="retry-original";const auto r=c.send(bytes.data(),bytes.size(),1000);CHECK(r.status==T::WouldBlock&&!r.bytes&&w.application.empty());
        bytes[0]='X';w.holdWrites=false;CHECK(c.send(bytes.data(),bytes.size(),1000).status==T::Error&&w.application.empty());}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        w.holdWrites=true;const std::string bytes="one-encryption-only";
        CHECK(c.send(bytes.data(),bytes.size(),1000).status==T::WouldBlock);const auto calls=w.writes;
        for(unsigned i=0;i<8;++i)CHECK(c.send(bytes.data(),bytes.size(),1000).status==T::WouldBlock);
        CHECK(w.writes==calls+8&&w.application.empty());w.holdWrites=false;
        const auto done=c.send(bytes.data(),bytes.size(),1000);CHECK(done.status==T::Bytes&&done.bytes==bytes.size()&&w.application==bytes);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        std::string original(131079,'x');for(std::size_t i=0;i<original.size();++i)original[i]=static_cast<char>('a'+i%23);
        std::size_t sent=0;unsigned turns=0;
        while(sent<original.size()&&++turns<1000){const auto r=c.send(original.data()+sent,original.size()-sent,1000);CHECK(r.status!=T::Error);
            if(r.status==T::Error)break;
            if(r.status==T::Bytes){CHECK(r.bytes&&r.bytes<=16384);sent+=r.bytes;}}
        CHECK(sent==original.size()&&w.application==original);
        std::string echoed;char out[97];turns=0;
        while(echoed.size()<original.size()&&++turns<5000){const auto r=c.receive(out,sizeof(out),1000);CHECK(r.status!=T::Error);
            if(r.status==T::Error)break;
            if(r.status==T::Bytes)echoed.append(out,r.bytes);}
        CHECK(echoed==original);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        const std::string data="buffered-plaintext\nnext-line\n";w.text(data);char out[1];std::string received;
        for(unsigned i=0;i<64&&received.empty();++i){const auto r=c.receive(out,sizeof(out),1000);if(r.status==T::Bytes)received.append(out,r.bytes);}
        CHECK(received==data.substr(0,1)&&c.bufferedRead());
        for(unsigned i=0;i<64&&received.size()<data.size();++i){const auto r=c.receive(out,sizeof(out),1000);if(r.status==T::Bytes)received.append(out,r.bytes);else CHECK(r.status!=T::Error);}
        CHECK(received==data&&!c.bufferedRead());}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        CHECK(SSL_version(w.ssl)==TLS1_3_VERSION);w.echo=false;
        require(SSL_key_update(w.ssl,SSL_KEY_UPDATE_REQUESTED)==1);ERR_clear_error();require(SSL_do_handshake(w.ssl)==1);
        const std::string message="after-key-update\n";w.text(message);std::string received;char out[64];
        for(unsigned i=0;i<64&&received.size()<message.size();++i){const auto r=c.receive(out,sizeof(out),1000);CHECK(r.status!=T::Error);if(r.status==T::Bytes)received.append(out,r.bytes);}
        CHECK(received==message&&c.state()==S::VerifiedOpen);
        const std::string response="response-after-update";std::size_t sent=0;
        for(unsigned i=0;i<64&&sent<response.size();++i){const auto r=c.send(response.data()+sent,response.size()-sent,1000);CHECK(r.status!=T::Error);if(r.status==T::Bytes)sent+=r.bytes;}
        CHECK(sent==response.size()&&w.application==response);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        w.afterSend=[&]{cancel=true;};const std::string data="cancel-after-wire-acceptance";
        const auto r=c.send(data.data(),data.size(),1000);CHECK(r.status==T::Error&&!r.bytes&&c.error()==E::Cancelled&&w.application==data);
        const auto calls=w.writes;cancel=false;CHECK(c.send(data.data(),data.size(),1000).status==T::Error&&w.writes==calls);}
    {Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        w.abrupt=true;char out[64];bool failed=false;for(unsigned i=0;i<32;++i){const auto r=c.receive(out,sizeof(out),1000);CHECK(r.status!=T::EndOfStream);if(r.status==T::Error){failed=true;break;}}
        CHECK(failed&&c.error()==E::Protocol&&c.state()==S::Failed);}
    for(unsigned mode=0;mode<3;++mode){Peer w(m);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000)&&connect(c));
        if(mode==0)cancel=true;
        if(mode==1)w.throwSend=true;
        if(mode==2)w.throwReceive=true;
        char out[64];const auto before=w.writes;const auto r=mode==2?c.receive(out,sizeof(out),1000):c.send("private",7,1000);
        CHECK(r.status==T::Error&&c.state()==S::Failed&&w.application.empty());
        if(mode==0)CHECK(c.error()==E::Cancelled&&w.writes==before);
        cancel=false;CHECK(c.send("late",4,1000).status==T::Error&&!c.begin(p,1000));}
    {Peer w(m,nullptr,true);PearlTlsStream c(w,cancel);auto p=policy();p.caPem=m.anchors;CHECK(c.begin(p,1000));
        connect(c);char out[8];for(unsigned i=0;i<32&&c.state()==S::VerifiedOpen;++i)c.receive(out,sizeof(out),1000);
        CHECK(w.fatal&&c.state()==S::Failed&&w.application.empty()&&SSL_get0_peer_certificate(w.ssl)==nullptr);}
}
std::string notification(){std::string header;const char *digits="0123456789abcdef";
    for(unsigned i=0;i<76;++i){header+=digits[((i+1)&255)>>4];header+=digits[(i+1)&15];}
    return "{\"id\":null,\"method\":\"mining.notify\",\"params\":{\"job_id\":\"tls-job\",\"header\":\""+header+
        "\",\"target\":\""+std::string(60,'0')+"ffff\",\"height\":139053,\"cert_version\":3}}\n";
}
struct Adapter final:PearlPoolTransport {
    std::atomic<bool> stop{false};Peer raw;PearlTlsStream tls;PearlTlsPolicy config;
    explicit Adapter(Material &m):raw(m),tls(raw,stop),config(policy()){
        config.caPem=m.anchors;raw.echo=false;raw.line=[&](const std::string &line){
            if(line.find("mining.authorize")!=line.npos)raw.text("{\"id\":1,\"result\":true,\"error\":null}\n"+notification());
            if(line.find("mining.submit")!=line.npos){const auto begin=line.find("\"id\":")+5;const auto end=line.find_first_not_of("0123456789",begin);
                raw.text("{\"id\":"+line.substr(begin,end-begin)+",\"result\":true,\"error\":null}\n");}};
    }
    PearlPoolConnectStatus open(const std::string &name,int)override{
        if(name!=config.serverName||!tls.begin(config,1000)||!connect(tls))return PearlPoolConnectStatus::ConnectFailure;
        return PearlPoolConnectStatus::Connected;
    }
    bool isOpen()const override{return tls.state()==S::VerifiedOpen;}
    void close()override{tls.abort();}
    bool progressIO()const override{return true;}
    bool bufferedRead()const override{return tls.bufferedRead();}
    bool setNonblocking(bool)override{return false;}
    PearlPoolWaitStatus waitReadable(int ms)override{return raw.waitReadable(ms);}
    PearlPoolWaitStatus waitWritable(int ms)override{return raw.waitWritable(ms);}
    PearlPoolTransfer send(const char *p,std::size_t n)override{return tls.send(p,n,1000);}
    PearlPoolTransfer receive(char *p,std::size_t n)override{return tls.receive(p,n,1000);}
};
void canonical(Material &m){
    Adapter wire(m);PearlPoolSource source("tls-fixture.test",443,"placeholder","fixture",
        {PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty},"",&wire);
    Job job;const bool fetched=source.fetch(&job);
    CHECK(fetched&&job.valid&&job.jobId=="tls-job");
    CHECK(wire.tls.state()==S::VerifiedOpen&&wire.raw.application.find("mining.authorize")!=wire.raw.application.npos);
    Solution proof;proof.nonce=7;proof.hit[0]=11;proof.extra="synthetic-proof";std::string error;
    const bool submitted=source.submit(job,proof,&error);
    CHECK(submitted&&source.submitCounters().accepted==1);
    CHECK(wire.raw.application.find("synthetic-proof")!=wire.raw.application.npos&&wire.raw.allWire.find("synthetic-proof")==wire.raw.allWire.npos);
}
#endif
}
int main(){
    try{unavailable();
#if defined(CANARY_POOL_TLS_OPENSSL) && !defined(_WIN32)
        Material m;native(m);canonical(m);
#endif
    }catch(const std::exception &){CHECK_MSG("synthetic fixture construction/operation failed",false);}
    return finishTests("native TLS stream (in-memory synthetic peer; no live endpoint)");
}
