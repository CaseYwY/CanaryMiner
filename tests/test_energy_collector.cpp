#include "energy_collector_fixture.h"
#include "product/energy_history_model.h"
#include "product/energy_leased_capture.h"
#include "product/energy_recovery.h"
#include "product/energy_review.h"
#include "product/local_store_lease.h"
#include "product_test_util.h"
using namespace om;
using namespace energy_collector_fixture;
namespace {
using S=EnergyHistoryStatus;
using A=EnergyAuthorizationAction;
using L=EnergyLeaseState;
using C=EnergyCaptureState;
EnergyHistoryProcessResult transact(const std::filesystem::path &helper,const std::filesystem::path &file,const std::string &epoch,const EnergyHistoryRequest &r){
    std::atomic<bool> cancel{false};EnergyHistoryProcessClient c(helper,EnergyCollectorFence{file,epoch});return c.transact(r,cancel,5000);
}
bool rejected(const EnergyHistoryProcessResult &r,S status=S::Attribution){return r.transport==LocalTaskStatus::Ok&&r.reply&&r.reply->opened==status&&!r.reply->applied;}
bool collecting(EnergyCaptureSink &c){return until([&]{c.tick(T,0);return c.snapshot()->state==C::Collecting;});}
int probe(int argc,char **argv){
    if(argc!=6)return 2;
    const auto authorization=std::filesystem::absolute(argv[2]),collector=std::filesystem::absolute(argv[3]),file=std::filesystem::absolute(argv[4]);
    const std::string action=argv[5];EnergyAuthorizationProcessClient client(authorization);auto doc=call(client,auth(file));
    if(doc.status==EnergyAuthorizationStatus::Missing)doc=enroll(client,file);
    if(doc.status!=EnergyAuthorizationStatus::Ok||!doc.document)return 3;
    Held held(collector,lease(file,doc));if(!held.wait()){std::puts("denied");return 0;}
    std::puts("held");std::fflush(stdout);
    if(action=="guard"){
        const auto result=transact(collector,file,held.reply.epoch,history(file,held.reply,true));
        std::puts(result.reply&&result.reply->applied==S::Ok?"written":"rejected");std::fflush(stdout);
    }else if(action=="once")return 0;
    else if(action!="hold")return 2;
    for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
}
int main(int argc,char **argv){
    if(argc>1&&std::string(argv[1])=="--probe")return probe(argc,argv);
    CHECK(argc==4);if(argc!=4)return finishTests("energy collector");
    const auto authorization=std::filesystem::absolute(argv[1]),collector=std::filesystem::absolute(argv[2]),fixture=std::filesystem::absolute(argv[3]);
    const auto root=std::filesystem::path(makeScratchDir("energy-collector"));EnergyAuthorizationProcessClient client(authorization);
    const auto file=root/"lease.json";auto doc=enroll(client,file);CHECK(doc.status==EnergyAuthorizationStatus::Ok&&doc.document);
    if(!doc.document)return finishTests("energy collector");
    const auto r=lease(file,doc);std::string wire;EnergyCollectorLeaseRequest decoded;
    CHECK(encodeEnergyCollectorLeaseRequest(r,&wire)&&decodeEnergyCollectorLeaseRequest(wire,&decoded));
    CHECK(!decodeEnergyCollectorLeaseRequest(wire+"x",&decoded)&&!decodeEnergyCollectorLeaseRequest(std::string(energyAuthorizationLimit+1,' '),&decoded));
    for(unsigned n=0;n<5;++n){auto bad=r;if(n==0)bad.parentPid=0;if(n==1)bad.parentPid=UINT64_MAX;if(n==2)bad.provision.action=A::Load;
        if(n==3)bad.provision.file="relative";
        if(n==4)bad.provision.devices.clear();
        CHECK(!encodeEnergyCollectorLeaseRequest(bad,&wire));}
    CHECK(!localStoreLeaseHeld(root/"absent.lock").has_value()&&!std::filesystem::exists(root/"absent.lock"));
    {LocalStoreLease l;CHECK(l.acquire(root/"probe.lock"));CHECK(localStoreLeaseHeld(root/"probe.lock")==true);}
    CHECK(localStoreLeaseHeld(root/"probe.lock")==false);
    Held first(collector,r);CHECK(first.wait());if(!first.ready)return finishTests("energy collector");
    CHECK(localStoreLeaseHeld(suffix(file,".collector.lock"))==true);const auto firstBytes=bytes(file),firstFence=bytes(suffix(file,".collector.json"));
    CHECK(firstFence.find(devices[0])==std::string::npos&&firstFence.find(devices[1])==std::string::npos);
    EnergyCollectorLeaseReply parsed;CHECK(encodeEnergyCollectorLeaseReply(r,first.reply,&wire)&&decodeEnergyCollectorLeaseReply(r,wire,&parsed));
    auto other=r;other.parentPid++;CHECK(!decodeEnergyCollectorLeaseReply(other,wire,&parsed));other=r;other.provision.ticket++;CHECK(!decodeEnergyCollectorLeaseReply(other,wire,&parsed));
    auto forged=first.reply;forged.epoch="bad";CHECK(!encodeEnergyCollectorLeaseReply(r,forged,&wire));
    // An independently spawned contender cannot reserve another session while the first collector is alive.
    {Held second(collector,lease(file,call(client,auth(file))));CHECK(!second.wait()&&second.done);CHECK(bytes(file)==firstBytes&&bytes(suffix(file,".collector.json"))==firstFence);}
    auto h=history(file,first.reply);auto result=transact(collector,file,first.reply.epoch,h);CHECK(result.reply&&result.reply->checkpoint.sequence==0);
    EnergyCollectorGuardRequest guard{localTaskProcessId(),{file,first.reply.epoch},history(file,first.reply,true)},decodedGuard;
    CHECK(encodeEnergyCollectorGuardRequest(guard,&wire)&&decodeEnergyCollectorGuardRequest(wire,&decodedGuard));
    CHECK(!decodeEnergyCollectorGuardRequest(wire+"x",&decodedGuard)&&!decodeEnergyCollectorGuardRequest(std::string(energyHistoryRequestLimit+1,' '),&decodedGuard));
    for(unsigned n=0;n<5;++n){auto bad=guard;if(n==0)bad.parentPid=0;if(n==1)bad.fence.epoch=std::string(32,'G');
        if(n==2)bad.history.mode=EnergyHistoryOpen::CreateNew;
        if(n==3)bad.history.recovery=EnergyRecoveryMode::Volatile;
        if(n==4)bad.fence.authorizationFile="relative";
        CHECK(!encodeEnergyCollectorGuardRequest(bad,&wire));}
    for(unsigned n=0;n<8;++n){auto bad=guard.history;auto epoch=first.reply.epoch;
        if(n==0)epoch=std::string(32,'f');
        if(n==1)bad.file=root/"unrelated.db";
        if(n==2)bad.binding.evidence.scope="different";
        if(n==3)--bad.limits.maxSessions;
        if(n==4){bad.batch->sessionId="session.00000000000000000000000000000000";bad.batch->start->id=bad.batch->sessionId;}
        if(n==5)++bad.batch->start->maximumGapMs;
        if(n==6)bad.binding.historyBeginUtcMs--;
        if(n==7){for(unsigned i=0;i<33;++i)bad.batch->readings.push_back({0,T+1000,1000,0,1});}
        CHECK_MSG("mismatched fence or frozen input rejected",rejected(transact(collector,file,epoch,bad)));}
    h.binding.historyBeginUtcMs=T-100;h.nowUtcMs=T-1; // codec remains valid; binding mismatch takes precedence
    CHECK(rejected(transact(collector,file,first.reply.epoch,h)));
    {LocalStoreLease op;CHECK(op.acquire(suffix(file,".collector-operation.lock")));CHECK(rejected(transact(collector,file,first.reply.epoch,guard.history),S::Storage));}
    const auto journal=suffix(guard.history.file,".energy-recovery.json");const auto before=bytes(journal);
    first.stop();CHECK(first.result.status==LocalTaskStatus::Cancelled&&localStoreLeaseHeld(suffix(file,".collector.lock"))==false);
    CHECK(rejected(transact(collector,file,first.reply.epoch,guard.history))&&bytes(journal)==before);
    // A new holder publishes a fresh epoch/session under the same operation mutex as every history transaction.
    {LocalStoreLease op;CHECK(op.acquire(suffix(file,".collector-operation.lock")));Held blocked(collector,lease(file,call(client,auth(file))));
        CHECK(!blocked.wait()&&bytes(file)==firstBytes&&bytes(suffix(file,".collector.json"))==firstFence);}
    Held successor(collector,lease(file,call(client,auth(file))));CHECK(successor.wait());if(!successor.ready)return finishTests("energy collector");
    CHECK(successor.reply.epoch!=first.reply.epoch&&successor.reply.provision.document->lastSessionId!=first.reply.provision.document->lastSessionId);
    CHECK(rejected(transact(collector,file,first.reply.epoch,guard.history))&&bytes(journal)==before);
    result=transact(collector,file,successor.reply.epoch,history(file,successor.reply,true));CHECK(result.reply&&result.reply->applied==S::Ok);
    successor.stop();
    // Corrupt or rolled-back fence/binding cannot be overwritten by another provisioning attempt.
    {const auto current=call(client,auth(file));const auto saved=bytes(file);put(suffix(file,".collector.json"),"{}");Held broken(collector,lease(file,current));
        CHECK(!broken.wait()&&bytes(file)==saved&&bytes(suffix(file,".collector.json"))=="{}");}
    {const auto p=root/"clock.json";auto q=lease(p,enroll(client,p));q.provision.nowUtcMs=T+5000;Held active(collector,q);CHECK(active.wait());
        if(!active.ready)return finishTests("energy collector");
        auto past=history(p,active.reply);past.nowUtcMs=T+4999;CHECK(rejected(transact(collector,p,active.reply.epoch,past),S::Clock));
        past=history(p,active.reply,true);CHECK(rejected(transact(collector,p,active.reply.epoch,past)));}
    // Full native selected set and maximal structured history envelope stay inside the original protocol budgets.
    {const auto p=root/"maximum.json";auto q=auth(p,A::Enroll,"absent");q.policy.batchReadings=128;q.devices.clear();
        for(unsigned i=0;i<64;++i){char uuid[64];std::snprintf(uuid,sizeof(uuid),"GPU-00000000-0000-0000-0000-%012u",i);q.devices.emplace_back(uuid);}
        auto d=call(client,q);CHECK(d.status==EnergyAuthorizationStatus::Ok&&d.document);if(!d.document)return finishTests("energy collector");
        auto save=auth(p,A::Save,d.token);save.policy=d.document->policy;save.policy.persistenceAuthorized=true;save.limits=d.document->limits;d=call(client,save);
        auto start=lease(p,d);start.provision.devices=q.devices;CHECK(encodeEnergyCollectorLeaseRequest(start,&wire)&&wire.size()<=energyAuthorizationLimit);
        Held active(collector,start);CHECK(active.wait());if(!active.ready)return finishTests("energy collector");
        CHECK(encodeEnergyCollectorLeaseReply(start,active.reply,&wire)&&wire.size()<energyAuthorizationLimit);
        auto maximum=history(p,active.reply,true);maximum.batch->readings.clear();
        for(unsigned i=0;i<128;++i)maximum.batch->readings.push_back({i%64,T+static_cast<int64_t>(i/64)*1000,static_cast<uint64_t>(i/64)*1000,0,UINT32_MAX});
        for(unsigned i=0;i<32;++i)maximum.batch->windows.push_back({"register."+std::to_string(i)+std::string(110,'r'),std::string(128,'p'),std::string(128,'v'),T,T+10000,false,0});
        EnergyCollectorGuardRequest full{localTaskProcessId(),{p,active.reply.epoch},maximum};
        CHECK(encodeEnergyCollectorGuardRequest(full,&wire)&&wire.size()<=energyHistoryRequestLimit&&decodeEnergyCollectorGuardRequest(wire,&decodedGuard));
        result=transact(collector,p,active.reply.epoch,maximum);CHECK(result.reply&&result.reply->applied==S::Ok&&result.reply->checkpoint.readingCount==128);
        full.history.file=root/std::string(4000,'x');full.fence.authorizationFile=root/std::string(4000,'y');
        CHECK(encodeEnergyCollectorGuardRequest(full,&wire)&&wire.size()<=energyHistoryRequestLimit);}
    // A delayed request reaches validation only after takeover. No prepared intent or credit may be introduced.
    {const auto p=root/"fixture-guard-delay.json";auto d=enroll(client,p);Held old(fixture,lease(p,d));CHECK(old.wait());if(!old.ready)return finishTests("energy collector");
        const auto q=history(p,old.reply,true);EnergyHistoryProcessResult delayed;std::thread flight([&]{delayed=transact(fixture,p,old.reply.epoch,q);});
        CHECK(until([&]{return std::filesystem::exists(suffix(p,".fixture-ready"));}));old.stop();
        Held fresh(collector,lease(p,call(client,auth(p))));CHECK(fresh.wait());put(suffix(p,".fixture-release"),"1");flight.join();
        CHECK(rejected(delayed));const auto inspected=transact(collector,p,fresh.reply.epoch,history(p,fresh.reply));
        CHECK(inspected.reply&&inspected.reply->checkpoint.sequence==0&&inspected.reply->recovery->state==EnergyRecoveryState::Empty);}
    // An already admitted operation keeps its lock through commit; a successor cannot publish a fence midway.
    {const auto p=root/"fixture-guard-in-operation.json";Held old(fixture,lease(p,enroll(client,p)));CHECK(old.wait());if(!old.ready)return finishTests("energy collector");
        auto q=history(p,old.reply,true);EnergyHistoryProcessResult pending;std::thread flight([&]{pending=transact(fixture,p,old.reply.epoch,q);});
        CHECK(until([&]{return std::filesystem::exists(suffix(p,".fixture-ready"));}));old.stop();const auto saved=bytes(p);
        {Held contender(collector,lease(p,call(client,auth(p))));CHECK(!contender.wait()&&bytes(p)==saved);}
        put(suffix(p,".fixture-release"),"1");flight.join();CHECK(pending.reply&&pending.reply->applied==S::Ok);
        auto takeover=lease(p,call(client,auth(p)));takeover.provision.nowUtcMs=T+10000;Held fresh(collector,takeover);CHECK(fresh.wait());}
    // A new lease can retry only the exact Prepared original from an older session, never treat it as new sampling authority.
    {const auto p=root/"prepared.json";Held old(collector,lease(p,enroll(client,p)));CHECK(old.wait());if(!old.ready)return finishTests("energy collector");
        auto q=history(p,old.reply,true);old.stop();
        {EnergyRecoveryJournal j;CHECK(j.open(q)==S::Ok);EnergyHistoryLedger db;CHECK(db.open(q.file,q.binding,q.nowUtcMs,EnergyHistoryOpen::Existing,q.limits)==S::Ok);
            CHECK(j.prepare(q,db.checkpoint(q.nowUtcMs))==S::Ok);}
        auto takeover=lease(p,call(client,auth(p)));takeover.provision.nowUtcMs=T+10000;
        Held fresh(collector,takeover);CHECK_MSG("Prepared takeover at or after its observed time",fresh.wait());if(!fresh.ready)return finishTests("energy collector");
        CHECK(rejected(transact(collector,p,fresh.reply.epoch,q)));q.recovery=EnergyRecoveryMode::Retry;
        auto changed=q;changed.batch->readings[0].milliwatts=1;result=transact(collector,p,fresh.reply.epoch,changed);CHECK(result.reply&&result.reply->applied==S::Conflict);
        result=transact(collector,p,fresh.reply.epoch,q);CHECK(result.reply&&result.reply->applied==S::Ok&&result.reply->recovery->state==EnergyRecoveryState::Confirmed);
        result=transact(collector,p,fresh.reply.epoch,q);CHECK(result.reply&&result.reply->applied==S::Replay);}
    // Stop/CLI/missing authorization cannot create identities. Wrapper admission is one-shot.
    {auto capture=makeLeasedEnergyCapture(authorization,collector,root/"never.json");capture->requestStop();CHECK(!capture->start(1,devices)&&!std::filesystem::exists(root/"never.json"));}
    {auto capture=makeLeasedEnergyCapture(authorization,collector,root/"disabled.json",true);CHECK(!capture->start(1,devices)&&!std::filesystem::exists(root/"disabled.json"));}
    {auto capture=makeLeasedEnergyCapture(authorization,collector,root/"missing.json",false,5000,[]{return T;});CHECK(capture->start(1,devices));
        CHECK(until([&]{return capture->snapshot()->leaseState==L::Disabled;}));CHECK(!capture->start(2,devices)&&!std::filesystem::exists(root/"missing.json"));}
    {const auto p=root/"capture.json";CHECK(enroll(client,p).status==EnergyAuthorizationStatus::Ok);
        std::atomic<int64_t> clock{T+10000};auto capture=makeLeasedEnergyCapture(authorization,collector,p,false,5000,[&]{return clock.load();});CHECK(capture->start(9,devices));
        CHECK(until([&]{capture->tick(T+10000,0);return capture->snapshot()->state==C::Collecting;}));
        CHECK(capture->snapshot()->leaseState==L::Held&&!capture->snapshot()->provisioningOutcomeUnknown);
        CHECK(capture->submit(8,{0,T+10000,0,100000})==EnergyCaptureAdmission::StaleGeneration);
        CHECK(capture->submit(9,{0,T+10000,0,100000})==EnergyCaptureAdmission::Buffered);
        CHECK(capture->submit(9,{0,T+11000,1000,0})==EnergyCaptureAdmission::Buffered);
        clock=T+20000;CHECK(until([&]{capture->tick(T+12000,2000);const auto s=capture->snapshot();return s->owner&&s->owner->reply&&s->owner->reply->checkpoint.sequence==1;}));
        capture->requestStop();const auto rev=capture->snapshot()->revision;capture->stop();CHECK(capture->snapshot()->revision==rev&&capture->snapshot()->leaseState==L::Stopped);
        CHECK(capture->submit(9,{0,T+12000,2000,0})==EnergyCaptureAdmission::Stopped&&!capture->start(10,devices));}
    for(const auto *name:{"fixture-lease-no-ack.json","fixture-lease-exit.json","fixture-lease-extra.json"}){
        const auto p=root/name;CHECK(enroll(client,p).status==EnergyAuthorizationStatus::Ok);
        auto capture=makeLeasedEnergyCapture(authorization,fixture,p,false,300,[]{return T;});CHECK(capture->start(1,devices));
        if(std::string(name)=="fixture-lease-exit.json"){CHECK(collecting(*capture));put(suffix(p,".fixture-exit"),"1");}
        CHECK(until([&]{return capture->snapshot()->leaseState==L::Lost;}));CHECK(capture->snapshot()->state==C::Faulted&&!capture->start(2,devices));
        if(std::string(name)=="fixture-lease-no-ack.json")CHECK(capture->snapshot()->provisioningOutcomeUnknown&&capture->snapshot()->provisionRequest);
        capture->stop();CHECK(localStoreLeaseHeld(suffix(p,".collector.lock"))==false);}
    {const auto dir=root/"stop-pending";std::filesystem::create_directory(dir);const auto p=dir/"fixture-lease-no-ack.json";enroll(client,p);
        auto capture=makeLeasedEnergyCapture(authorization,fixture,p,false,5000,[]{return T;});CHECK(capture->start(1,devices));
        CHECK(until([&]{return std::filesystem::exists(suffix(p,".collector.json"));}));const auto start=std::chrono::steady_clock::now();capture->requestStop();
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));const auto stopped=capture->snapshot();
        CHECK(stopped->provisioningOutcomeUnknown&&stopped->provisionRequest&&stopped->provisionRequest->devices==devices);
        EnergyReviewPanel panel;panel.open(stopped);auto text=panel.render(120,0,ColorLevel::None);CHECK(text.find("UNKNOWN")!=std::string::npos&&text.find(devices[0])==std::string::npos);
        panel.key('i');text=panel.render(120,0,ColorLevel::None);CHECK(text.find("ORIGINAL reservation")!=std::string::npos&&text.find(devices[0])!=std::string::npos);
        for(int width:{20,40,80,120})for(int rows:{12,24,40}){const auto rendered=panel.render(width,rows,ColorLevel::None);size_t line=0,count=0;
            for(char ch:rendered){if(ch=='\n'){CHECK(line<static_cast<size_t>(width));line=0;++count;}else ++line;}CHECK(count<=static_cast<size_t>(rows));}
        capture->stop();CHECK(capture->snapshot()->revision==stopped->revision&&!capture->start(2,devices));}
    std::filesystem::remove_all(root);return finishTests("energy collector");
}
