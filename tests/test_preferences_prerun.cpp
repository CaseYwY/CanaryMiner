// Actual read-only sibling helper plus synthetic original-epoch denial. No devices, mining workers, sources or preference writes.
#include "product/prerun_preferences.h"
#include "product_test_util.h"
#include <fstream>
using namespace om;
int main(){
    if(!platformInit())return 2;
    const auto root=std::filesystem::path(makeScratchDir("preferences-prerun"));
    const RuntimePreferences defaultRuntime;
    const GamePreferences defaultGame;
    const LanguagePreferences defaultLanguage;
    const PoolSecurityPreferences defaultPool;
    PreRunPreferences output;output.nativeEpoch=73;output.runtime.workWaitMs=41;
    volatile sig_atomic_t stop=1;
    CHECK(inspectPreRunPreferences(root.u8string(),&stop,PreRunPreferencesMode::Ordinary,&output)==PreRunPreferencesStatus::Stopped);
    CHECK(output.nativeEpoch==73&&output.runtime.workWaitMs==41&&std::filesystem::is_empty(root));
    stop=0;
    CHECK(inspectPreRunPreferences(root.u8string(),&stop,static_cast<PreRunPreferencesMode>(99),&output)==PreRunPreferencesStatus::Refused);
    CHECK(inspectPreRunPreferences(root.u8string(),&stop,PreRunPreferencesMode::Ordinary,nullptr)==PreRunPreferencesStatus::Refused);
    CHECK(output.nativeEpoch==73&&output.runtime.workWaitMs==41&&std::filesystem::is_empty(root));
    const auto original=nativeProcessEpoch();
    NativeProcessAdmission synthetic;CHECK(synthetic.begin());synthetic.markUnresolved();
    CHECK(inspectPreRunPreferences(root.u8string(),&stop,PreRunPreferencesMode::Ordinary,&output)==PreRunPreferencesStatus::Refused);
    synthetic.settled(); // synthetic no-creation evidence only; not native reaping qualification
    {
        NativeProcessScope old(original);
        CHECK(inspectPreRunPreferences(root.u8string(),&stop,PreRunPreferencesMode::Ordinary,&output)==PreRunPreferencesStatus::Refused);
        CHECK(output.nativeEpoch==73&&output.runtime.workWaitMs==41&&std::filesystem::is_empty(root));
    }
    CHECK(inspectPreRunPreferences(root.u8string(),&stop,PreRunPreferencesMode::Ordinary,&output)==PreRunPreferencesStatus::Ready);
    CHECK(output.nativeEpoch==nativeProcessEpoch()&&output.runtime==defaultRuntime&&output.game==defaultGame);
    CHECK(output.languageAvailable&&output.language==defaultLanguage&&output.pool==defaultPool);
    CHECK(std::filesystem::is_empty(root));
    const std::string bad="{\"schema_version\":99,\"fixture.private\":\"not-for-output\"}";
    for(const auto *name:{"language-preferences.json","runtime-preferences.json","game-preferences.json"})std::ofstream(root/name,std::ios::binary)<<bad;
    CHECK(inspectPreRunPreferences(root.u8string(),&stop,PreRunPreferencesMode::PoolOnly,&output)==PreRunPreferencesStatus::Ready);
    CHECK(output.pool==defaultPool&&output.nativeEpoch==nativeProcessEpoch());
    for(const auto *name:{"language-preferences.json","runtime-preferences.json","game-preferences.json"}){
        std::ifstream input(root/name,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),{}};CHECK(bytes==bad);
    }
    output.nativeEpoch=73;output.runtime.workWaitMs=41;
    CHECK(inspectPreRunPreferences(root.u8string(),&stop,PreRunPreferencesMode::Ordinary,&output)==PreRunPreferencesStatus::Refused);
    CHECK(output.nativeEpoch==73&&output.runtime.workWaitMs==41);
    CHECK(!std::filesystem::exists(root/"pool-security-preferences.json")&&!std::filesystem::exists(root/"ui-preferences.json"));
    std::filesystem::remove_all(root);
    platformShutdown();
    return finishTests("pre-run exact read-only helper defaults, pool-only isolation, preserved rejected outputs and original-epoch/stop denial");
}
