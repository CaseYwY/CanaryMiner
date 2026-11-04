#include "product/pool_tls_check.h"
#include "product_test_util.h"
using namespace om;
namespace {
PoolTlsCheckRequest request(){return {true,"localhost",1,1000,{},{}};}
void codecs(){
    auto r=request();std::string wire;CHECK(validPoolTlsCheckRequest(r)&&encodePoolTlsCheckRequest(r,1,&wire));
    PoolTlsCheckRequest decoded;uint64_t parent=0;CHECK(decodePoolTlsCheckRequest(wire,&decoded,&parent)&&parent==1&&decoded.host==r.host&&decoded.port==r.port&&decoded.enabled);
    for(std::size_t i=0;i<wire.size();++i){PoolTlsCheckRequest saved=request();saved.host="unchanged.test";uint64_t pid=99;
        CHECK(!decodePoolTlsCheckRequest(wire.substr(0,i),&saved,&pid)&&saved.host=="unchanged.test"&&pid==99);}
    CHECK(!decodePoolTlsCheckRequest(wire+"\n",&decoded,&parent));
    CHECK(!decodePoolTlsCheckRequest("CMTLSQ1\n01\n"+wire.substr(10),&decoded,&parent));
    for(unsigned mode=0;mode<12;++mode){auto bad=request();
        if(mode==0)bad.host="127.0.0.1";
        if(mode==1)bad.host="::1";
        if(mode==2)bad.host="bad..test";
        if(mode==3)bad.host="bad.test.";
        if(mode==4)bad.host="https://localhost";
        if(mode==5)bad.host=std::string("local\0host",10);
        if(mode==6)bad.port=0;
        if(mode==7)bad.port=65536;
        if(mode==8)bad.totalMs=49;
        if(mode==9)bad.totalMs=60001;
        if(mode==10)bad.caPem=std::string(65537,'x');
        if(mode==11)bad.host=std::string(64,'x')+".test";
        std::string unchanged="unchanged";CHECK(!validPoolTlsCheckRequest(bad)&&!encodePoolTlsCheckRequest(bad,1,&unchanged)&&unchanged=="unchanged");
    }
    auto p=std::make_shared<PearlTlsPrivateTrustInput>();p->enabled=true;p->referenceDnsName=r.host;p->anchors={{0,1,2,10,0}};p->crls={{3,0,4,10}};
    r.windowsPrivateTrust=p;CHECK(encodePoolTlsCheckRequest(r,UINT32_MAX,&wire)&&decodePoolTlsCheckRequest(wire,&decoded,&parent)&&parent==UINT32_MAX&&decoded.windowsPrivateTrust->anchors==p->anchors&&decoded.windowsPrivateTrust->crls==p->crls);
    CHECK(!encodePoolTlsCheckRequest(r,0,&wire)&&!encodePoolTlsCheckRequest(r,UINT64_MAX,&wire));
    for(unsigned mode=0;mode<7;++mode){auto bad=r;auto custom=std::make_shared<PearlTlsPrivateTrustInput>(*p);bad.windowsPrivateTrust=custom;
        if(mode==0)bad.caPem="not-allowed-together";
        if(mode==1)custom->enabled=false;
        if(mode==2)custom->referenceDnsName="other.test";
        if(mode==3)custom->leaf={1};
        if(mode==4)custom->anchors.clear();
        if(mode==5)custom->intermediates.resize(17,{1});
        if(mode==6)custom->crls.assign(2,PearlTlsDer(65536,1));
        CHECK(!validPoolTlsCheckRequest(bad));
    }
    for(unsigned s=0;s<=10;++s)for(unsigned g=0;g<=5;++g)for(unsigned e=0;e<=12;++e){
        PoolTlsCheckReply a{static_cast<PoolTlsCheckStatus>(s),static_cast<PoolTlsCheckStage>(g),static_cast<PearlTlsError>(e)},b;
        std::string text="unchanged";const bool encoded=encodePoolTlsCheckReply(a,&text);
        if(encoded){CHECK(decodePoolTlsCheckReply(text,&b)&&a.status==b.status&&a.stage==b.stage&&a.tlsError==b.tlsError);
            for(std::size_t i=0;i<text.size();++i)CHECK(!decodePoolTlsCheckReply(text.substr(0,i),&b));
            CHECK(!decodePoolTlsCheckReply(text+"\n",&b));}
        else CHECK(text=="unchanged");
    }
    {std::string text;CHECK(!encodePoolTlsCheckReply({static_cast<PoolTlsCheckStatus>(-1),PoolTlsCheckStage::None,PearlTlsError::None},&text));
        CHECK(!encodePoolTlsCheckReply({PoolTlsCheckStatus::Cancelled,static_cast<PoolTlsCheckStage>(-1),PearlTlsError::None},&text));
        CHECK(!encodePoolTlsCheckReply({PoolTlsCheckStatus::Tls,PoolTlsCheckStage::Handshake,static_cast<PearlTlsError>(-1)},&text));}
}
void refusal(){
    std::atomic<bool> cancel{false};auto r=request();r.enabled=false;
    CHECK(executePoolTlsCheck(r,cancel).status==PoolTlsCheckStatus::Disabled);
    r.enabled=true;cancel=true;CHECK(executePoolTlsCheck(r,cancel).status==PoolTlsCheckStatus::Cancelled);cancel=false;
    auto bad=r;bad.port=0;CHECK(executePoolTlsCheck(bad,cancel).status==PoolTlsCheckStatus::Invalid);
#if !defined(CANARY_POOL_TLS_CHECK)
    CHECK(executePoolTlsCheck(r,cancel).status==PoolTlsCheckStatus::Unavailable);
#endif
    PoolTlsCheckClient disabled("not-an-executable");r.enabled=false;const auto quiet=disabled.inspect(r,cancel);
    CHECK(quiet.transport==LocalTaskStatus::Ok&&!quiet.childStarted&&quiet.reply&&quiet.reply->status==PoolTlsCheckStatus::Disabled);
    r.enabled=true;CHECK(disabled.inspect(r,cancel).transport==LocalTaskStatus::Invalid);
    PoolTlsCheckClient stopped("not-an-executable");cancel=true;const auto stop=stopped.inspect(r,cancel);cancel=false;
    CHECK(stop.transport==LocalTaskStatus::Cancelled&&!stop.childStarted&&!stop.reply&&stopped.inspect(r,cancel).transport==LocalTaskStatus::Invalid);
}
}
int main(){codecs();refusal();return finishTests("one-shot TLS inspection contract (CPU only; no DNS/socket/handshake)");}
