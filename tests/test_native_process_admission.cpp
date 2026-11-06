#include "product/native_process_admission.h"
#include "product_test_util.h"
#include <array>
#include <thread>
#include <type_traits>

uint64_t peerNativeProcessEpoch();
bool peerNativeProcessUnresolved();
void peerNativeProcessFault(om::NativeProcessAdmission &);
void peerNativeProcessSettle(om::NativeProcessAdmission &);
using namespace om;
int main(){
    static_assert(!std::is_copy_constructible<NativeProcessAdmission>::value,"ownership must not copy");
    static_assert(!std::is_move_constructible<NativeProcessAdmission>::value,"ownership must not move");
    const auto original=nativeProcessEpoch();CHECK(original&&original==peerNativeProcessEpoch()&&!nativeProcessUnresolved());
    NativeProcessAdmission a,b,c;CHECK(a.begin()&&b.begin()&&a.permitted()&&b.permitted());CHECK(!a.begin());
    peerNativeProcessFault(a);CHECK(nativeProcessUnresolved()&&peerNativeProcessUnresolved());
    CHECK(!a.permitted()&&!b.permitted()&&!c.begin()&&!nativeProcessEpochCurrent(original));
    const auto firstFault=nativeProcessEpoch();a.markUnresolved();CHECK(nativeProcessEpoch()==firstFault);
    b.markUnresolved();CHECK(nativeProcessEpoch()>firstFault);peerNativeProcessSettle(a);CHECK(nativeProcessUnresolved());
    peerNativeProcessSettle(b);CHECK(!nativeProcessUnresolved());
    CHECK(!a.unchanged()&&!b.unchanged()&&!nativeProcessEpochCurrent(original));
    CHECK(c.begin()&&c.permitted());c.settled();CHECK(c.unchanged()&&!c.permitted());
    CHECK(a.begin()&&a.permitted());a.settled();
    // No heap or owner mutex is needed for independent concurrent faults/settlement.
    std::array<NativeProcessAdmission,32> owners;
    for(auto &owner:owners)CHECK(owner.begin());
    std::array<std::thread,32> threads;
    for(std::size_t i=0;i<threads.size();++i)threads[i]=std::thread([&owners,i]{peerNativeProcessFault(owners[i]);});
    for(auto &thread:threads)thread.join();
    CHECK(nativeProcessUnresolved()&&!c.begin());
    for(auto &owner:owners)CHECK(!owner.unchanged());
    for(std::size_t i=0;i<threads.size();++i)threads[i]=std::thread([&owners,i]{peerNativeProcessSettle(owners[i]);});
    for(auto &thread:threads)thread.join();
    CHECK(!nativeProcessUnresolved()&&c.begin()&&c.permitted());c.settled();
    {NativeProcessAdmission abandoned;CHECK(abandoned.begin());abandoned.markUnresolved();}
    CHECK(nativeProcessUnresolved()&&!c.begin()); // destruction is not settlement proof
    return finishTests("shared native process uncertainty epoch (pure CPU)");
}
