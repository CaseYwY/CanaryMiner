// Offline bounded inspection only. Fixture files contain public synthetic DER, never private keys or OS-store enrollment.
#include "core/pearl_tls_private_trust.h"
#include "product_test_util.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
using namespace om;
namespace {
using S=PearlTlsPrivateTrustStatus;
PearlTlsPrivateTrustInput tiny(){return {true,"fixture.test",{1},{{2}},{},{}};}
void common(){std::atomic<bool> cancel{false};
    {PearlTlsPrivateTrustInput p;const auto r=inspectPearlTlsPrivateTrust(p,cancel);CHECK(r.status==S::Disabled&&!r.chainEvaluated&&!r.policyEvaluated);}
    for(unsigned mode=0;mode<9;++mode){auto p=tiny();
        if(mode==0)p.leaf.clear();
        if(mode==1)p.leaf.resize(65537);
        if(mode==2)p.anchors.clear();
        if(mode==3)p.anchors.resize(17,{1});
        if(mode==4)p.crls={{}};
        if(mode==5)p.intermediates.resize(17,{1});
        if(mode==6)p.referenceDnsName="bad..test";
        if(mode==7)p.referenceDnsName=std::string("bad\0.test",9);
        if(mode==8)p.crls.resize(16,PearlTlsDer(65536,1));
        const auto r=inspectPearlTlsPrivateTrust(p,cancel);CHECK(r.status==S::InvalidInput&&!r.chainEvaluated&&!r.policyEvaluated);}
    for(const char *ip:{"127.0.0.1","::1"}){auto p=tiny();p.referenceDnsName=ip;CHECK(inspectPearlTlsPrivateTrust(p,cancel).status==S::UnsupportedReference);}
    {auto p=tiny();cancel=true;CHECK(inspectPearlTlsPrivateTrust(p,cancel).status==S::Cancelled);cancel=false;}
    const auto r=inspectPearlTlsPrivateTrust(tiny(),cancel);
#if defined(_WIN32) && defined(CANARY_POOL_TLS_PRIVATE_TRUST_WINDOWS)
    CHECK(r.status==S::InvalidInput&&!r.chainEvaluated);
#else
    CHECK(r.status==S::Unavailable&&!r.chainEvaluated);
#endif
}
PearlTlsDer read(const std::filesystem::path &path){std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in||in.tellg()<=0||in.tellg()>65536)throw std::runtime_error("invalid synthetic public fixture");
    PearlTlsDer bytes(static_cast<std::size_t>(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char *>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!in)throw std::runtime_error("incomplete synthetic public fixture");
    return bytes;
}
void fixtures(const std::filesystem::path &dir){
    std::atomic<bool> cancel{false};
    for(const char *kind:{"missing","stale","revoked","expired","future","wrongusage","badsig","wrongroot","valid"}){
        const std::string prefix=kind;PearlTlsPrivateTrustInput p;p.enabled=true;p.referenceDnsName="fixture.test";
        p.anchors={read(dir/(prefix+".root.der"))};p.leaf=read(dir/(prefix+".leaf.der"));
        if(prefix!="missing")p.crls={read(dir/(prefix+".crl.der"))};
        const auto r=inspectPearlTlsPrivateTrust(p,cancel);const bool valid=prefix=="valid";
        if((r.status==S::Verified)!=valid||r.status==S::NativeFailure)std::fprintf(stderr,"[PRIVATE-TRUST-FIXTURE] case=%s status=%u chain=%08x policy=%08x evaluated=%d/%d native=%u/%08x\n",kind,
            static_cast<unsigned>(r.status),r.chainErrors,r.policyError,r.chainEvaluated,r.policyEvaluated,static_cast<unsigned>(r.nativeStage),r.nativeError);
        CHECK((r.status==S::Verified)==valid&&r.chainEvaluated);
        if(valid){CHECK(r.policyEvaluated&&!r.chainErrors&&!r.policyError);
            p.referenceDnsName="wrong-fixture.test";const auto wrong=inspectPearlTlsPrivateTrust(p,cancel);
            CHECK(wrong.status==S::Rejected&&wrong.chainEvaluated&&!wrong.chainErrors&&wrong.policyEvaluated&&wrong.policyError);
            p.referenceDnsName="fixture.test";p.leaf=p.anchors.front();CHECK(inspectPearlTlsPrivateTrust(p,cancel).status==S::Rejected);
            p.leaf=read(dir/(prefix+".leaf.der"));p.anchors={p.leaf};CHECK(inspectPearlTlsPrivateTrust(p,cancel).status==S::InvalidInput);
        }else{CHECK(r.status==S::Rejected&&r.chainErrors&&!r.policyEvaluated);
            if(prefix=="missing"||prefix=="stale")CHECK((r.chainErrors&0x01000040u)!=0); // unknown/offline revocation
            if(prefix=="revoked")CHECK((r.chainErrors&0x00000004u)!=0);
            if(prefix=="expired"||prefix=="future")CHECK((r.chainErrors&0x00000001u)!=0);
            if(prefix=="wrongusage")CHECK((r.chainErrors&0x00000010u)!=0);
            if(prefix=="badsig")CHECK((r.chainErrors&0x00000008u)!=0);
            if(prefix=="wrongroot")CHECK((r.chainErrors&0x00010028u)!=0);
        }
    }
}
}
int main(int argc,char **argv){
    try{common();if(argc==3&&std::string(argv[1])=="--fixtures")fixtures(std::filesystem::u8path(argv[2]));else CHECK(argc==1);}
    catch(const std::exception &){CHECK_MSG("synthetic fixture unavailable or invalid",false);}
    return finishTests("offline private native trust (no TLS handshake or OS-store install)");
}
