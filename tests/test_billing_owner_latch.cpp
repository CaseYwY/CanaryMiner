#include "billing_input_fixture.h"
#include "product/billing_owner.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
using namespace billing_input_fixture;
int main(int argc,char **argv){
    if(argc!=6)return 2;
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4])};
    const auto root=std::filesystem::path(makeScratchDir("billing-owner-latch"));const auto p=plan(root/"bill.db");
    BillingOwner owner(helpers,{p.context.file,p.context.binding,p.context.limits},{true,10000},[]{return BillingOwnerTime{at,10000};});
    BillingOwnerTestPeer::unreapedNext(owner);CHECK(owner.start(17));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(owner.snapshot()->pending&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto s=owner.snapshot();CHECK(s->state==BillingOwnerState::Faulted&&s->transport==LocalTaskStatus::Unreaped&&!s->reviewable&&!s->view&&!s->pending);
    CHECK(owner.refresh(17,s->revision)==BillingOwnerAdmission::Stopped&&owner.createReviewed(17,s->revision)==BillingOwnerAdmission::Stopped&&!owner.start(18));
    CHECK(!std::filesystem::exists(p.context.file)&&!std::filesystem::exists(side(p.context.file,".billing-input.lock")));
    owner.stop();const auto stopped=owner.snapshot();owner.stop();CHECK(owner.snapshot()==stopped);
    helpers.monetaryQuery=std::filesystem::absolute(argv[5]);const auto file=root/"fixture.billing-owner-read-hang.db";
    BillingOwner revoked(helpers,{file,p.context.binding,p.context.limits},{true,10000},[]{return BillingOwnerTime{at,10000};});CHECK(revoked.start(19));
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!std::filesystem::exists(side(file,".fixture-ready"))&&std::chrono::steady_clock::now()<limit)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(std::filesystem::exists(side(file,".fixture-ready")));BillingOwnerTestPeer::unreapedNext(revoked);CHECK(revoked.invalidate(19));
    while(revoked.snapshot()->pending&&std::chrono::steady_clock::now()<limit)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(revoked.snapshot()->state==BillingOwnerState::Faulted&&revoked.snapshot()->transport==LocalTaskStatus::Unreaped);
    CHECK(revoked.refresh(19,revoked.snapshot()->revision)==BillingOwnerAdmission::Stopped);revoked.stop();
    std::filesystem::remove_all(root);return finishTests("billing owner unreaped latch");
}
