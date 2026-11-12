#include "product/pool_tls_process_transport.h"
#include "core/pearl_pool.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
namespace {
using T=PearlPoolTransferStatus;using I=PearlPoolInterest;using W=PearlPoolWaitStatus;
PoolTlsCheckRequest config(std::string host="echo.test"){return {true,std::move(host),1,5000,{},{}};}
void roundTrip(const std::filesystem::path &helper,PoolTlsTransportMode mode){std::atomic<bool> cancel{false};PoolTlsProcessTransport t(helper,config(),cancel,nullptr,mode);
    CHECK(t.open("echo.test",1)==PearlPoolConnectStatus::Connected&&t.isOpen()&&t.progressIO());
    std::string original(32779,'x');for(std::size_t i=0;i<original.size();++i)original[i]=static_cast<char>(i%256);
    std::size_t sent=0;unsigned turns=0;while(sent<original.size()&&++turns<256){const auto r=t.send(original.data()+sent,original.size()-sent);
        CHECK(r.status!=T::Error);if(r.status==T::Error)break;if(r.status==T::Bytes)sent+=r.bytes;else if(r.status==T::WouldBlock)CHECK(t.waitFor(r.interest,1000)==W::Ready);}
    CHECK(sent==original.size()&&t.isOpen());std::string received;char bytes[97];turns=0;
    while(received.size()<original.size()&&++turns<1024){const auto r=t.receive(bytes,sizeof(bytes));CHECK(r.status!=T::Error);if(r.status==T::Error)break;
        if(r.status==T::Bytes)received.append(bytes,r.bytes);else if(r.status==T::WouldBlock)CHECK(t.waitFor(r.interest,1000)==W::Ready);}
    CHECK(received==original);t.close();CHECK(!t.isOpen()&&t.open("echo.test",1)!=PearlPoolConnectStatus::Connected);
}
void canonicalFixture(const std::filesystem::path &helper,PoolTlsTransportMode mode){std::atomic<bool> cancel{false};PoolTlsProcessTransport transport(helper,config("fixture.test"),cancel,nullptr,mode);
    PearlPoolSource source("fixture.test",1,"placeholder","fixture",{PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty},"",&transport);
    Job job;CHECK(source.fetch(&job)&&job.valid&&job.jobId=="rpc-job");Solution solution;solution.nonce=7;solution.hit[0]=11;solution.extra="synthetic-proof";std::string error;
    const bool submitted=source.submit(job,solution,&error);
    CHECK(submitted&&source.submitCounters().accepted==1);
}
void refusal(const std::filesystem::path &helper){std::atomic<bool> cancel{false};
    {PoolTlsProcessTransport t(helper,config(),cancel);t.close();CHECK(t.open("echo.test",1)!=PearlPoolConnectStatus::Connected);}
    {PoolTlsProcessTransport t(helper,config(),cancel);CHECK(t.open("other.test",1)!=PearlPoolConnectStatus::Connected&&!t.isOpen());}
    {PoolTlsProcessTransport t(helper,config(),cancel);CHECK(t.open("echo.test",1)==PearlPoolConnectStatus::Connected);std::string data="unchanged";
        CHECK(t.send(data.data(),data.size()).status==T::WouldBlock);data[0]='X';CHECK(t.send(data.data(),data.size()).status==T::Error&&t.error()==PearlTlsError::Protocol);}
    {PoolTlsProcessTransport t(helper,config("stall-open.test"),cancel);std::thread stop([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));cancel=true;});
        const auto begin=std::chrono::steady_clock::now();CHECK(t.open("stall-open.test",1)!=PearlPoolConnectStatus::Connected);stop.join();CHECK(t.error()==PearlTlsError::Cancelled&&std::chrono::steady_clock::now()-begin<std::chrono::seconds(2));cancel=false;
        CHECK(t.open("stall-open.test",1)!=PearlPoolConnectStatus::Connected);}
    {PoolTlsProcessTransport t(helper,config("lost-write.test"),cancel);CHECK(t.open("lost-write.test",1)==PearlPoolConnectStatus::Connected);const std::string data="not-replayable";bool failed=false;
        for(unsigned i=0;i<16;++i){const auto r=t.send(data.data(),data.size());if(r.status==T::Error){failed=true;break;}if(r.status==T::WouldBlock)t.waitFor(r.interest,1000);}
        CHECK(failed&&!t.isOpen()&&t.open("lost-write.test",1)!=PearlPoolConnectStatus::Connected);}
    {PoolTlsProcessTransport t(helper,config("lost-submit.test"),cancel);
        PearlPoolSource source("lost-submit.test",1,"placeholder","fixture",{PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty},"",&t);
        Job job;CHECK(source.fetch(&job)&&job.valid);Solution proof;proof.nonce=7;proof.hit[0]=11;proof.extra="synthetic-proof";std::string error;
        const bool submitted=source.submit(job,proof,&error);
        PearlSourceEvent events[64];const auto count=source.pearlSourceObservability()->drain(events,64);unsigned unknowns=0;
        for(std::size_t i=0;i<count;++i)if(events[i].delivery==PearlSourceDelivery::PossiblySent&&events[i].terminal==PearlSourceTerminal::Unknown)++unknowns;
        CHECK(!submitted&&unknowns==1&&source.submitCounters().submitted==0&&source.submitCounters().unknown==0&&source.submitCounters().accepted==0&&!t.isOpen());
        CHECK(t.open("lost-submit.test",1)!=PearlPoolConnectStatus::Connected);}
}
void openEvidenceAndPlaintext(const std::filesystem::path &helper){
    using C=PoolTlsCheckStatus;using G=PoolTlsCheckStage;using E=PearlTlsError;
    struct Case{const char *host;C status;G stage;E error;};std::atomic<bool> cancel{false};
    for(const auto &item:{Case{"dns-fail.test",C::Dns,G::Resolve,E::None},Case{"connect-fail.test",C::Connect,G::Connect,E::None},
                        Case{"network-timeout.test",C::Timeout,G::Connect,E::None},Case{"handshake-timeout.test",C::Timeout,G::Handshake,E::None},
                        Case{"verify-fail.test",C::Tls,G::Handshake,E::Verification},Case{"unavailable-open.test",C::Unavailable,G::None,E::None}}){
        PoolTlsProcessTransport tls(helper,config(item.host),cancel);
        CHECK(!tls.openFailure()&&tls.open(item.host,1)!=PearlPoolConnectStatus::Connected&&!tls.isOpen());
        const auto proof=tls.openFailure();CHECK(proof&&proof->status==item.status&&proof->stage==item.stage&&proof->tlsError==item.error);
        tls.close();CHECK(tls.openFailure().has_value());
        PoolTlsProcessTransport plain(helper,config(item.host),cancel,nullptr,PoolTlsTransportMode::Plaintext);
        CHECK(plain.open(item.host,1)==PearlPoolConnectStatus::Connected&&plain.isOpen()&&!plain.openFailure());
    }
    for(auto mode:{PoolTlsTransportMode::Tls,PoolTlsTransportMode::Plaintext}){
        PoolTlsProcessTransport invalid(helper,config("wrong-mode.test"),cancel,nullptr,mode);
        CHECK(invalid.open("wrong-mode.test",1)!=PearlPoolConnectStatus::Connected&&invalid.error()==E::Protocol&&!invalid.openFailure());
    }
    {PoolTlsProcessTransport lost(helper,config("lost-open.test"),cancel);CHECK(lost.open("lost-open.test",1)!=PearlPoolConnectStatus::Connected&&!lost.openFailure());}
    {auto request=config("stall-open.test");request.totalMs=100;PoolTlsProcessTransport timeout(helper,request,cancel);
        CHECK(timeout.open(request.host,1)!=PearlPoolConnectStatus::Connected&&timeout.error()==E::Timeout&&!timeout.openFailure());}
    {auto request=config();request.caPem="synthetic-anchor";PoolTlsProcessTransport invalid(helper,request,cancel,nullptr,PoolTlsTransportMode::Plaintext);
        CHECK(invalid.open(request.host,1)!=PearlPoolConnectStatus::Connected&&invalid.error()==E::InvalidPolicy&&!invalid.openFailure());}
    {PoolTlsProcessTransport plain(helper,config("closed-read.test"),cancel,nullptr,PoolTlsTransportMode::Plaintext);
        CHECK(plain.open("closed-read.test",1)==PearlPoolConnectStatus::Connected);char byte;PearlPoolTransfer read;
        for(unsigned n=0;n<8;++n){read=plain.receive(&byte,1);if(read.status!=T::WouldBlock)break;plain.waitFor(read.interest,1000);}
        CHECK(read.status==T::EndOfStream&&!plain.isOpen()&&plain.error()==E::None&&!plain.openFailure());}
    {PoolTlsProcessTransport plain(helper,config("stall-open.test"),cancel,nullptr,PoolTlsTransportMode::Plaintext);
        std::thread stop([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));cancel=true;});const auto begin=std::chrono::steady_clock::now();
        CHECK(plain.open("stall-open.test",1)!=PearlPoolConnectStatus::Connected);stop.join();
        CHECK(plain.error()==E::Cancelled&&!plain.openFailure()&&std::chrono::steady_clock::now()-begin<std::chrono::seconds(2));cancel=false;
        CHECK(plain.open("stall-open.test",1)!=PearlPoolConnectStatus::Connected);}
    {PoolTlsProcessTransport plain(helper,config("lost-submit.test"),cancel,nullptr,PoolTlsTransportMode::Plaintext);
        PearlPoolSource source("lost-submit.test",1,"placeholder","fixture",{PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty},"",&plain);
        Job job;CHECK(source.fetch(&job)&&job.valid);Solution proof;proof.nonce=7;proof.hit[0]=11;proof.extra="synthetic-proof";std::string error;
        CHECK(!source.submit(job,proof,&error));PearlSourceEvent events[64];const auto count=source.pearlSourceObservability()->drain(events,64);unsigned unknown=0;
        for(std::size_t i=0;i<count;++i)if(events[i].delivery==PearlSourceDelivery::PossiblySent&&events[i].terminal==PearlSourceTerminal::Unknown)++unknown;
        CHECK(unknown==1&&!plain.isOpen()&&!plain.openFailure()&&source.submitCounters().accepted==0);}
}
void corruptOpenEvidence(const std::filesystem::path &helper){std::atomic<bool> cancel{false};
    for(const char *host:{"trailing-open.test","nonzero-open.test","stall-after-failure.test","healthy-open-failure.test"}){
        auto request=config(host);if(request.host=="stall-after-failure.test")request.totalMs=150;
        PoolTlsProcessTransport transport(helper,request,cancel);CHECK(transport.open(host,1)!=PearlPoolConnectStatus::Connected&&!transport.isOpen());
        if(request.host=="healthy-open-failure.test"){const auto failure=transport.openFailure();
            CHECK(failure&&failure->status==PoolTlsCheckStatus::Connect&&failure->stage==PoolTlsCheckStage::Connect);
        }else CHECK(!transport.openFailure());
    }
}
void absoluteOpenBudget(const std::filesystem::path &helper){std::atomic<bool> cancel{false};
    const auto now=[] {return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    for(uint64_t deadline:{now()-1,now()+10}){PoolTlsProcessTransport t(helper,config(),cancel);
        CHECK(t.openUntil("echo.test",1,deadline)!=PearlPoolConnectStatus::Connected&&t.error()==PearlTlsError::Timeout&&!t.openFailure());}
    {PoolTlsProcessTransport t(helper,config("stall-open.test"),cancel);const auto begin=now();
        CHECK(t.openUntil("stall-open.test",1,begin+150)!=PearlPoolConnectStatus::Connected&&t.error()==PearlTlsError::Timeout&&!t.openFailure()&&now()-begin<2000);}
    {PoolTlsProcessTransport t(helper,config(),cancel);const auto deadline=now()+100;
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        CHECK(t.openUntil("echo.test",1,deadline)!=PearlPoolConnectStatus::Connected&&t.error()==PearlTlsError::Timeout&&!t.openFailure());}
}
}
int main(int argc,char **argv){if(argc!=2&&argc!=3)return 2;
    const auto helper=std::filesystem::u8path(argv[1]);for(auto mode:{PoolTlsTransportMode::Tls,PoolTlsTransportMode::Plaintext}){roundTrip(helper,mode);canonicalFixture(helper,mode);}
    refusal(helper);openEvidenceAndPlaintext(helper);absoluteOpenBudget(helper);if(argc==3)corruptOpenEvidence(std::filesystem::u8path(argv[2]));
    return finishTests("persistent TLS/plaintext process transport (synthetic connection, real native IPC)");}
