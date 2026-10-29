#include "billing_input_fixture.h"
#include "product/billing_review_access.h"
#include "product/settings.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
using namespace billing_input_fixture;
namespace {
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
template<class T>bool done(T &o){return until([&]{return !o.snapshot()->pending;});}
bool has(const BillingReviewPanel &p,const std::string &text){for(const auto &line:p.lines())if(line.find(text)!=std::string::npos)return true;return false;}
BillingOwnerTime fixedClock(){return {now,10000};}
struct Fixture {
    BillingOwnerBinding context;BillingSourceNotice notice;
    Fixture(const std::filesystem::path &root,const std::string &name){const auto dir=root/name;std::filesystem::create_directory(dir);context={dir/"fixture.bill.db",binding(dir/"fixture.source.db"),{}};
        {BillingRecoveryStore store;CHECK(store.open(context.file,context.binding,now,BillingRecoveryOpen::CreateNew)==S::Ok);}
        const auto source=executeEnergyHistoryRequest(input(context.binding,1));CHECK(source.applied==EnergyHistoryStatus::Ok);notice={anchor(source.checkpoint),T+1000,T+1000};}
};
void acknowledge(BillingReviewPanel &p,const BillingReviewAccess &a){p.key('v',a);if(!p.reveal)p.key('i',a);p.key(' ',a);}
void dimensions(const std::string &screen,int columns,int rows){size_t start=0,count=0;for(size_t i=0;i<screen.size();++i)if(screen[i]=='\n'){CHECK(i-start<static_cast<size_t>(columns));start=i+1;++count;}CHECK(count<=static_cast<size_t>(rows));}
}
int main(int argc,char **argv){
    if(argc!=6)return 2;
    const auto reader=std::filesystem::absolute(argv[1]);BillingOwnerHelpers helpers{std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4]),std::filesystem::absolute(argv[5])};
    const auto root=std::filesystem::path(makeScratchDir("billing-candidate-review"));
    BillingReviewPanel panel;panel.open();panel.key('p');panel.key('v');panel.key('g');panel.key('i');panel.key(' ');panel.key(13);CHECK(!panel.reviewing&&!panel.acknowledged&&has(panel,"no approved billing owner"));
    Fixture fixture(root,"native");BillingOwner owner(helpers,fixture.context,{true,10000},fixedClock);CHECK(owner.start(7)&&done(owner));
    BillingComputeOwner compute(reader,owner.snapshot()->context,{true,10000,512},fixedClock);CHECK(compute.start(7));auto access=billingCandidateReview(owner,compute);
    access.candidateSnapshot();CHECK(compute.notify(7,compute.snapshot()->revision,fixture.notice)==BillingComputeAdmission::Queued&&done(compute));
    const auto originalMoney=bytes(fixture.context.file),originalJournal=bytes(side(fixture.context.file,".billing-recovery")),originalSource=bytes(fixture.context.binding.source->file);
    panel.open(owner.snapshot());panel.key('p',access);CHECK(panel.candidateMode&&has(panel,"Candidate: REVIEWABLE")&&!has(panel,"fixture.")&&!has(panel,"370368"));
    panel.key('v',access);panel.key(13,access);panel.key(' ',access);CHECK(panel.reviewing&&!panel.acknowledged&&!std::filesystem::exists(side(fixture.context.file)));
    panel.key('i',access);panel.key(' ',access);CHECK(panel.acknowledged&&panel.completeDocument()&&has(panel,"370368 / 3600000000")&&has(panel,"candidate.original.receipt")&&has(panel,"preview.ending")&&has(panel,"SCHEDULING HINT"));
    panel.key('b',access);CHECK(panel.render(80,24,ColorLevel::None).find("END OF COMPLETE PRIVATE CANDIDATE DOCUMENT")!=std::string::npos);
    CHECK(panel.render(32,12,ColorLevel::None).find("DOCUMENT")!=std::string::npos);
    for(int width:{32,60,80})for(int height:{12,24})dimensions(panel.render(width,height,ColorLevel::None),width,height);
    panel.key('i',access);CHECK(!panel.acknowledged&&!has(panel,"370368"));panel.key(13,access);CHECK(!std::filesystem::exists(side(fixture.context.file)));
    panel.key(27,access);CHECK(!panel.reviewing&&panel.candidateMode&&bytes(fixture.context.file)==originalMoney);
    // Changing compute alone revokes a complete acknowledged review.
    acknowledge(panel,access);const auto before=compute.snapshot();CHECK(panel.acknowledged&&compute.refresh(7,before->revision)==BillingComputeAdmission::Queued&&done(compute));
    panel.key(13,access);CHECK(!panel.reviewing&&!panel.acknowledged&&!std::filesystem::exists(side(fixture.context.file)));
    // Changing money alone does too; explicit G may reuse the hint against the new exact prefix.
    acknowledge(panel,access);const auto oldMoney=owner.snapshot();CHECK(panel.acknowledged&&owner.refresh(7,oldMoney->revision)==BillingOwnerAdmission::Queued&&done(owner));
    panel.key(13,access);CHECK(!panel.reviewing&&!panel.acknowledged&&has(panel,"HISTORICAL / cannot submit"));
    const auto tickets=compute.snapshot()->lastTicket;panel.key('g',access);CHECK(done(compute)&&compute.snapshot()->lastTicket==tickets+1);panel.sync(access);CHECK(has(panel,"Candidate: REVIEWABLE"));
    // The last compute claim occurs after validated input and the monetary recheck, not just at UI key time.
    auto c=compute.snapshot();auto m=owner.snapshot();unsigned claims=0;auto invalid=c->candidate->original;invalid.id=std::string(1024,'x');
    CHECK(owner.submitReviewed(7,m->revision,invalid,[&]{++claims;return true;})==BillingOwnerAdmission::Invalid&&claims==0);
    CHECK(owner.submitReviewed(7,m->revision,c->candidate->original,[&]{++claims;compute.invalidate(7);return compute.claimReviewed(7,c->revision,7,m->revision);})==BillingOwnerAdmission::ReviewChanged&&claims==1);
    CHECK(owner.snapshot()==m&&!std::filesystem::exists(side(fixture.context.file))&&bytes(fixture.context.file)==originalMoney&&bytes(side(fixture.context.file,".billing-recovery"))==originalJournal);
    CHECK(owner.refresh(7,m->revision)==BillingOwnerAdmission::Queued&&done(owner));access.candidateSnapshot();CHECK(compute.notify(7,compute.snapshot()->revision,fixture.notice)==BillingComputeAdmission::Queued&&done(compute));
    c=compute.snapshot();m=owner.snapshot();CHECK(!compute.claimReviewed(7,c->revision,8,m->revision)&&!compute.claimReviewed(7,c->revision,7,m->revision+1)&&compute.snapshot()==c);
    // Bounded document failure cannot acknowledge; historical data still remains inspectable.
    auto broken=std::make_shared<BillingComputeSnapshot>(*c);auto candidate=std::make_shared<BillingCandidate>(*c->candidate);candidate->original.trace.intervals.resize(513);broken->candidate=candidate;
    BillingReviewAccess bad;bad.submit=[](uint64_t,uint64_t,uint64_t,uint64_t){CHECK(false);return BillingOwnerAdmission::Queued;};
    panel.open(m);panel.updateCandidate(broken);panel.key('p');acknowledge(panel,bad);panel.key(13,bad);CHECK(!panel.completeDocument()&&!panel.acknowledged&&has(panel,"INCOMPLETE REVIEW DOCUMENT"));
    // Maximum native binding remains byte-complete and viewport-bounded in a retained candidate.
    candidate=std::make_shared<BillingCandidate>(*c->candidate);auto prior=std::make_shared<BillingOwnerSnapshot>(*m);auto context=std::make_shared<BillingOwnerBinding>(*m->context);
    context->binding=largeBinding();context->binding.plan.supplier="fixture.\x1b[2J\n\xc3\xa9";prior->context=context;candidate->reviewed=prior;broken->candidate=candidate;broken->reviewable=false;
    panel.open(m);panel.updateCandidate(broken);panel.key('p');panel.key('i');CHECK(panel.completeDocument()&&has(panel,"tariff.revision[63].calendar[63].rate")&&has(panel,"fixture.\\x1b[2J\\x0a\\xc3\\xa9"));
    panel.key('b');const auto tail=panel.render(32,12,ColorLevel::None);CHECK(panel.offset>8192&&tail.find("DOCUMENT")!=std::string::npos&&tail.find('\x1b')==std::string::npos);dimensions(tail,32,12);panel.key('v',bad);CHECK(!panel.reviewing);
    // Exact Submit is one-use; the candidate never relabels the resulting action proof as current money.
    unsigned submits=0;const auto send=access.submit;access.submit=[&](uint64_t g,uint64_t r,uint64_t mg,uint64_t mr){++submits;return send(g,r,mg,mr);};
    panel.open(owner.snapshot());panel.key('p',access);acknowledge(panel,access);CHECK(panel.acknowledged);panel.key(13,access);panel.key(13,access);panel.key('v',access);panel.key(' ',access);panel.key(13,access);
    CHECK(done(owner)&&submits==1);panel.sync(access);CHECK(has(panel,"HISTORICAL / cannot submit")&&has(panel,"HISTORICAL ACTION PROOF")&&!owner.snapshot()->reviewable);
    CHECK(owner.snapshot()->actionProof&&owner.snapshot()->actionProof->committed.provisionalTotal->numerator.compare(c->candidate->preview.provisionalTotal->numerator)==0);
    CHECK(bytes(fixture.context.binding.source->file)==originalSource);const auto after=bytes(side(fixture.context.file));
    panel.key('f',access);CHECK(done(owner));panel.sync(access);panel.key('v',access);panel.key(13,access);CHECK(submits==1&&bytes(side(fixture.context.file))==after);
    panel.key('g',access);CHECK(!compute.snapshot()->pending&&!compute.snapshot()->reviewable); // old hint ends at the already committed prefix
    SettingsPanel settings;settings.open({});settings.billingAccess=access;settings.group=2;settings.field=4;settings.groupsFocused=false;settings.key(13,{});settings.key('p',{});
    CHECK(settings.billingReview.candidateMode&&settings.render(80,24,ColorLevel::None).find("Billing / candidate preview")!=std::string::npos);
    settings.key(3,{});CHECK(owner.snapshot()->state==BillingOwnerState::Stopped&&compute.snapshot()->state==BillingComputeState::Stopped);
    CHECK(access.submit(7,c->revision,7,m->revision)!=BillingOwnerAdmission::Queued);
    // Concurrent callers cannot consume the same pair twice.
    {Fixture f(root,"concurrent");BillingOwner money(helpers,f.context,{true,10000},fixedClock);CHECK(money.start(8)&&done(money));BillingComputeOwner calc(reader,money.snapshot()->context,{true,10000,512},fixedClock);CHECK(calc.start(8));auto a=billingCandidateReview(money,calc);
        a.candidateSnapshot();CHECK(calc.notify(8,calc.snapshot()->revision,f.notice)==BillingComputeAdmission::Queued&&done(calc));const auto cs=calc.snapshot();const auto ms=money.snapshot();
        BillingOwnerAdmission left=BillingOwnerAdmission::Invalid,right=left;std::thread x([&]{left=a.submit(8,cs->revision,8,ms->revision);}),y([&]{right=a.submit(8,cs->revision,8,ms->revision);});x.join();y.join();
        CHECK((left==BillingOwnerAdmission::Queued)!=(right==BillingOwnerAdmission::Queued));CHECK(done(money)&&money.snapshot()->actionProof&&money.snapshot()->actionProof->committed.sequence==1&&!calc.snapshot()->reviewable);}
    owner.stop();compute.stop();std::filesystem::remove_all(root);return finishTests("billing candidate review");
}
