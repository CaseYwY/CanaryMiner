// Actual confirmed helper receipts held under control-thread allocation denial; no GPU/game/mining/source access.
#include "product/preferences_settings.h"
#include "product_test_util.h"
#include <new>
#include <thread>
namespace {
using namespace om;namespace fs=std::filesystem;
thread_local bool rejectAllocations=false;thread_local unsigned allocationAttempts=0;
template<class Predicate>bool await(Predicate predicate){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);do{if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(std::chrono::steady_clock::now()<until);return false;}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations)return false;++allocationAttempts;return true;}
int main(int argc,char **argv){using namespace om;if(argc!=2)return 4;
    const auto root=fs::temp_directory_path()/("canary-preferences-completion-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;const bool owned=fs::create_directory(root,error);CHECK(owned);if(!owned)return finishTests("preference completion scratch refusal");
    struct Cleanup{fs::path root;~Cleanup(){std::error_code ignored;fs::remove_all(root,ignored);}} cleanup{root};
    SessionIntent intent;const auto token=intent.explicitStart();std::array<fs::path,8> targets{};targets[0]=root/"ui.json";
    PreferencesOwner owner(fs::absolute(fs::u8path(argv[1])),targets,intent,token);SettingsPanel panel;panel.open({});PreferencesSettings bridge(owner,panel);
    CHECK(owner.start());CHECK(await([&]{return owner.snapshot().state==PreferencesOwnerState::Ready;}));CHECK(bridge.load(PreferencesKind::Ui));
    CHECK(await([&]{return static_cast<bool>(owner.snapshot().reply);}));const auto loadedTicket=owner.snapshot().submitted;
    bool threw=false;allocationAttempts=0;rejectAllocations=true;std::shared_ptr<PreferencesSettingsCompletion> completion;
    try{completion=bridge.poll();}catch(...){threw=true;}rejectAllocations=false;
    CHECK(!threw&&!completion&&allocationAttempts>0&&bridge.pending()&&owner.snapshot().submitted==loadedTicket&&!owner.snapshot().reply&&!fs::exists(targets[0]));
    completion=bridge.poll();CHECK(completion&&completion->reply->ticket==loadedTicket&&!bridge.pending());
    const auto original=panel.active;panel.draft.hideWorker=true;panel.page=SettingsPage::Review;bridge.key('\r',1700000000,12345);CHECK(bridge.pending()&&panel.active==original);
    CHECK(await([&]{return static_cast<bool>(owner.snapshot().reply);}));const auto savedTicket=owner.snapshot().submitted;CHECK(fs::is_regular_file(targets[0]));
    threw=false;allocationAttempts=0;rejectAllocations=true;
    try{completion=bridge.poll();}catch(...){threw=true;}rejectAllocations=false;
    CHECK(!threw&&!completion&&allocationAttempts>0&&bridge.pending()&&panel.active==original&&owner.snapshot().submitted==savedTicket&&!owner.snapshot().reply);
    completion=bridge.poll();CHECK(completion&&completion->reply->ticket==savedTicket&&completion->reply->status==PreferencesReplyStatus::Saved&&!bridge.pending()&&panel.active.hideWorker);
    CHECK(owner.snapshot().submitted==3&&intent.permits(token));owner.requestStop();owner.stop();CHECK(owner.snapshot().settled);
    return finishTests("native private Load/Saved completion allocation failure retains the exact receipt/draft without reissuing I/O");
}
