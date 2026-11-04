#include "product/pool_tls_process_transport.h"
#include "core/pearl_pool.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
namespace {
using T=PearlPoolTransferStatus;using I=PearlPoolInterest;using W=PearlPoolWaitStatus;
PoolTlsCheckRequest config(std::string host="echo.test"){return {true,std::move(host),1,5000,{},{}};}
void roundTrip(const std::filesystem::path &helper){std::atomic<bool> cancel{false};PoolTlsProcessTransport t(helper,config(),cancel);
    CHECK(t.open("echo.test",1)==PearlPoolConnectStatus::Connected&&t.isOpen()&&t.progressIO());
    std::string original(32779,'x');for(std::size_t i=0;i<original.size();++i)original[i]=static_cast<char>(i%256);
    std::size_t sent=0;unsigned turns=0;while(sent<original.size()&&++turns<256){const auto r=t.send(original.data()+sent,original.size()-sent);
        CHECK(r.status!=T::Error);if(r.status==T::Error)break;if(r.status==T::Bytes)sent+=r.bytes;else if(r.status==T::WouldBlock)CHECK(t.waitFor(r.interest,1000)==W::Ready);}
    CHECK(sent==original.size()&&t.isOpen());std::string received;char bytes[97];turns=0;
    while(received.size()<original.size()&&++turns<1024){const auto r=t.receive(bytes,sizeof(bytes));CHECK(r.status!=T::Error);if(r.status==T::Error)break;
        if(r.status==T::Bytes)received.append(bytes,r.bytes);else if(r.status==T::WouldBlock)CHECK(t.waitFor(r.interest,1000)==W::Ready);}
    CHECK(received==original);t.close();CHECK(!t.isOpen()&&t.open("echo.test",1)!=PearlPoolConnectStatus::Connected);
}
void canonicalFixture(const std::filesystem::path &helper){std::atomic<bool> cancel{false};PoolTlsProcessTransport transport(helper,config("fixture.test"),cancel);
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
}
int main(int argc,char **argv){if(argc!=2)return 2;
    const auto helper=std::filesystem::u8path(argv[1]);roundTrip(helper);canonicalFixture(helper);refusal(helper);return finishTests("persistent TLS process transport (synthetic connection, real native IPC)");}
