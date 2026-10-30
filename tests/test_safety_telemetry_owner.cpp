// Deterministic late-completion seam: only this executable replaces the actual helper transaction.
#include "product/safety_telemetry_owner.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
namespace {
std::atomic<bool> entered{false},release{false},observedCancel{false};
std::atomic<unsigned> flights{0};
LocalTaskStatus completion=LocalTaskStatus::Unreaped;
bool wait(const std::function<bool()> &ready){const auto end=safetyTelemetryTime().monotonicMs+3000;
    do{if(ready())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(safetyTelemetryTime().monotonicMs<end);return ready();}
}
namespace om {
SafetyTelemetryResult safetyTelemetryOwnerTestSample(const SafetyTelemetryRequest &r,const std::atomic<bool> &cancel,unsigned){
    ++flights;entered=true;
    while(!release)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    observedCancel=cancel.load();SafetyTelemetryResult out;out.transport=completion;
    if(completion==LocalTaskStatus::Ok)out.reply=SafetyTelemetryReply{SafetyTelemetryStatus::Ok,r.issued,r.issued,{{40,0,511}}};
    return out;
}
}
int main(){
    for(auto status:{LocalTaskStatus::Ok,LocalTaskStatus::Cancelled,LocalTaskStatus::Unreaped}){
        entered=release=observedCancel=false;flights=0;completion=status;
        SafetyTelemetryPolicy policy;policy.enabled=true;
        SafetyTelemetryOwner owner(std::filesystem::absolute("unused-safety-helper"),{"GPU-00112233-4455-6677-8899-aabbccddee01"},policy);
        CHECK(owner.start(7));CHECK(wait([]{return entered.load();}));const auto before=owner.snapshot();CHECK(before->pending&&!before->reply);
        owner.requestStop();const auto stopped=owner.snapshot();CHECK(stopped->state==SafetyTelemetryOwnerState::Stopped&&!stopped->reply&&!stopped->pending);
        release=true;owner.stop();const auto after=owner.snapshot();
        CHECK(observedCancel&&flights==1&&!owner.start(8)&&!after->pending&&!after->reply&&after->completed==0);
        CHECK(after->unreaped==(status==LocalTaskStatus::Unreaped));
        if(status==LocalTaskStatus::Unreaped){CHECK(after->state==SafetyTelemetryOwnerState::Faulted&&after->transport==LocalTaskStatus::Unreaped&&after->request&&after->request->epoch==7);
            owner.requestStop();owner.stop();CHECK(owner.snapshot()->unreaped&&owner.snapshot()->transport==LocalTaskStatus::Unreaped&&owner.snapshot()->state==SafetyTelemetryOwnerState::Faulted);}
        else CHECK(after->state==SafetyTelemetryOwnerState::Stopped);
        CHECK(before->pending&&stopped->state==SafetyTelemetryOwnerState::Stopped&&!stopped->unreaped);
    }
    return finishTests("safety telemetry late completion");
}
