// Memory-only original explicit-session permission; no native process is created here.
#include "product/native_process_admission.h"
#include "product/session_intent.h"
#include "product_test_util.h"
using namespace om;
int main(){
    SessionIntent intent;auto first=intent.explicitStart();CHECK(intent.permits(first));
    NativeProcessAdmission unrelated;CHECK(unrelated.begin());unrelated.markUnresolved();
    CHECK(!intent.permits(first)); // foreign ownership uncertainty revokes the whole old intent, not merely a worker retry
    SessionIntent other;CHECK(!other.explicitStart().epoch);
    unrelated.settled();CHECK(!nativeProcessUnresolved());
    CHECK(!intent.permits(first)&&!intent.explicitStart().epoch); // even a transient cleared fault cannot resurrect it
    intent.stop();const auto replacement=intent.explicitStart();CHECK(replacement.epoch>first.epoch&&intent.permits(replacement));
    CHECK(unrelated.begin());unrelated.markUnresolved();intent.stop();
    CHECK(!intent.permits(replacement)&&!intent.explicitStart().epoch);
    unrelated.settled();const auto final=intent.explicitStart();CHECK(final.epoch>replacement.epoch&&intent.permits(final));
    PolicyConstraints forced;forced.forceResume=true;
    CHECK(unrelated.begin());unrelated.markUnresolved();
    CHECK(arbitrateMining(intent,final,forced)==MiningDecision::Stopped);
    unrelated.settled();CHECK(arbitrateMining(intent,final,forced)==MiningDecision::Stopped);
    return finishTests("original session/native uncertainty fencing (memory only)");
}
