// Denial-only stale caller checks around a genuinely healthy newer native panel/helper; owned CPU files only.
#include "product/preferences_settings.h"
#include "product_test_util.h"
#include <thread>
namespace {
using namespace om;namespace fs=std::filesystem;
template<class Predicate>bool await(Predicate predicate){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);do{if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(std::chrono::steady_clock::now()<until);return false;}
}
int main(int argc,char **argv){using namespace om;if(argc!=2)return 4;
    const auto oldEpoch=nativeProcessEpoch();NativeProcessAdmission older;CHECK(older.begin());older.markUnresolved();older.settled();CHECK(nativeProcessEpochCurrent(nativeProcessEpoch()));
    const auto root=fs::temp_directory_path()/("canary-preferences-foreign-panel-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;const bool owned=fs::create_directory(root,error);CHECK(owned);if(!owned)return finishTests("preference foreign panel scratch refusal");
    struct Cleanup{fs::path root;~Cleanup(){std::error_code ignored;fs::remove_all(root,ignored);}} cleanup{root};
    SessionIntent intent;const auto token=intent.explicitStart();std::array<fs::path,8> targets{};targets[0]=root/"ui.json";targets[2]=root/"manual.json";
    PreferencesOwner owner(fs::absolute(fs::u8path(argv[1])),targets,intent,token);SettingsPanel panel;panel.open({});panel.groupsFocused=false;panel.field=1;PreferencesSettings bridge(owner,panel);
    CHECK(owner.start());CHECK(await([&]{return owner.snapshot().state==PreferencesOwnerState::Ready;}));CHECK(bridge.load(PreferencesKind::Ui));CHECK(await([&]{return static_cast<bool>(owner.snapshot().reply);}));
    const auto before=owner.snapshot();const auto draft=panel.draft;const auto message=panel.message;
    {
        NativeProcessScope old(oldEpoch);
        CHECK(bridge.key(3,1700000000,12345)==SettingsResult::None);
        CHECK(!bridge.poll()&&bridge.pending());
        bridge.key(' ',1700000000,12345);CHECK(panel.draft==draft&&panel.message==message);
        CHECK(!bridge.load(PreferencesKind::Ui)&&panel.message==message);
        CHECK(bridge.render(80,24,ColorLevel::None).find("unavailable")!=std::string::npos);
    }
    const auto after=owner.snapshot();CHECK(after.reply==before.reply&&after.reviews==before.reviews&&after.submitted==before.submitted&&intent.permits(token));
    auto completion=bridge.poll();CHECK(completion&&completion->reply==before.reply&&!bridge.pending());
    if(completion){
        CHECK(bridge.load(PreferencesKind::Manual));completion.reset();CHECK(await([&]{completion=bridge.poll();return static_cast<bool>(completion);}));
        panel.manual.draft.enabled=true;panel.manual.draft.prlUsdt="2";panel.manual.draft.prlAsOfUtc=1699999999;panel.manual.page=ManualMarketSettingsPage::Review;
        bridge.context.manualProjectionAvailable=true;unsigned factories=0;bridge.context.manualFactory=[&](const auto &,uint64_t,uint64_t){++factories;return ManualMarketProjector{};};
        const auto originalDraft=panel.manual.draft;const auto originalMessage=panel.manual.message;const auto submitted=owner.snapshot().submitted;
        {NativeProcessScope old(oldEpoch);CHECK(bridge.key('\r',1700000000,12345)==SettingsResult::None);}
        CHECK(factories==0&&panel.manual.draft==originalDraft&&panel.manual.message==originalMessage&&owner.snapshot().submitted==submitted&&!bridge.pending());
        bridge.key('\r',1700000000,12345);CHECK(factories==1&&bridge.pending());completion.reset();CHECK(await([&]{completion=bridge.poll();return static_cast<bool>(completion);}));
        CHECK(completion&&completion->reply->status==PreferencesReplyStatus::Saved&&fs::is_regular_file(targets[2]));
    }
    owner.stop();CHECK(owner.snapshot().settled&&!fs::exists(targets[0]));
    return finishTests("foreign preference panel callers cannot quit/mutate/orphan a healthy newer owner's pending reply or disclose cached review");
}
