// Pure parent-memory lifetimes/scopes. Threads check isolation; no native process, data source or file is accessed.
#include "product/native_process_admission.h"
#include "product_test_util.h"
#include <atomic>
#include <thread>

int main(){
    using namespace om;
    const auto original=nativeProcessEpoch();CHECK(original!=0&&!nativeProcessUnresolved());
    CHECK(nativeProcessAdmissionEpoch()==original);
    NativeProcessLifetime parent;CHECK(!parent.current()&&parent.epoch()==0);CHECK(parent.begin()&&parent.current()&&parent.epoch()==original);
    NativeProcessAdmission operation;CHECK(operation.begin()&&operation.permitted());
    {
        NativeProcessScope scope(parent.epoch());CHECK(nativeProcessAdmissionEpoch()==original&&operation.permitted());
        NativeProcessLifetime nested;CHECK(nested.begin()&&nested.epoch()==original);
        {
            NativeProcessScope incompatible(original+1);CHECK(nativeProcessAdmissionEpoch()==0&&!operation.permitted());
            CHECK(parent.current()&&!parent.permitted()&&!parent.begin());
            NativeProcessAdmission denied;CHECK(!denied.begin()&&!denied.permitted());
            NativeProcessLifetime deniedLife;CHECK(!deniedLife.begin()&&!deniedLife.current());
        }
        CHECK(nativeProcessAdmissionEpoch()==original&&operation.permitted());
        {
            NativeProcessScope same(original);CHECK(nativeProcessAdmissionEpoch()==original&&operation.permitted());
        }
        CHECK(nativeProcessAdmissionEpoch()==original);
    }
    CHECK(nativeProcessAdmissionEpoch()==original&&operation.permitted());
    try{NativeProcessScope incompatible(original+1);throw 1;}catch(int){}
    CHECK(nativeProcessAdmissionEpoch()==original&&operation.permitted());
    NativeProcessLifetime invalid;
    {
        NativeProcessScope zero(0);CHECK(nativeProcessAdmissionEpoch()==0);CHECK(!invalid.begin()&&!invalid.current());
        NativeProcessAdmission denied;CHECK(!denied.begin());
    }
    CHECK(!invalid.begin()&&!invalid.current()); // first rejected lifetime cannot recapture a later good epoch
    NativeProcessAdmission foreign;CHECK(foreign.begin());foreign.markUnresolved();CHECK(nativeProcessUnresolved());
    NativeProcessLifetime duringFault;CHECK(!duringFault.begin()&&duringFault.epoch()==0);
    foreign.settled();CHECK(!nativeProcessUnresolved()&&nativeProcessEpoch()!=original);
    CHECK(!duringFault.begin()&&!duringFault.current());
    CHECK(!parent.current()&&!parent.begin()&&!operation.permitted());
    const auto current=nativeProcessEpoch();
    {
        NativeProcessScope old(parent.epoch());CHECK(nativeProcessAdmissionEpoch()==original);
        NativeProcessAdmission denied;CHECK(!denied.begin());
        NativeProcessLifetime deniedLife;CHECK(!deniedLife.begin()&&!deniedLife.current());
        {
            NativeProcessScope cannotRenew(current);CHECK(nativeProcessAdmissionEpoch()==0);
            NativeProcessAdmission deniedAgain;CHECK(!deniedAgain.begin());
        }
        CHECK(nativeProcessAdmissionEpoch()==original);
        std::atomic<bool> isolated{false},explicitOldDenied{false};
        std::thread independent([&]{NativeProcessAdmission fresh;isolated=nativeProcessAdmissionEpoch()==current&&fresh.begin();
            NativeProcessScope copiedOld(parent.epoch());NativeProcessAdmission stale;explicitOldDenied=!stale.begin();});
        independent.join();CHECK(isolated.load()&&explicitOldDenied.load());
    }
    CHECK(nativeProcessAdmissionEpoch()==current);
    NativeProcessLifetime fresh;CHECK(fresh.begin()&&fresh.current()&&fresh.epoch()==current);
    {
        NativeProcessScope scope(fresh.epoch());NativeProcessAdmission allowed;CHECK(allowed.begin()&&allowed.permitted());allowed.settled();CHECK(!allowed.permitted());
    }
    operation.settled();CHECK(!operation.permitted()&&!nativeProcessUnresolved());
    return finishTests("original native lifetime and nested thread-local scope (memory only)");
}
