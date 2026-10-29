#include "billing_input_fixture.h"
#include "product/billing_review_access.h"
#include "product/settings.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
using namespace billing_input_fixture;
namespace {
using D=BillingOwnerAdmission;using O=BillingOwnerState;
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
bool done(BillingOwner &o){return until([&]{return !o.snapshot()->pending;});}
bool has(const BillingReviewPanel &p,const std::string &s){for(const auto &line:p.lines())if(line.find(s)!=std::string::npos)return true;return false;}
bool contains(const billing_review_detail::Document &d,const std::string &s){for(const auto &line:d.lines)if(line.find(s)!=std::string::npos)return true;return false;}
std::shared_ptr<BillingOwnerSnapshot> sample(const std::filesystem::path &file){
    auto s=std::make_shared<BillingOwnerSnapshot>();s->generation=5;s->revision=9;s->lastTicket=2;s->state=O::NeedsReview;s->reviewable=true;
    auto p=plan(file);s->context=std::make_shared<const BillingOwnerBinding>(BillingOwnerBinding{file,p.context.binding,p.context.limits});
    auto v=std::make_shared<BillingOwnerView>();v->stable=true;v->observedUtcMs=at;
    auto stage=std::make_shared<BillingInputSnapshot>();stage->status=S::Ok;v->stage=stage;
    v->monetary=std::make_shared<const BillingRecoverySnapshot>();s->view=v;return s;
}
void review(BillingReviewPanel &p,const BillingReviewAccess &a,int key){p.key(key,a);if(!p.reveal)p.key('i',a);p.key(' ',a);p.key(13,a);}
void receiving(const BillingInputPlan &p,const std::string &previous={}){
    std::string payload;auto r=request(p,&payload);r.expectedRevision=previous;auto reply=executeBillingInputRequest(r);CHECK(reply.status==S::Ok);
    for(size_t offset=0;offset<payload.size();offset+=billingInputPageBytes){r.action=A::Append;r.offset=offset;r.data=payload.substr(offset,billingInputPageBytes);r.expectedRevision=reply.revision;reply=executeBillingInputRequest(r);CHECK(reply.status==S::Ok);}
}
void dimensions(const std::string &screen,int columns,int rows){size_t begin=0,count=0;for(size_t pos=0;pos<screen.size();++pos)if(screen[pos]=='\n'){CHECK(pos-begin<static_cast<size_t>(columns));begin=pos+1;++count;}CHECK(count<=static_cast<size_t>(rows));}
}
int main(int argc,char **argv){
    if(argc!=6)return 2;
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4])};
    const auto inputFixture=std::filesystem::absolute(argv[5]);const auto root=std::filesystem::path(makeScratchDir("billing-review"));
    BillingReviewPanel panel;BillingReviewAccess mock;unsigned calls=0,stops=0;
    auto admit=[&](uint64_t g,uint64_t r){CHECK(g==5&&r==9);++calls;return D::Queued;};mock.inspect=mock.create=mock.commit=mock.retry=mock.reconcile=admit;mock.requestStop=[&]{++stops;};
    panel.open();for(int key:std::vector<int>{'n','c','r','e','i',' ',13,'f'})panel.key(key,mock);
    CHECK(calls==0&&has(panel,"no approved billing owner"));
    auto s=sample(root/"fixture.private-ledger.db");panel.open(s);CHECK(!has(panel,"fixture.")&&!panel.reveal);
    panel.key('n',mock);panel.key(13,mock);panel.key(' ',mock);CHECK(panel.reviewing&&!panel.acknowledged&&calls==0);
    panel.key('i',mock);CHECK(has(panel,"fixture.private-ledger.db")&&panel.completeDocument());panel.key(' ',mock);CHECK(panel.acknowledged);
    panel.key(13,mock);CHECK(calls==1&&!panel.reviewing&&panel.submittedTicket==3&&panel.message.find("not a durable")!=std::string::npos);
    for(int key:std::vector<int>{13,13,'n',' ',13,'f'})panel.key(key,mock);
    CHECK(calls==1); // even before a new snapshot arrives
    panel.open(s);panel.key('n',mock);panel.key('i',mock);panel.key(' ',mock);auto changed=std::make_shared<BillingOwnerSnapshot>(*s);++changed->revision;
    panel.update(changed);panel.key(13,mock);CHECK(!panel.reviewing&&!panel.acknowledged&&calls==1);
    panel.open(s);panel.key('n',mock);panel.key('i',mock);panel.key(' ',mock);panel.key('i',mock);panel.key(13,mock);CHECK(calls==1&&!panel.acknowledged);
    panel.key(27,mock);CHECK(panel.visible&&!panel.reviewing);CHECK(panel.key(27,mock)==BillingReviewResult::Close&&!panel.visible&&!panel.reveal);
    for(auto state:{O::Stopped,O::Busy,O::ClockFault,O::Faulted}){auto bad=std::make_shared<BillingOwnerSnapshot>(*s);bad->state=state;panel.open(bad);for(int key:{'n','c','r','e','f'})panel.key(key,mock);CHECK(!panel.reviewing&&calls==1);}
    panel.open(s);CHECK(panel.key('q',mock)==BillingReviewResult::Quit&&stops==1);review(panel,mock,'n');CHECK(calls==1&&!panel.reveal);
    panel.open(s);CHECK(panel.key(3,mock)==BillingReviewResult::Quit&&stops==2);
    for(auto admission:{D::Stopped,D::NotReady,D::StaleGeneration,D::ReviewChanged,D::Invalid}){
        auto a=mock;a.create=[admission](uint64_t,uint64_t){return admission;};panel.open(s);review(panel,a,'n');CHECK(!panel.reviewing&&!panel.submittedTicket&&!panel.message.empty());}
    // Bound failures cannot produce acknowledgement, even if the action otherwise looks eligible.
    for(unsigned variant=0;variant<3;++variant){auto bad=sample(root/"too-large.db");auto b=std::make_shared<BillingOwnerBinding>(*bad->context);
        if(variant==0)b->binding.plan.supplier=std::string(128*1024+1,'x');
        if(variant==1)b->binding.plan.revisions.resize(129);
        if(variant==2)b->binding.plan.revisions[0].calendar.resize(65);
        bad->context=b;panel.open(bad);review(panel,mock,'n');CHECK(!panel.completeDocument()&&!panel.acknowledged&&has(panel,"INCOMPLETE REVIEW")&&calls==1);}
    billing_review_detail::Document capped;capped.add(std::string(16*1024*1024,'x'));capped.add("one byte too many");CHECK(!capped.complete&&capped.lines.size()==1);
    billing_review_detail::Document lines;for(unsigned i=0;i<131073;++i)lines.add("");CHECK(!lines.complete&&lines.lines.size()==131072);
    CHECK(billing_review_detail::number(UINT64_MAX)=="18446744073709551615");
    // Every byte survives ASCII quoting, including ESC/newline/NUL, UTF-8 and backslash sequences.
    std::string raw;for(unsigned i=0;i<256;++i)raw+=static_cast<char>(i);const auto escaped=billing_review_detail::text(raw);std::string decoded;
    for(size_t i=1;i+1<escaped.size();++i){const auto c=escaped[i];CHECK(c>=32&&c<=126);if(c=='\\'){CHECK(escaped[++i]=='x');decoded+=static_cast<char>(std::stoul(escaped.substr(i+1,2),nullptr,16));i+=2;}else decoded+=c;}
    CHECK(decoded==raw);
    auto unsafe=sample(root/"quoted.db");auto b=std::make_shared<BillingOwnerBinding>(*unsafe->context);b->binding.plan.supplier="fixture.\x1b[2J\n\t\"\\\xc3\xa9";unsafe->context=b;
    panel.open(unsafe);panel.key('i');CHECK(has(panel,"fixture.\\x1b[2J\\x0a\\x09\\x22\\x5c\\xc3\\xa9"));CHECK(panel.render(80,24,ColorLevel::None).find('\x1b')==std::string::npos);
    // Maximum admitted binding and trace: tail reachable well beyond the old 8192-row ceiling.
    auto large=sample(root/"large.db");b=std::make_shared<BillingOwnerBinding>(*large->context);b->binding=largeBinding();large->context=b;
    auto v=std::make_shared<BillingOwnerView>(*large->view);auto m=std::make_shared<BillingRecoverySnapshot>();m->status=S::Ok;m->state=R::Prepared;m->original=largeChunk();m->original->id="fixture.tail-original";v->monetary=m;large->view=v;
    panel.open(large);panel.key('i');CHECK(panel.completeDocument()&&has(panel,"tariff.revision[63].calendar[63].rate")&&has(panel,"fixture.tail-original"));
    for(int width:{20,32,60,80,120})for(int height:{8,12,24}){panel.offset=0;dimensions(panel.render(width,height,ColorLevel::None),width,height);panel.key('b');const auto end=panel.render(width,height,ColorLevel::None);dimensions(end,width,height);
        if(width>=32&&height>=12){CHECK(panel.offset>8192&&end.find("DOCUMENT")!=std::string::npos);const auto old=panel.offset;panel.key(kKeyUp);panel.render(width,height,ColorLevel::None);CHECK(panel.offset<old);} }
    panel.key('t');CHECK(panel.offset==0);panel.key('i');CHECK(!has(panel,"fixture.tail-original"));
    panel.key('i');panel.key('b');panel.render(80,24,ColorLevel::None);
    CHECK(panel.render(32,12,ColorLevel::None).find("DOCUMENT")!=std::string::npos); // resizing preserves the reviewed tail
    // Full source receipts include original query, ancestry, nested readings and exact monetary proof.
    auto sourcePlan=plan(root/"receipt.db");sourcePlan.context.binding=binding(root/"power.db");sourcePlan.context.nowUtcMs=now;
    CHECK(executeEnergyHistoryRequest(input(sourcePlan.context.binding,1)).applied==EnergyHistoryStatus::Ok);
    const auto queryRequest=query(sourcePlan.context.binding,{0,sha256Hex(energy_history_detail::bindingText(sourcePlan.context.binding.source->binding))});BillingChunk chunk;
    CHECK(makeBillingSourceChunk(1,queryRequest,executeEnergyTraceRequest(queryRequest),&chunk)==S::Ok);
    auto source=sample(sourcePlan.context.file);source->context=std::make_shared<const BillingOwnerBinding>(BillingOwnerBinding{sourcePlan.context.file,sourcePlan.context.binding,{}});
    v=std::make_shared<BillingOwnerView>(*source->view);m=std::make_shared<BillingRecoverySnapshot>();m->status=S::Ok;m->state=R::Prepared;m->original=chunk;v->monetary=m;source->view=v;
    auto u=std::make_shared<BillingOwnerIntent>();u->original=std::make_shared<const BillingChunk>(chunk);u->monetary=m;source->uncertain=u;source->actionProof=m;source->reviewable=false;
    const auto doc=billing_review_detail::document(*source);CHECK(doc.complete&&contains(doc,"receipt.request.originalQuery UTC ms = "+std::to_string(now))&&contains(doc,"receipt.reply.recovery.original.reading[1]"));
    CHECK(contains(doc,"receipt.request.anchor.sequence = 0")&&contains(doc,"receipt.reply.completeTrace[0]")&&contains(doc,"HISTORICAL ACTION PROOF")&&contains(doc,"UNKNOWN HANDOFF")&&contains(doc,"RETAINED EARLIER"));
    panel.open(source);CHECK(!has(panel,"fixture.")&&!has(panel,std::to_string(now)));panel.key('r',mock);CHECK(!panel.reviewing);
    // Native routes preserve independent draft/save semantics and stop without opening a backend.
    SettingsPanel settings;settings.open({});settings.draft.color=!settings.active.color;settings.groupsFocused=false;settings.group=2;settings.field=4;
    CHECK(settings.fields()==5&&settings.key(13,{})==SettingsResult::None&&settings.billingReview.visible&&settings.dirty());
    CHECK(settings.render(80,24,ColorLevel::None).find("no approved billing owner")!=std::string::npos);settings.key(27,{});CHECK(!settings.billingReview.visible&&settings.dirty());
    settings.group=8;settings.field=5;CHECK(settings.fields()==6);settings.key(13,{});CHECK(settings.billingReview.visible);settings.key(27,{});
    settings.billingAccess.requestStop=[&]{++stops;};CHECK(settings.key(3,{})==SettingsResult::Quit&&stops==3&&settings.dirty());
    // Real owner/helper flows. Admission is followed by a separate inspection; old views never become post-write totals.
    {auto p=plan(root/"create.db");BillingOwner owner(helpers,{p.context.file,p.context.binding,{}},{true,10000},[]{return BillingOwnerTime{at,10000};});CHECK(owner.start(10)&&done(owner));
        auto a=billingOwnerReview(owner);panel.open(owner.snapshot());review(panel,a,'n');CHECK(panel.submittedTicket&&done(owner));panel.update(owner.snapshot());
        CHECK(!panel.reviewing&&has(panel,"HISTORICAL ACTION PROOF")&&has(panel,"RETAINED EARLIER")&&!owner.snapshot()->reviewable);
        panel.key('f',a);CHECK(done(owner));panel.update(owner.snapshot());CHECK(owner.snapshot()->state==O::Ready&&has(panel,"CURRENT READ"));panel.key('q',a);CHECK(owner.snapshot()->state==O::Stopped&&!owner.start(11));}
    {auto p=plan(root/"commit.db");receiving(p);const auto original=bytes(side(p.context.file));BillingOwner owner(helpers,{p.context.file,p.context.binding,{}},{true,10000},[]{return BillingOwnerTime{at,10000};});CHECK(owner.start(12)&&done(owner));
        auto a=billingOwnerReview(owner);panel.open(owner.snapshot());panel.key('c',a);panel.key('i',a);panel.key(' ',a);panel.key(27,a);CHECK(bytes(side(p.context.file))==original&&!std::filesystem::exists(p.context.file));
        review(panel,a,'c');CHECK(done(owner)&&owner.snapshot()->actionProof&&inspect(p).state==R::Empty);}
    {auto p=plan(root/"fixture.billing-input-prepared.db");std::atomic<bool> cancel{false};BillingInputProcessClient writer(helpers.input),faulty(inputFixture);const auto created=writer.transact(p,"",cancel,10000);CHECK(created.confirmed.has_value());
        p.prior=created.confirmed;p.chunk=billing_fixture::chunk();faulty.transact(p,created.reply->revision,cancel,10000);const auto original=bytes(side(p.context.file));
        BillingOwner owner(helpers,{p.context.file,p.context.binding,{}},{true,10000},[]{return BillingOwnerTime{at,10000};});CHECK(owner.start(13)&&done(owner));auto a=billingOwnerReview(owner);panel.open(owner.snapshot());
        panel.key('r',a);panel.key('i',a);panel.key(' ',a);const auto reviewed=owner.snapshot();CHECK(owner.refresh(13,reviewed->revision)==D::Queued&&done(owner));
        panel.key(13,a);CHECK(!panel.reviewing&&inspect(p).state==R::Prepared&&bytes(side(p.context.file))==original);
        review(panel,a,'r');panel.key(13,a);CHECK(done(owner)&&owner.snapshot()->actionProof&&inspect(p).committed.sequence==1&&bytes(side(p.context.file))==original);
        panel.update(owner.snapshot());panel.key('f',a);CHECK(done(owner));panel.update(owner.snapshot());review(panel,a,'e');CHECK(done(owner));
        panel.update(owner.snapshot());panel.key('f',a);CHECK(done(owner));panel.update(owner.snapshot());CHECK(owner.snapshot()->state==O::Ready&&owner.snapshot()->view->stage->phase==P::Complete);}
    std::filesystem::remove_all(root);return finishTests("billing review");
}
