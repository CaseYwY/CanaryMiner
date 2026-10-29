#include "billing_input_fixture.h"
#include "product/billing_input_query.h"
#include "product/local_store_lease.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
using namespace billing_input_fixture;
namespace {
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
BillingQueryPageRequest page(const BillingQueryRequest &r){return {r.generation,r.ticket,r.file,sha256Hex(billing_detail::bindingText(r.binding,r.limits)),"",r.nowUtcMs,0};}
BillingChunk longChunk(){auto c=largeChunk();for(size_t i=0;i<c.trace.intervals.size();++i)c.trace.intervals[i].watts=d(std::string(88,i%2?'2':'1'));return c;}
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);BillingInputQueryClient reader(helper);BillingInputProcessClient writer(std::filesystem::absolute(argv[3]));std::atomic<bool> cancel{false};
    const auto root=std::filesystem::path(makeScratchDir("billing-stage-query"));auto p=plan(root/"bill.db");
    const auto missing=reader.transact(p.context,cancel,5000);CHECK(missing.reply&&missing.reply->status==S::Ok&&!missing.reply->present&&!missing.reply->outcome&&!std::filesystem::exists(side(p.context.file)));
    const auto opened=writer.transact(p,"",cancel,10000);CHECK(opened.confirmed.has_value());p.prior=opened.confirmed;p.chunk=longChunk();
    std::string payload;auto r=request(p,&payload);r.expectedRevision=opened.reply->revision;auto begin=writer.exchange(r,cancel,5000);CHECK(begin.reply&&begin.reply->phase==P::Receiving);
    auto read=reader.transact(p.context,cancel,5000);CHECK(read.reply&&read.reply->present&&!read.reply->received&&!read.reply->original&&!read.reply->outcome&&read.reply->revision==begin.reply->revision);
    r.action=A::Append;r.expectedRevision=begin.reply->revision;r.data=payload.substr(0,billingInputPageBytes);const auto first=writer.exchange(r,cancel,5000);CHECK(first.reply&&first.reply->received==billingInputPageBytes);
    const auto partialBytes=bytes(side(p.context.file));read=reader.transact(p.context,cancel,5000);
    CHECK(read.reply&&read.reply->phase==P::Receiving&&!read.reply->original&&!read.reply->outcome&&read.reply->originalPrefix==payload.substr(r.manifest.bindingBytes,billingInputPageBytes-r.manifest.bindingBytes)&&bytes(side(p.context.file))==partialBytes);
    r.expectedRevision=first.reply->revision;r.offset=billingInputPageBytes;r.data=payload.substr(r.offset);const auto ready=writer.exchange(r,cancel,5000);CHECK(ready.reply&&ready.reply->status==S::Ok);
    read=reader.transact(p.context,cancel,5000);CHECK(read.reply&&read.reply->phase==P::Receiving&&read.reply->original&&billing_detail::chunkText(*read.reply->original)==billing_detail::chunkText(*p.chunk)&&!read.reply->outcome);
    auto q=page(p.context);const auto initial=executeBillingInputQuery(q);CHECK(initial.status==S::Ok&&initial.total>billingQueryPageBytes);
    q.expectedView=initial.viewDigest;q.offset=billingQueryPageBytes;
    r.expectedRevision=ready.reply->revision;r.action=A::Commit;r.offset=0;r.data.clear();const auto applied=writer.exchange(r,cancel,5000);CHECK(applied.reply&&applied.reply->phase==P::Complete);
    CHECK(executeBillingInputQuery(q).status==S::Conflict);
    const auto completed=bytes(side(p.context.file)),journal=bytes(side(p.context.file,".billing-recovery")),database=bytes(p.context.file);
    read=reader.transact(p.context,cancel,5000);CHECK(read.reply&&read.reply->original&&read.reply->outcome&&read.reply->outcome->committed.sequence==1&&read.reply->revision==applied.reply->revision);
    CHECK(bytes(side(p.context.file))==completed&&bytes(side(p.context.file,".billing-recovery"))==journal&&bytes(p.context.file)==database);
    // Stored stage certificates are historical: this reader neither opens nor repairs the monetary pair.
    std::filesystem::remove(p.context.file);std::filesystem::remove(side(p.context.file,".billing-recovery"));
    read=reader.transact(p.context,cancel,5000);CHECK(read.reply&&read.reply->outcome&&read.reply->outcome->committed.sequence==1&&!std::filesystem::exists(p.context.file)&&!std::filesystem::exists(side(p.context.file,".billing-recovery")));
    {auto wrong=p.context;wrong.binding.cycleKey="fixture.other";const auto denied=reader.transact(wrong,cancel,5000);CHECK(denied.reply&&denied.reply->status==S::Attribution&&!denied.reply->present);}
    {auto earlier=p.context;earlier.nowUtcMs--;const auto denied=reader.transact(earlier,cancel,5000);CHECK(denied.reply&&denied.reply->status==S::Clock);}
    {LocalStoreLease lease;CHECK(lease.acquire(side(p.context.file,".billing-input.lock")));CHECK(reader.transact(p.context,cancel,5000).reply->status==S::Storage);}
    // A near-1-MiB binding stays in caller memory: the helper returns its manifest and staged original prefix.
    {auto big=plan(root/"large.db");big.context.binding=largeBinding();std::string input;auto req=request(big,&input);auto current=writer.exchange(req,cancel,10000);
        req.action=A::Append;req.expectedRevision=current.reply->revision;req.data=input.substr(0,billingInputPageBytes);current=writer.exchange(req,cancel,10000);
        const auto saved=bytes(side(big.context.file));const auto partial=reader.transact(big.context,cancel,10000);
        CHECK(partial.reply&&partial.reply->received==billingInputPageBytes&&partial.reply->originalPrefix.empty()&&!partial.reply->original);
        // Rehashing an altered partial binding prefix cannot fool parent reconstruction against its approved complete binding.
        auto changed=req.data;changed[0]='x';const auto forged=billing_input_detail::stageBody(req.manifest,P::Receiving,{},changed);
        {std::ofstream f(side(big.context.file),std::ios::binary|std::ios::trunc);f<<forged<<sha256Hex(forged)<<'\n';}
        const auto denied=reader.transact(big.context,cancel,10000);CHECK(denied.transport==LocalTaskStatus::Unavailable&&!denied.reply);
        {std::ofstream f(side(big.context.file),std::ios::binary|std::ios::trunc);f<<saved;}
        for(size_t offset=billingInputPageBytes;offset<input.size();offset+=billingInputPageBytes){req.expectedRevision=current.reply->revision;req.offset=offset;req.data=input.substr(offset,billingInputPageBytes);current=writer.exchange(req,cancel,10000);CHECK(current.reply&&current.reply->status==S::Ok);}
        const auto full=reader.transact(big.context,cancel,10000);CHECK(full.reply&&full.reply->received==input.size()&&full.reply->originalPrefix=="null"&&!full.reply->outcome);
        req.expectedRevision=current.reply->revision;req.action=A::Commit;req.offset=0;req.data.clear();CHECK(writer.exchange(req,cancel,10000).reply->phase==P::Complete);
        const auto exact=reader.transact(big.context,cancel,10000);CHECK(exact.reply&&exact.reply->outcome&&exact.reply->outcome->state==R::Empty);}
    // Codec and response identity guards.
    {auto request=page(p.context);std::string wire;BillingQueryPageRequest decoded;uint64_t parent=0;
        CHECK(encodeBillingInputQueryRequest(request,localTaskProcessId(),&wire)&&decodeBillingInputQueryRequest(wire,&decoded,&parent));CHECK(!decodeBillingInputQueryRequest(wire+" ",&decoded,&parent));
        const auto result=executeBillingInputQuery(request);BillingQueryPageReply reply;CHECK(encodeBillingInputQueryReply(request,result,&wire)&&decodeBillingInputQueryReply(request,wire,&reply));
        for(unsigned k=0;k<5;++k){auto bad=request;if(k==0)bad.generation++;if(k==1)bad.ticket++;if(k==2)bad.offset=1;if(k==3)bad.expectedView=std::string(64,'f');if(k==4)bad.offset=billingQueryPageBytes;CHECK(!decodeBillingInputQueryReply(bad,wire,&reply));}
        CHECK(!decodeBillingInputQueryReply(request,wire+"x",&reply));
        LocalTaskProcess wrong(std::filesystem::absolute(argv[3]));CHECK(encodeBillingInputQueryRequest(request,localTaskProcessId(),&wire)&&wrong.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);
#if defined(__linux__)
        LocalTaskProcess pid(helper);CHECK(encodeBillingInputQueryRequest(request,localTaskProcessId()==1?2:1,&wire)&&pid.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);
#endif
    }
    {auto change=plan(root/"fixture.billing-stage-query-change.db");const auto created=writer.transact(change,"",cancel,10000);change.prior=created.confirmed;change.chunk=longChunk();
        CHECK(writer.transact(change,created.reply->revision,cancel,10000).confirmed.has_value());BillingInputQueryClient fake(fixture);const auto result=fake.transact(change.context,cancel,5000);
        CHECK(result.reply&&result.reply->status==S::Conflict&&!result.reply->original&&!result.reply->outcome);}
    {auto bad=plan(root/"fixture.billing-stage-query-proof.db");CHECK(writer.transact(bad,"",cancel,10000).confirmed.has_value());BillingInputQueryClient fake(fixture);
        const auto result=fake.transact(bad.context,cancel,5000);CHECK(result.transport==LocalTaskStatus::Unavailable&&!result.reply);}
    {auto hang=plan(root/"fixture.billing-stage-query-hang.db");CHECK(writer.transact(hang,"",cancel,10000).confirmed.has_value());BillingInputQueryClient stalled(fixture);BillingInputQueryResult result;
        std::thread pending([&]{result=stalled.transact(hang.context,cancel,5000);});CHECK(until([&]{return std::filesystem::exists(side(hang.context.file,".fixture-ready"));}));
        CHECK(stalled.transact(hang.context,cancel,5000).transport==LocalTaskStatus::Busy);cancel=true;pending.join();CHECK(result.transport==LocalTaskStatus::Cancelled&&!result.reply);cancel=false;
        CHECK(stalled.transact(hang.context,cancel,100).transport==LocalTaskStatus::Timeout);cancel=true;CHECK(reader.transact(p.context,cancel,5000).transport==LocalTaskStatus::Cancelled);cancel=false;}
    std::filesystem::remove_all(root);return finishTests("billing input query");
}
