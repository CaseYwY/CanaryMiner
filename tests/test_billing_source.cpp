#include "billing_source_fixture.h"
#include "product/energy_recovery.h"
#include "product/billing_recovery.h"
#include "product/billing_model.h"
#include "product/billing_query_process.h"
#include "product/billing_retry_process.h"
#include "product_test_util.h"
#include <fstream>
using namespace billing_source_fixture;
namespace {
using S=BillingStatus;using E=EnergyHistoryStatus;
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
BillingChunk import(BillingLedger &ledger,EnergyTraceProcessClient &reader,int64_t end,uint64_t sequence){
    const auto q=ledger.sourceQuery(end,now);CHECK(q.has_value());if(!q)return {};
    auto issued=*q;issued.source.generation=123;issued.source.ticket=456;std::atomic<bool> cancel{false};const auto reply=reader.transact(issued,cancel,5000);
    CHECK(reply.reply&&reply.reply->status==E::Ok&&reply.reply->completeTrace&&reply.reply->anchorStatus==E::Ok);if(!reply.reply)return {};
    BillingChunk c;CHECK(makeBillingSourceChunk(sequence,issued,*reply.reply,&c)==S::Ok);
    CHECK(c.receipt&&c.receipt->request.source.generation==1&&c.receipt->reply.source.ticket==1);return c;
}
std::string longKey(unsigned n){auto s="fixture."+std::to_string(n)+".";return s+std::string(128-s.size(),'x');}
void maximumReceipt(const std::filesystem::path &root,EnergyTraceProcessClient &reader,BillingQueryProcessClient *monetary,BillingRetryProcessClient *retry){
    // Keep 15-digit UTC fields while leaving the next civil midnight inside the supported year range.
    constexpr int64_t begin=253402300700000LL-86400000;constexpr uint64_t mono=INT64_MAX-4096;auto b=binding(root/"maximum-source.db",true);
    b.cycleBeginUtcMs=begin;b.cycleEndUtcMs=begin+512;b.plan=electricity_fixture::plan(begin,begin+512);b.evidence.scope=longKey(64);b.evidence.adapter=longKey(65);
    b.source->binding={b.evidence,{},begin};for(unsigned channel=0;channel<64;++channel)b.source->binding.channels.push_back(longKey(channel));b.source->limits.compaction->afterBatches=4;
    auto r=input(b,1);r.batch.reset();r.recovery=EnergyRecoveryMode::Inspect;r.nowUtcMs=begin+1000;
    {EnergyHistoryLedger ledger;EnergyRecoveryJournal journal;
        CHECK(journal.open(r)==E::Ok&&ledger.open(r.file,r.binding,r.nowUtcMs,EnergyHistoryOpen::CreateNew,r.limits)==E::Ok&&journal.initialize(ledger.checkpoint(r.nowUtcMs))==E::Ok);
        r.mode=EnergyHistoryOpen::Existing;r.recovery=EnergyRecoveryMode::Submit;
        for(unsigned frame=0,seq=1;frame<514;frame+=2,++seq){EnergyHistoryBatch in;in.sequence=seq;in.id="energy."+std::to_string(seq);in.sessionId=longKey(66);in.observedAtMs=begin+frame+1;
            if(!frame)in.start=EnergyHistorySession{in.sessionId,begin,mono,60000};
            for(unsigned t=frame;t<frame+2;++t)for(unsigned channel=0;channel<64;++channel)in.readings.push_back({channel,begin+t,mono+t,INT64_MAX,UINT32_MAX-(channel?0u:1u+t%2)});
            if(frame==512)for(unsigned i=0;i<32;++i)in.windows.push_back({longKey(i+100),longKey(i+200),longKey(i+300),begin,begin+1000,true,-64800});
            r.batch=in;CHECK(journal.prepare(r,ledger.checkpoint(r.nowUtcMs))==E::Ok&&ledger.apply(in,r.nowUtcMs)==E::Ok&&journal.settle(E::Ok)==E::Ok);
        }
    }
    const auto file=root/"maximum-bill.db";BillingChunk original;
    {BillingLedger ledger;CHECK(ledger.open(file,b,r.nowUtcMs,BillingOpen::CreateNew)==S::Ok);const auto q=ledger.sourceQuery(begin+512,r.nowUtcMs);CHECK(q.has_value());
        std::atomic<bool> cancel{false};const auto reply=reader.transact(*q,cancel,5000);CHECK(reply.reply&&makeBillingSourceChunk(1,*q,*reply.reply,&original)==S::Ok);
        CHECK(original.trace.intervals.size()==512&&original.receipt->reply.source.recovery->batch->readings.size()==128&&original.receipt->reply.source.recovery->batch->windows.size()==32);
        CHECK(ledger.apply(original,r.nowUtcMs)==S::Ok&&ledger.snapshot(r.nowUtcMs).settledTotal.has_value());}
    BillingLedger inspect;CHECK(inspect.open(file,b,r.nowUtcMs,BillingOpen::Inspect)==S::Ok&&inspect.verifyCommitted(original,r.nowUtcMs)==S::Replay&&inspect.snapshot(r.nowUtcMs).source->checkpoint.sequence==257);
    const auto recovered=root/"maximum-recovery.db";auto saved=recovered;saved+=".billing-recovery";
    {BillingRecoveryStore store;CHECK(store.open(recovered,b,r.nowUtcMs,BillingRecoveryOpen::CreateNew)==S::Ok&&store.submit(original,r.nowUtcMs)==S::Ok);}
    const auto frozen=bytes(saved);CHECK(frozen.size()>64*1024&&frozen.size()<256*1024);
    {BillingRecoveryStore store;CHECK(store.open(recovered,b,r.nowUtcMs,BillingRecoveryOpen::QueryOnly)==S::Ok);const auto s=store.inspect(r.nowUtcMs);
        CHECK(s.state==BillingRecoveryState::Confirmed&&s.committed.source->checkpoint.sequence==257&&billing_detail::chunkText(*s.original)==billing_detail::chunkText(original)&&bytes(saved)==frozen);}
    if(monetary){BillingQueryRequest q;q.generation=q.ticket=1;q.file=recovered;q.binding=b;q.nowUtcMs=r.nowUtcMs;std::atomic<bool> cancel{false};const auto read=monetary->transact(q,cancel,10000);
        CHECK(read.reply&&read.reply->state==BillingRecoveryState::Confirmed&&read.reply->original&&billing_detail::chunkText(*read.reply->original)==billing_detail::chunkText(original)&&bytes(saved)==frozen);
        if(retry&&read.reply){const auto replay=retry->transact(q,*read.reply,cancel,10000);CHECK(replay.applied==S::Replay&&replay.confirmed&&replay.confirmed->revision==read.reply->revision&&bytes(saved)==frozen);}}
}
}
int main(int argc,char **argv){
    CHECK(argc>=2&&argc<=4);if(argc<2||argc>4)return finishTests("billing source");
    std::unique_ptr<BillingQueryProcessClient> monetary;if(argc>=3)monetary=std::make_unique<BillingQueryProcessClient>(std::filesystem::absolute(argv[2]));
    std::unique_ptr<BillingRetryProcessClient> retry;if(argc==4)retry=std::make_unique<BillingRetryProcessClient>(std::filesystem::absolute(argv[3]));
    EnergyTraceProcessClient reader(std::filesystem::absolute(argv[1]));std::atomic<bool> cancel{false};const auto root=std::filesystem::path(makeScratchDir("billing-source"));
    for(bool compact:{false,true}){const auto dir=root/(compact?"v2":"v1");std::filesystem::create_directories(dir);const auto file=dir/"bill.db";auto b=binding(dir/"energy.db",compact);
        CHECK(executeEnergyHistoryRequest(input(b,1)).applied==E::Ok);BillingChunk first,second;BillingSnapshot frozen;
        {BillingLedger ledger;CHECK(ledger.open(file,b,now,BillingOpen::CreateNew)==S::Ok);const auto empty=ledger.snapshot(now);
            CHECK(empty.source&&empty.source->checkpoint.sequence==0&&!empty.provisionalTotal);
            CHECK(!ledger.sourceQuery(T,now)&&!ledger.sourceQuery(T+4001,now)&&!ledger.sourceQuery(T+500,T)&&!ledger.sourceQuery(T+500,now,0));
            first=import(ledger,reader,T+500,1);CHECK(ledger.apply(first,now-1)==S::Clock&&ledger.apply(first,now)==S::Ok);
            frozen=ledger.snapshot(now);CHECK(frozen.source->checkpoint.sequence==1&&frozen.observedUtcMs==T+1000&&frozen.provisionalTotal->numerator.text()=="185184");
            CHECK(ledger.apply(first,now)==S::Replay&&ledger.snapshot(now).digest==frozen.digest);
            second=import(ledger,reader,T+1000,2);CHECK(ledger.apply(second,now)==S::Ok&&ledger.snapshot(now).provisionalTotal->numerator.text()=="370368");
            CHECK(ledger.snapshot(now-1).status==S::Clock&&ledger.verifyCommitted(first,now-1)==S::Clock&&!ledger.sourceQuery(T+2000,now-1));
            auto bad=second;bad.receipt.reset();CHECK(ledger.apply(bad,now)==S::Attribution);
            bad=second;bad.receipt->request.source.file=dir/"other.db";CHECK(ledger.apply(bad,now)==S::Conflict);
            CHECK(frozen.opening.throughUtcMs==T+500&&frozen.source->checkpoint.sequence==1);}
        for(unsigned seq=2;seq<=4;++seq){CHECK(executeEnergyHistoryRequest(input(b,seq)).applied==E::Ok);
            BillingLedger ledger;CHECK(ledger.open(file,b,now)==S::Ok);auto chunk=import(ledger,reader,T+seq*1000,seq+1);
            if(seq==2){auto bad=chunk;bad.receipt->request.anchor->digest=std::string(64,'a');CHECK(ledger.apply(bad,now)==S::Conflict);
                bad=chunk;bad.trace.intervals[0].watts=d("1");CHECK(ledger.apply(bad,now)==S::Attribution);
                bad=chunk;bad.observedUtcMs++;CHECK(ledger.apply(bad,now)==S::Clock);
                // A source-checked chunk prepared from the seed cannot bypass the monetary ledger's last source anchor.
                const auto reset=query(b,{0,sha256Hex(energy_history_detail::bindingText(b.source->binding))},T+1000,T+2000);const auto resetReply=executeEnergyTraceRequest(reset);
                CHECK(makeBillingSourceChunk(3,reset,resetReply,&bad)==S::Ok&&ledger.apply(bad,now)==S::Conflict);}
            CHECK(ledger.apply(chunk,now)==S::Ok);const auto current=ledger.snapshot(now);
            CHECK(current.source->checkpoint.sequence==seq&&current.source->checkpoint.digest==chunk.receipt->reply.source.checkpoint.digest);
            if(seq==4)CHECK(current.settledTotal&&current.settledTotal->numerator.text()=="3070368"&&!ledger.sourceQuery(T+4000,now));
        }
        const auto original=bytes(file);
        {BillingLedger inspect;CHECK(inspect.open(file,b,now,BillingOpen::Inspect)==S::Ok&&inspect.verifyCommitted(first,now)==S::Replay&&inspect.verifyCommitted(second,now)==S::Replay);
            CHECK(inspect.apply(first,now)==S::Unsupported&&inspect.snapshot(now).source->checkpoint.sequence==4);}
        CHECK(bytes(file)==original);
        {BillingLedger past;CHECK(past.open(file,b,now-1,BillingOpen::Inspect)==S::Clock&&bytes(file)==original);}
        for(unsigned variant=0;variant<5;++variant){auto wrong=b;
            if(variant==0)wrong.source.reset();
            if(variant==1)wrong.source->file=dir/"other.db";
            if(variant==2)wrong.source->binding.channels[0]="fixture.other-channel";
            if(variant==3)wrong.source->limits.maxBatches=128;
            if(variant==4)wrong.source->binding.historyBeginUtcMs--;
            BillingLedger ledger;CHECK(ledger.open(file,wrong,now,BillingOpen::Inspect)==(variant==0?S::Storage:S::Attribution)&&bytes(file)==original);}
        std::filesystem::remove(b.source->file);std::filesystem::remove(journal(b.source->file)); // owned fixture only: committed receipt replay needs no live source
        BillingLedger offline;CHECK(offline.open(file,b,now,BillingOpen::Inspect)==S::Ok&&offline.verifyCommitted(first,now)==S::Replay&&offline.snapshot(now).settledTotal->numerator.text()=="3070368");
    }
    // Audit source lineage through raw rows, the compacted checkpoint and its retained immediate predecessor.
    for(bool compact:{false,true}){auto b=binding(root/(compact?"anchors-v2.db":"anchors-v1.db"),compact);std::vector<EnergyHistoryAnchor> anchors{{0,sha256Hex(energy_history_detail::bindingText(b.source->binding))}};
        for(unsigned seq=1;seq<=5;++seq){const auto result=executeEnergyHistoryRequest(input(b,seq));CHECK(result.applied==E::Ok);anchors.push_back(anchor(result.checkpoint));}
        for(unsigned seq=0;seq<=5;++seq){const auto q=query(b,anchors[seq]);const auto result=reader.transact(q,cancel,5000);const auto expected=compact&&seq>0&&seq<3?E::DetailExpired:E::Ok;
            CHECK(result.reply&&result.reply->status==expected&&result.reply->anchorStatus==expected&&result.reply->completeTrace.has_value()==(expected==E::Ok));
            if(result.reply){std::string wire;EnergyTraceReply decoded;CHECK(encodeEnergyTraceReply(q,*result.reply,&wire)&&decodeEnergyTraceReply(q,wire,&decoded));
                auto legacy=q;legacy.anchor.reset();CHECK(!decodeEnergyTraceReply(legacy,wire,&decoded));}
            if(expected==E::Ok){auto bad=q;bad.anchor->digest=std::string(64,'f');const auto conflict=reader.transact(bad,cancel,5000);
                CHECK(conflict.reply&&conflict.reply->status==E::Conflict&&!conflict.reply->completeTrace);}}
        {EnergyHistoryLedger source;CHECK(source.open(b.source->file,b.source->binding,now,EnergyHistoryOpen::ExistingQuery,b.source->limits)==E::Ok);
            CHECK(source.verifyAnchor(anchors[5],now)==E::Ok&&source.verifyAnchor({6,anchors[5].digest},now)==E::Conflict&&source.verifyAnchor({0,"bad"},now)==E::Invalid&&source.verifyAnchor(anchors[5],T)==E::Clock);}
        const auto unknown=query(b,{6,anchors[5].digest});const auto result=reader.transact(unknown,cancel,5000);CHECK(result.reply&&result.reply->status==E::Conflict&&!result.reply->completeTrace);
        auto invalid=query(b,anchors[1]);invalid.anchor->digest="bad";CHECK(reader.transact(invalid,cancel,5000).transport==LocalTaskStatus::Invalid);
    }
    {auto b=binding(root/"forked.db");CHECK(executeEnergyHistoryRequest(input(b,1)).applied==E::Ok);const auto bill=root/"fork-bill.db";BillingLedger ledger;
        CHECK(ledger.open(bill,b,now,BillingOpen::CreateNew)==S::Ok);auto first=import(ledger,reader,T+1000,1);CHECK(ledger.apply(first,now)==S::Ok);const auto prior=ledger.snapshot(now);
        std::filesystem::remove(b.source->file);std::filesystem::remove(journal(b.source->file));
        // Replace only this owned synthetic bundle, including READONLY-reader companions; production creation still refuses orphans.
        for(const auto *suffix:{"-wal","-shm","-journal"}){auto companion=b.source->file;companion+=suffix;std::filesystem::remove(companion);}
        CHECK(executeEnergyHistoryRequest(input(b,1,999999)).applied==E::Ok&&executeEnergyHistoryRequest(input(b,2)).applied==E::Ok);
        const auto q=ledger.sourceQuery(T+2000,now);CHECK(q.has_value());const auto fork=reader.transact(*q,cancel,5000);BillingChunk denied;
        CHECK(fork.reply&&fork.reply->source.checkpoint.sequence>prior.source->checkpoint.sequence&&fork.reply->status==E::Conflict&&!fork.reply->completeTrace);
        CHECK(makeBillingSourceChunk(2,*q,*fork.reply,&denied)==S::Invalid&&ledger.snapshot(now).digest==prior.digest);
    }
    {auto b=binding(root/"uncertain.db");CHECK(executeEnergyHistoryRequest(input(b,1)).applied==E::Ok);const auto bill=root/"uncertain-bill.db";BillingLedger ledger;CHECK(ledger.open(bill,b,now,BillingOpen::CreateNew)==S::Ok);
        auto first=import(ledger,reader,T+1000,1);CHECK(ledger.apply(first,now)==S::Ok);auto next=input(b,2);
        {EnergyRecoveryJournal journal;EnergyHistoryLedger source;CHECK(journal.open(next)==E::Ok&&source.open(next.file,next.binding,now)==E::Ok&&journal.prepare(next,source.checkpoint(now))==E::Ok&&source.apply(*next.batch,now)==E::Ok);}
        const auto q=ledger.sourceQuery(T+2000,now);const auto pending=reader.transact(*q,cancel,5000);BillingChunk denied;
        CHECK(pending.reply&&pending.reply->status==E::Conflict&&pending.reply->anchorStatus==E::Conflict&&!pending.reply->completeTrace&&makeBillingSourceChunk(2,*q,*pending.reply,&denied)==S::Invalid);}
    {auto b=binding(root/"missing.db");BillingLedger ledger;CHECK(ledger.open(root/"missing-bill.db",b,now,BillingOpen::CreateNew)==S::Ok);
        const auto q=ledger.sourceQuery(T+1000,now);const auto missing=reader.transact(*q,cancel,5000);CHECK(missing.reply&&missing.reply->status==E::Storage&&missing.reply->anchorStatus==E::Storage&&!ledger.snapshot(now).provisionalTotal);}
    {auto b=binding(root/"zero.db");CHECK(executeEnergyHistoryRequest(input(b,1,0)).applied==E::Ok);BillingLedger ledger;CHECK(ledger.open(root/"zero-bill.db",b,now,BillingOpen::CreateNew)==S::Ok);
        const auto q=ledger.sourceQuery(T+2000,now);const auto partial=reader.transact(*q,cancel,5000);BillingChunk chunk;
        CHECK(partial.reply&&partial.reply->status==E::Ok&&!partial.reply->completeTrace&&makeBillingSourceChunk(1,*q,*partial.reply,&chunk)==S::Invalid&&!ledger.snapshot(now).provisionalTotal);
        chunk=import(ledger,reader,T+1000,1);CHECK(ledger.apply(chunk,now)==S::Ok&&ledger.snapshot(now).provisionalTotal->numerator.zero());}
    {auto b=binding(root/"native-contract.db");b.evidence.origin=PowerOrigin::ObservedTelemetry;b.evidence.basis=PowerBasis::GpuBoard;b.evidence.adapter="native-nvml-milliwatts";b.source->binding.evidence=b.evidence;
        CHECK(executeEnergyHistoryRequest(input(b,1)).applied==E::Ok);const auto q=query(b,{0,sha256Hex(energy_history_detail::bindingText(b.source->binding))});const auto read=reader.transact(q,cancel,5000);BillingChunk chunk;
        CHECK(read.reply&&read.reply->completeTrace&&makeBillingSourceChunk(1,q,*read.reply,&chunk)==S::Unsupported);
        BillingLedger ledger;CHECK(ledger.open(root/"native-bill.db",b,now,BillingOpen::CreateNew)==S::Unsupported&&!std::filesystem::exists(root/"native-bill.db"));}
    maximumReceipt(root,reader,monetary.get(),retry.get());
    std::filesystem::remove_all(root);return finishTests("billing source");
}
