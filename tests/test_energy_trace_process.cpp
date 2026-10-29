#include "energy_history_fixture.h"
#include "electricity_fixture.h"
#include "product/energy_trace_process.h"
#include "product/energy_recovery.h"
#include "product_test_util.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace om;
using namespace energy_history_fixture;
namespace {
using S=EnergyHistoryStatus;
EnergyTraceRequest query(const std::filesystem::path &file){EnergyTraceRequest q;q.source=request(file);q.source.recovery=EnergyRecoveryMode::Inspect;q.source.queries={{T,T+1000,"",""}};return q;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
std::filesystem::path suffix(std::filesystem::path p,const char *s){p+=s;return p;}
void create(EnergyHistoryProcessClient &writer,const std::filesystem::path &path){auto r=request(path,first());r.recovery=EnergyRecoveryMode::Submit;r.mode=EnergyHistoryOpen::CreateNew;r.queries.clear();std::atomic<bool> cancel{false};
    const auto result=writer.transact(r,cancel,5000);CHECK(result.reply&&result.reply->applied==S::Ok);}
template<class Predicate>bool until(Predicate test){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(test())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return test();}
std::string longKey(unsigned n){auto s="fixture."+std::to_string(n)+".";return s+std::string(128-s.size(),'x');}
void maximumEnvelope(const std::filesystem::path &path,bool compact,EnergyTraceProcessClient &reader){
    constexpr int64_t begin=253402300700000LL;constexpr uint64_t mono=INT64_MAX-4096;
    auto r=request(path);r.mode=EnergyHistoryOpen::CreateNew;r.recovery=EnergyRecoveryMode::Inspect;r.queries.clear();r.nowUtcMs=begin+1000;
    r.binding.historyBeginUtcMs=begin;r.binding.channels.clear();for(unsigned channel=0;channel<64;++channel)r.binding.channels.push_back(longKey(channel));
    r.binding.evidence.scope=longKey(64);r.binding.evidence.adapter=longKey(65);
    if(compact)r.limits.compaction=EnergyCompactionPolicy{4,32768};
    {EnergyRecoveryJournal journal;EnergyHistoryLedger ledger;
        CHECK(journal.open(r)==S::Ok&&ledger.open(path,r.binding,r.nowUtcMs,r.mode,r.limits)==S::Ok&&journal.initialize(ledger.checkpoint(r.nowUtcMs))==S::Ok);
        r.mode=EnergyHistoryOpen::Existing;r.recovery=EnergyRecoveryMode::Submit;
        for(unsigned frame=0,seq=1;frame<514;frame+=2,++seq){EnergyHistoryBatch input;input.sequence=seq;input.id=compact?"energy."+std::to_string(seq):longKey(seq+1000);input.sessionId=longKey(66);
            if(!frame)input.start=EnergyHistorySession{input.sessionId,begin,mono,60000};
            input.observedAtMs=begin+frame+1;
            for(unsigned at=frame;at<frame+2;++at)for(unsigned channel=0;channel<64;++channel)
                input.readings.push_back({channel,begin+at,mono+at,INT64_MAX,UINT32_MAX-(channel?0u:1u+at%2)});
            if(frame==512)for(unsigned i=0;i<32;++i)input.windows.push_back({longKey(i+100),longKey(i+200),longKey(i+300),begin,begin+1000,true,-64800});
            r.batch=input;CHECK(journal.prepare(r,ledger.checkpoint(r.nowUtcMs))==S::Ok&&ledger.apply(input,r.nowUtcMs)==S::Ok&&journal.settle(S::Ok)==S::Ok);
        }
    }
    const auto database=bytes(path),original=bytes(suffix(path,".energy-recovery.json"));EnergyTraceRequest q;q.source=r;q.source.batch.reset();q.source.recovery=EnergyRecoveryMode::Inspect;
    q.source.generation=q.source.ticket=UINT64_MAX;q.source.queries={{begin,begin+512,"",""}};std::atomic<bool> cancel{false};
    const auto result=reader.transact(q,cancel,5000);CHECK(result.transport==LocalTaskStatus::Ok&&result.reply&&result.reply->completeTrace);
    if(!result.reply||!result.reply->completeTrace)return;
    CHECK(result.reply->completeTrace->intervals.size()==512&&result.reply->source.recovery->batch->readings.size()==128&&result.reply->source.recovery->batch->windows.size()==32);
    CHECK(result.reply->completeTrace->intervals[0].watts.text()=="274877906.879"&&result.reply->completeTrace->intervals[1].watts.text()=="274877906.878");
    std::string wire;EnergyTraceReply decoded;CHECK(encodeEnergyTraceReply(q,*result.reply,&wire)&&wire.size()>40000&&wire.size()<=localTaskResponseLimit&&decodeEnergyTraceReply(q,wire,&decoded));
    CHECK(!decodeEnergyTraceReply(q,wire+std::string(localTaskResponseLimit,' '),&decoded));
    CHECK(bytes(path)==database&&bytes(suffix(path,".energy-recovery.json"))==original);
}
}
int main(int argc,char **argv){
    CHECK(argc==4||argc==6);if(argc!=4&&argc!=6)return finishTests("energy trace process");
    const auto writerPath=std::filesystem::absolute(argv[1]),readerPath=std::filesystem::absolute(argv[2]),fixturePath=std::filesystem::absolute(argv[3]);
    EnergyHistoryProcessClient writer(writerPath);EnergyTraceProcessClient reader(readerPath),fixture(fixturePath);std::atomic<bool> cancel{false};
    if(argc==6){const std::string mode=argv[4];const auto root=std::filesystem::absolute(argv[5]),file=root/"fixture.energy-trace-hang.db";
        if(mode=="parent-probe"){create(writer,file);fixture.transact(query(file),cancel,60000);return 9;}
        if(mode=="read-probe"){const auto result=reader.transact(query(file),cancel,5000);if(result.reply&&result.reply->completeTrace){std::cout<<"synthetic committed trace ready\n";return 0;}return 10;}
        return 2;
    }
    const auto root=std::filesystem::path(makeScratchDir("energy-trace-process")),file=root/"source.db",journal=suffix(file,".energy-recovery.json");
    create(writer,file);const auto original=bytes(file),saved=bytes(journal);auto q=query(file);std::string wire;uint64_t pid=0;EnergyTraceRequest decoded;
    CHECK(encodeEnergyTraceRequest(q,localTaskProcessId(),&wire)&&decodeEnergyTraceRequest(wire,&decoded,&pid)&&pid==localTaskProcessId()&&decoded.maxIntervals==512);
    CHECK(!encodeEnergyTraceRequest(q,0,&wire)&&!encodeEnergyTraceRequest(q,uint64_t{1}<<32,&wire));
    for(const auto &bad:std::vector<std::string>{"CMETQ1\n0\n512\n{}","CMETQ1\n01\n512\n{}","CMETQ1\n4294967296\n1\n{}","CMETQ1\n1\n513\n{}","CMETQ1\n1\n-1\n{}",std::string(energyTraceRequestLimit+1,'x')})CHECK(!decodeEnergyTraceRequest(bad,&decoded,&pid));
    for(unsigned kind=0;kind<10;++kind){auto bad=q;
        if(kind==0)bad.source.batch=first();
        if(kind==1)bad.source.mode=EnergyHistoryOpen::CreateNew;
        if(kind==2)bad.source.recovery=EnergyRecoveryMode::Volatile;
        if(kind==3)bad.source.queries.push_back(bad.source.queries[0]);
        if(kind==4)bad.source.queries[0].registerId="billing";
        if(kind==5)bad.maxIntervals=0;
        if(kind==6)bad.maxIntervals=513;
        if(kind==7)bad.source.mode=EnergyHistoryOpen::ExistingQuery;
        if(kind==8)bad.source.queries.clear();
        if(kind==9)bad.source.recovery=EnergyRecoveryMode::Retry;
        CHECK(!validEnergyTraceRequest(bad)&&reader.transact(bad,cancel,5000).transport==LocalTaskStatus::Invalid);}
    {LocalTaskProcess transport(readerPath);auto rejected=q.source;rejected.mode=EnergyHistoryOpen::CreateNew;std::string inner;CHECK(encodeEnergyHistoryRequest(rejected,&inner));
        CHECK(transport.run("CMETQ1\n"+std::to_string(localTaskProcessId())+"\n512\n"+inner,cancel,5000).status==LocalTaskStatus::Unavailable);
        rejected=q.source;rejected.batch=first();rejected.recovery=EnergyRecoveryMode::Submit;CHECK(encodeEnergyHistoryRequest(rejected,&inner));
        CHECK(transport.run("CMETQ1\n"+std::to_string(localTaskProcessId())+"\n512\n"+inner,cancel,5000).status==LocalTaskStatus::Unavailable);
#if defined(__linux__)
        CHECK(encodeEnergyTraceRequest(q,localTaskProcessId()==1?2:1,&inner));
        CHECK(transport.run(inner,cancel,5000).status==LocalTaskStatus::Unavailable);
#endif
        CHECK(bytes(file)==original&&bytes(journal)==saved);}
    const auto good=reader.transact(q,cancel,5000);CHECK(good.transport==LocalTaskStatus::Ok&&good.reply&&good.reply->status==S::Ok&&good.reply->completeTrace);
    CHECK(good.reply->completeTrace->intervals.size()==1&&good.reply->completeTrace->intervals[0].watts.text()=="123.456"&&good.reply->source.checkpoint.sequence==1);
    EnergyTraceReply parsed;CHECK(encodeEnergyTraceReply(q,*good.reply,&wire)&&decodeEnergyTraceReply(q,wire,&parsed));
    CHECK(!decodeEnergyTraceReply(q,wire+"\n",&parsed)&&!decodeEnergyTraceReply(q,"CMETA1\n999999\n{}",&parsed));
    CHECK(bytes(file)==original&&bytes(journal)==saved);
    for(unsigned kind=0;kind<12;++kind){auto bad=*good.reply;
        if(kind==0)bad.source.ticket++;
        if(kind==1)bad.completeTrace->intervals[0].beginUtcMs++;
        if(kind==2)bad.completeTrace->intervals[0].endUtcMs--;
        if(kind==3)bad.completeTrace->intervals[0].watts=electricity_fixture::d("123.457");
        if(kind==4)bad.completeTrace.reset();
        if(kind==5)bad.completeTrace->evidence.basis=PowerBasis::WholeMeterImport;
        if(kind==6)bad.completeTrace->intervals.clear();
        if(kind==7)bad.completeTrace->intervals[0].watts=electricity_fixture::d("-1");
        if(kind==8)bad.completeTrace->intervals[0].watts=electricity_fixture::d("123.4561");
        if(kind==9)bad.status=S::Replay;
        if(kind==10){const auto row=bad.completeTrace->intervals[0];bad.completeTrace->intervals={{row.beginUtcMs,row.beginUtcMs+1,row.watts},{row.beginUtcMs+1,row.endUtcMs,row.watts}};}
        if(kind==11)bad.source.recovery->state=EnergyRecoveryState::Prepared,bad.source.recovery->applied.reset();
        CHECK(!encodeEnergyTraceReply(q,bad,&wire));}
    auto partial=q;partial.source.queries[0].endUtcMs=T+3000;const auto incomplete=reader.transact(partial,cancel,5000);
    CHECK(incomplete.reply&&incomplete.reply->status==S::Ok&&!incomplete.reply->completeTrace&&incomplete.reply->source.totals[0].coverage==EnergyHistoryCoverage::Partial);
    {EnergyRecoveryJournal inspect;CHECK(inspect.open(q.source,EnergyRecoveryOpen::QueryOnly)==S::Ok);EnergyHistoryLedger source;
        CHECK(source.open(file,q.source.binding,q.source.nowUtcMs,EnergyHistoryOpen::ExistingQuery)==S::Ok);const auto before=inspect.inspect(source,q.source.nowUtcMs);
        auto next=request(file,second());next.recovery=EnergyRecoveryMode::Submit;
        CHECK(inspect.initialize(source.checkpoint(q.source.nowUtcMs))==S::Invalid&&inspect.prepare(next,source.checkpoint(q.source.nowUtcMs))==S::Invalid&&inspect.settle(S::Ok)==S::Invalid);
        CHECK(inspect.inspect(source,q.source.nowUtcMs).state==before.state&&bytes(journal)==saved);
        const auto locked=reader.transact(q,cancel,5000);CHECK(locked.reply&&locked.reply->status==S::Storage&&!locked.reply->completeTrace);}
    for(unsigned kind=0;kind<3;++kind){auto bad=q.source;
        if(kind==0)bad.mode=EnergyHistoryOpen::CreateNew;
        if(kind==1)bad.batch=first(),bad.recovery=EnergyRecoveryMode::Submit;
        if(kind==2)bad.recovery=EnergyRecoveryMode::Retry,bad.batch=first();
        EnergyRecoveryJournal inspect;CHECK(inspect.open(bad,EnergyRecoveryOpen::QueryOnly)==S::Invalid&&inspect.settle(S::Ok)==S::Invalid&&bytes(journal)==saved);}
    {const auto path=root/"empty.db";auto r=request(path);r.queries.clear();r.recovery=EnergyRecoveryMode::Inspect;r.mode=EnergyHistoryOpen::CreateNew;
        CHECK(executeEnergyHistoryRequest(r).opened==S::Ok);const auto empty=reader.transact(query(path),cancel,5000);
        CHECK(empty.reply&&empty.reply->status==S::Ok&&!empty.reply->completeTrace&&empty.reply->source.recovery->state==EnergyRecoveryState::Empty&&empty.reply->source.checkpoint.sequence==0);}
    {const auto path=root/"rejected.db";create(writer,path);auto r=request(path,second());r.recovery=EnergyRecoveryMode::Submit;r.batch->sessionId="fixture.not-active";
        const auto rejected=writer.transact(r,cancel,5000);CHECK(rejected.reply&&rejected.reply->applied==S::Conflict&&rejected.reply->recovery->state==EnergyRecoveryState::Rejected);
        const auto read=reader.transact(query(path),cancel,5000);CHECK(read.reply&&read.reply->status==S::Ok&&read.reply->completeTrace&&read.reply->source.checkpoint.sequence==1&&read.reply->source.recovery->state==EnergyRecoveryState::Rejected);}
    for(bool committed:{false,true}){const auto path=root/(committed?"committed.db":"prepared.db");create(writer,path);auto r=request(path,second());r.recovery=EnergyRecoveryMode::Submit;r.queries.clear();
        {EnergyRecoveryJournal recovery;CHECK(recovery.open(r)==S::Ok);EnergyHistoryLedger source;CHECK(source.open(path,r.binding,r.nowUtcMs)==S::Ok);
            CHECK(recovery.prepare(r,source.checkpoint(r.nowUtcMs))==S::Ok);if(committed)CHECK(source.apply(second(),r.nowUtcMs)==S::Ok);}
        const auto pending=bytes(suffix(path,".energy-recovery.json"));const auto read=reader.transact(query(path),cancel,5000);
        CHECK(read.reply&&read.reply->status==S::Conflict&&!read.reply->completeTrace&&read.reply->source.recovery->state==EnergyRecoveryState::Prepared&&bytes(suffix(path,".energy-recovery.json"))==pending);
        r.recovery=EnergyRecoveryMode::Retry;const auto retry=writer.transact(r,cancel,5000);CHECK(retry.reply&&(retry.reply->applied==S::Ok||retry.reply->applied==S::Replay));
        CHECK(reader.transact(query(path),cancel,5000).reply->completeTrace.has_value());}
    {const auto path=root/"diverged.db";create(writer,path);{EnergyHistoryLedger source;CHECK(source.open(path,binding(),T+20000)==S::Ok&&source.apply(second(),T+20000)==S::Ok);}
        const auto before=bytes(path),receipt=bytes(suffix(path,".energy-recovery.json"));const auto result=reader.transact(query(path),cancel,5000);
        CHECK(result.reply&&result.reply->status==S::Conflict&&!result.reply->completeTrace&&result.reply->source.recovery->state==EnergyRecoveryState::Diverged&&bytes(path)==before&&bytes(suffix(path,".energy-recovery.json"))==receipt);}
    {auto absent=query(root/"missing"/"ledger.db");CHECK(reader.transact(absent,cancel,5000).reply->status==S::Storage&&!std::filesystem::exists(absent.source.file.parent_path()));
        const auto path=root/"orphan.db";std::filesystem::copy_file(file,path);CHECK(reader.transact(query(path),cancel,5000).reply->status==S::Storage&&bytes(path)==original&&!std::filesystem::exists(suffix(path,".energy-recovery.json")));}
    {const auto path=root/"corrupt-journal.db";create(writer,path);const auto receipt=suffix(path,".energy-recovery.json");const auto source=bytes(path);
        {std::ofstream broken(receipt,std::ios::binary);broken<<"fixture.invalid";}
        const auto read=reader.transact(query(path),cancel,5000);CHECK(read.reply&&read.reply->status==S::Storage&&!read.reply->completeTrace&&bytes(path)==source&&bytes(receipt)=="fixture.invalid");}
    {const auto path=root/"missing-source.db";create(writer,path);const auto receipt=bytes(suffix(path,".energy-recovery.json"));std::filesystem::remove(path);
        const auto read=reader.transact(query(path),cancel,5000);CHECK(read.reply&&read.reply->status==S::Storage&&!std::filesystem::exists(path)&&bytes(suffix(path,".energy-recovery.json"))==receipt);}
    // Maximum chronological trace goes through the actual process boundary; no output-limit widening or averaging.
    for(bool compact:{false,true}){const auto path=root/(compact?"maximum-v2.db":"maximum-v1.db");auto r=request(path);r.binding.channels.resize(1);r.queries.clear();r.recovery=EnergyRecoveryMode::Submit;if(compact)r.limits.compaction=EnergyCompactionPolicy{2,32768};
        for(unsigned begin=0,seq=1;begin<514;begin+=128,++seq){EnergyHistoryBatch input;input.sequence=seq;input.id=compact?"energy."+std::to_string(seq):"fixture."+std::to_string(seq);input.sessionId="fixture.maximum";
            if(begin==0)input.start=EnergyHistorySession{input.sessionId,T,100,5000};
            for(unsigned i=begin;i<(std::min)(begin+128,514u);++i)input.readings.push_back(reading(0,i,i%2?1000u:0u));
            input.observedAtMs=input.readings.back().utcMs;r.batch=input;r.mode=begin?EnergyHistoryOpen::Existing:EnergyHistoryOpen::CreateNew;
            CHECK(executeEnergyHistoryRequest(r).applied==S::Ok);}
        auto request=query(path);request.source.binding=r.binding;request.source.limits=r.limits;request.source.queries[0].endUtcMs=T+512;
        auto result=reader.transact(request,cancel,5000);CHECK(result.reply&&result.reply->status==S::Ok&&result.reply->completeTrace->intervals.size()==512);
        CHECK(encodeEnergyTraceReply(request,*result.reply,&wire)&&wire.size()<localTaskResponseLimit);
        request.source.queries[0].endUtcMs++;result=reader.transact(request,cancel,5000);CHECK(result.reply&&result.reply->status==S::Capacity&&!result.reply->completeTrace);}
    for(bool compact:{false,true})maximumEnvelope(root/(compact?"envelope-v2.db":"envelope-v1.db"),compact,reader);
    cancel=true;CHECK(reader.transact(q,cancel,5000).transport==LocalTaskStatus::Cancelled);cancel=false;
    for(const auto *name:{"fixture.energy-trace-bad.db","fixture.energy-trace-exit.db","fixture.energy-trace-extra.db"}){const auto path=root/name;create(writer,path);const auto before=bytes(path);
        const auto result=fixture.transact(query(path),cancel,5000);CHECK(result.transport==LocalTaskStatus::Unavailable&&!result.reply&&bytes(path)==before);}
    {auto hang=query(root/"fixture.energy-trace-hang.db");create(writer,hang.source.file);CHECK(fixture.transact(hang,cancel,50).transport==LocalTaskStatus::Timeout);
        const auto marker=suffix(hang.source.file,".fixture-ready");std::filesystem::remove(marker);EnergyTraceProcessResult stopped;
        std::thread thread([&]{stopped=fixture.transact(hang,cancel,5000);});CHECK(until([&]{return !bytes(marker).empty();}));
        CHECK(fixture.transact(hang,cancel,5000).transport==LocalTaskStatus::Busy);cancel=true;thread.join();CHECK(stopped.transport==LocalTaskStatus::Cancelled&&!stopped.reply);cancel=false;
        CHECK(reader.transact(hang,cancel,5000).reply->completeTrace.has_value());}
    EnergyTraceProcessClient wrongWorker(writerPath);CHECK(wrongWorker.transact(q,cancel,5000).transport==LocalTaskStatus::Unavailable&&bytes(file)==original&&bytes(journal)==saved);
    std::filesystem::remove_all(root);return finishTests("energy trace process");
}
