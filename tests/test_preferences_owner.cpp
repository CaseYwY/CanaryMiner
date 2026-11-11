// Real owned CPU helper/files only; no GPU, game, mining, provider, credentials or installed miner access.
#include "product/preferences_owner.h"
#include "product/preferences_document.h"
#include "product/ui_preferences.h"
#include "product/tariff_declaration.h"
#include "product_test_util.h"
#include <fstream>
#include <new>
#include <sstream>
#include <thread>
namespace {
using namespace om;namespace fs=std::filesystem;
thread_local bool rejectAllocations=false;thread_local unsigned allocationAttempts=0;
void put(const fs::path &path,const std::string &text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;CHECK(file.good());}
std::string read(const fs::path &path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
template<class Predicate>bool await(PreferencesOwner &owner,Predicate predicate){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do {if(predicate(owner.snapshot()))return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(std::chrono::steady_clock::now()<end);
    return false;
}
std::shared_ptr<const PreferencesReply> completed(PreferencesOwner &owner,uint64_t ticket){
    CHECK(await(owner,[&](const auto &view){return view.completed==ticket||view.state==PreferencesOwnerState::Faulted||view.state==PreferencesOwnerState::Stopped;}));
    auto reply=owner.takeReply(ticket);CHECK(reply&&reply->ticket==ticket);return reply;
}
void nativeFault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
}
bool canaryNativeAllocationFault(){if(!rejectAllocations)return false;++allocationAttempts;return true;}
int main(int argc,char **argv){using namespace om;if(argc!=3)return 4;
    const std::string scenario=argv[2];
    if(scenario!="basic"&&scenario!="foreign"&&scenario!="stop"&&scenario!="fault"&&scenario!="allocation"&&scenario!="stopped-before-start"&&scenario!="lost"&&scenario!="wrong-reply"&&scenario!="invalid-loaded"&&scenario!="hold-load"&&scenario!="hold-save"&&scenario!="hold-save-allocation"&&scenario!="invalid-target"&&scenario!="admission-allocation")return 4;
    const auto originalEpoch=nativeProcessEpoch();if(scenario=="foreign")nativeFault();
    const auto root=fs::temp_directory_path()/("canary-preferences-owner-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;const bool owned=fs::create_directory(root,error);CHECK(owned);if(!owned)return finishTests("preference owner scratch refusal");
    struct Cleanup{fs::path root;~Cleanup(){std::error_code ignored;fs::remove_all(root,ignored);}} cleanup{root};
    std::array<fs::path,8> targets{};for(unsigned i=0;i<8;++i)targets[i]=root/(std::string(preferencesKindName(static_cast<PreferencesKind>(i)))+".json");
    if(scenario=="invalid-target")targets[0]=root/"alias"/".."/"ui.json";
    SessionIntent intent;const auto token=intent.explicitStart();CHECK(intent.permits(token));
    PreferencesOwnerPolicy policy;policy.operationTimeoutMs=(scenario=="lost"||scenario=="wrong-reply"||scenario=="invalid-loaded")?1000:5000;policy.reapMs=1000;
    PreferencesOwner owner(fs::absolute(fs::u8path(argv[1])),targets,intent,token,policy);
    CHECK(owner.snapshot().state==PreferencesOwnerState::Dormant);
    if(scenario=="stopped-before-start"){
        owner.requestStop();CHECK(owner.snapshot().state==PreferencesOwnerState::Stopped&&!owner.start()&&fs::is_empty(root));
        return finishTests("preference owner stop-before-start terminality");
    }
    if(scenario=="foreign"){{NativeProcessScope old(originalEpoch);CHECK(!owner.start());CHECK(owner.snapshot().state==PreferencesOwnerState::Stopped);}CHECK(owner.snapshot().state==PreferencesOwnerState::Dormant);}
    if(scenario=="invalid-target"){
        CHECK(owner.start());CHECK(await(owner,[](const auto &v){return v.state==PreferencesOwnerState::Faulted;}));
        CHECK(owner.snapshot().transport==LocalTaskStatus::Invalid&&owner.snapshot().settled&&owner.snapshot().completed==0&&fs::is_empty(root));owner.stop();
        return finishTests("ambiguous configured target refused before helper creation instead of silent path rewrite");
    }
    CHECK(owner.start());CHECK(!owner.start());CHECK(await(owner,[](const auto &v){return v.state==PreferencesOwnerState::Ready||v.state==PreferencesOwnerState::Faulted;}));
    CHECK(owner.snapshot().state==PreferencesOwnerState::Ready&&owner.snapshot().completed==1&&fs::is_empty(root));
    if(scenario=="stop"||scenario=="fault"||scenario=="allocation"){
        if(scenario=="fault")nativeFault();else intent.stop();
        if(scenario=="allocation"){
            bool injected=false;rejectAllocations=true;try{void *probe=::operator new(1);::operator delete(probe);}catch(const std::bad_alloc &){injected=true;}rejectAllocations=false;CHECK(injected&&allocationAttempts==1);allocationAttempts=0;
            rejectAllocations=true;const auto view=owner.snapshot();rejectAllocations=false;CHECK(view.state==PreferencesOwnerState::Stopped&&allocationAttempts==0);
        }else CHECK(owner.snapshot().state==(scenario=="fault"?PreferencesOwnerState::Faulted:PreferencesOwnerState::Stopped));
        CHECK(owner.load(PreferencesKind::Ui).status==PreferencesAdmission::Inactive&&!owner.start());owner.stop();CHECK(owner.snapshot().settled&&fs::is_empty(root));
        return finishTests("preference owner current-parent revocation without polling/I/O/allocation");
    }
    if(scenario=="admission-allocation"){
        const auto before=owner.snapshot();allocationAttempts=0;rejectAllocations=true;const auto failed=owner.load(PreferencesKind::Ui);rejectAllocations=false;
        CHECK(failed.status==PreferencesAdmission::Invalid&&allocationAttempts>0&&owner.snapshot().submitted==before.submitted&&!owner.snapshot().pending);
    }
    auto submission=owner.load(PreferencesKind::Ui);CHECK(submission.status==PreferencesAdmission::Queued);
    if(scenario=="hold-load"){
        CHECK(owner.load(PreferencesKind::Runtime).status==PreferencesAdmission::Busy);
        owner.requestStop();CHECK(owner.snapshot().state==PreferencesOwnerState::Stopped&&!owner.snapshot().uncertain&&intent.permits(token));
        owner.stop();CHECK(owner.snapshot().settled&&fs::is_empty(root));return finishTests("held native preference read cannot block memory stop/control");
    }
    if(scenario=="invalid-loaded"){
        CHECK(await(owner,[](const auto &v){return v.state==PreferencesOwnerState::Faulted;}));CHECK(owner.snapshot().transport==LocalTaskStatus::Invalid&&!owner.takeReply(submission.ticket)&&!owner.snapshot().uncertain);owner.stop();CHECK(owner.snapshot().settled);return finishTests("noncanonical helper data withheld before current review");
    }
    CHECK(await(owner,[&](const auto &v){return v.completed==submission.ticket;}));
    const auto untouched=owner.snapshot();CHECK(untouched.reply&&untouched.reply->status==PreferencesReplyStatus::Loaded);
    if(scenario=="foreign"){{NativeProcessScope old(originalEpoch);
        CHECK(owner.snapshot().state==PreferencesOwnerState::Stopped&&!owner.takeReply(submission.ticket));
        CHECK(owner.load(PreferencesKind::Ui).status==PreferencesAdmission::Inactive&&owner.save(PreferencesKind::Ui,untouched.reviews[0],untouched.reply->document).status==PreferencesAdmission::Inactive);
        owner.requestStop();owner.stop();CHECK(!owner.acknowledgeInspection(PreferencesKind::Ui,untouched.reviews[0]));}
        const auto after=owner.snapshot();CHECK(after.state==untouched.state&&after.submitted==untouched.submitted&&after.reviews==untouched.reviews&&after.reply==untouched.reply);
        CHECK(owner.takeReply(submission.ticket)==untouched.reply);owner.stop();CHECK(owner.snapshot().settled);return finishTests("foreign preference callers cannot start/consume/cancel a healthy parent");
    }
    auto reply=owner.takeReply(submission.ticket);if(!reply)return finishTests("preference owner missing initial reply");
    std::array<std::string,8> documents{};documents[0]=reply->document;
    if(scenario=="admission-allocation"){
        const auto before=owner.snapshot();bool threw=false;PreferencesSubmission failed;allocationAttempts=0;rejectAllocations=true;
        try{failed=owner.save(PreferencesKind::Ui,reply->review,documents[0]);}catch(const std::bad_alloc &){threw=true;}rejectAllocations=false;
        CHECK(!threw&&failed.status==PreferencesAdmission::Invalid&&allocationAttempts>0);
        const auto after=owner.snapshot();CHECK(after.submitted==before.submitted&&after.reviews==before.reviews&&!after.pending&&!fs::exists(targets[0]));
        submission=owner.save(PreferencesKind::Ui,reply->review,documents[0]);CHECK(submission.status==PreferencesAdmission::Queued);reply=completed(owner,submission.ticket);
        CHECK(reply&&reply->status==PreferencesReplyStatus::Saved&&read(targets[0])==documents[0]);owner.stop();CHECK(owner.snapshot().settled);
        return finishTests("preference admission allocation failure preserves healthy original review and never escapes the callback");
    }
    auto ui=UiPreferences{};ui.hideWorker=true;const auto changed=uiPreferencesJson(ui);
    if(scenario=="lost"||scenario=="wrong-reply"||scenario=="hold-save"||scenario=="hold-save-allocation"){
        submission=owner.save(PreferencesKind::Ui,reply->review,changed);CHECK(submission.status==PreferencesAdmission::Queued);
        if(scenario=="hold-save"||scenario=="hold-save-allocation"){
            CHECK(await(owner,[](const auto &v){return v.inFlight;}));
            if(scenario=="hold-save-allocation"){
                intent.stop();allocationAttempts=0;rejectAllocations=true;const auto view=owner.snapshot();rejectAllocations=false;
                CHECK(view.state==PreferencesOwnerState::Stopped&&view.uncertain&&allocationAttempts==0);
            }else owner.requestStop();
            CHECK(owner.snapshot().state==PreferencesOwnerState::Stopped);
        }else CHECK(await(owner,[](const auto &v){return v.state==PreferencesOwnerState::Faulted;}));
        const auto failed=owner.snapshot();
        CHECK(failed.uncertain&&failed.uncertain->ticket==submission.ticket&&failed.uncertain->document==changed&&!owner.takeReply(submission.ticket));
        if(scenario=="lost"||scenario=="wrong-reply")CHECK(read(targets[0])==changed&&failed.transport==(scenario=="lost"?LocalTaskStatus::Timeout:LocalTaskStatus::Invalid));
        CHECK(!owner.start()&&owner.load(PreferencesKind::Ui).status==PreferencesAdmission::Inactive);owner.stop();CHECK(owner.snapshot().settled&&owner.snapshot().uncertain==failed.uncertain);
        return finishTests("issued private preference original survives lost/mismatched/cancelled native acknowledgement");
    }
    for(unsigned i=1;i<8;++i){submission=owner.load(static_cast<PreferencesKind>(i));CHECK(submission.status==PreferencesAdmission::Queued);reply=completed(owner,submission.ticket);if(!reply)return finishTests("preference owner missing load");documents[i]=reply->document;}
    CHECK(canonicalPreferencesDocument(PreferencesKind::Tariff,documents[7],true)&&!canonicalPreferencesDocument(PreferencesKind::Tariff,documents[7]));
    CHECK(owner.save(PreferencesKind::Tariff,owner.snapshot().reviews[7],documents[7]).status==PreferencesAdmission::Invalid);
    std::ifstream fixture("config/tariff-declaration.example.json");std::stringstream text;text<<fixture.rdbuf();JsonValue tariff;CHECK(parseTariffDeclaration(text.str(),&tariff,nullptr));documents[7]=tariffJson(tariff)+"\n";
    for(unsigned i=0;i<8;++i){const auto kind=static_cast<PreferencesKind>(i);submission=owner.save(kind,owner.snapshot().reviews[i],documents[i]);CHECK(submission.status==PreferencesAdmission::Queued);
        CHECK(owner.load(kind).status==PreferencesAdmission::Busy);reply=completed(owner,submission.ticket);if(!reply)return finishTests("preference owner missing save");CHECK(reply->status==PreferencesReplyStatus::Saved&&read(targets[i])==documents[i]);}
    const auto before=owner.snapshot();CHECK(owner.save(PreferencesKind::Ui,before.reviews[0],documents[0]+"\n").status==PreferencesAdmission::Invalid&&owner.snapshot().reviews==before.reviews&&owner.snapshot().submitted==before.submitted);
    put(targets[0],documents[0]+"\n");submission=owner.save(PreferencesKind::Ui,before.reviews[0],documents[0]);CHECK(submission.status==PreferencesAdmission::Queued);reply=completed(owner,submission.ticket);if(!reply)return finishTests("preference owner missing unknown");
    const auto unknown=owner.snapshot();CHECK(reply->status==PreferencesReplyStatus::Unknown&&unknown.state==PreferencesOwnerState::NeedsInspection&&unknown.uncertain&&unknown.uncertain->document==documents[0]);
    CHECK(owner.load(PreferencesKind::Runtime).status==PreferencesAdmission::NeedsInspection&&owner.save(PreferencesKind::Ui,before.reviews[0],documents[0]).status==PreferencesAdmission::NeedsInspection);
    submission=owner.load(PreferencesKind::Ui);CHECK(submission.status==PreferencesAdmission::Queued&&!owner.acknowledgeInspection(PreferencesKind::Ui,before.reviews[0]));reply=completed(owner,submission.ticket);if(!reply)return finishTests("preference owner missing inspection");
    CHECK(reply->status==PreferencesReplyStatus::Loaded&&reply->document==documents[0]&&read(targets[0])==documents[0]+"\n");
    CHECK(!owner.acknowledgeInspection(PreferencesKind::Runtime,reply->review)&&!owner.acknowledgeInspection(PreferencesKind::Ui,reply->review-1));
    CHECK(owner.acknowledgeInspection(PreferencesKind::Ui,reply->review)&&owner.snapshot().state==PreferencesOwnerState::Ready&&owner.snapshot().acknowledgedUnknown==unknown.uncertain&&!owner.snapshot().uncertain);
    submission=owner.save(PreferencesKind::Ui,reply->review,documents[0]);CHECK(submission.status==PreferencesAdmission::Queued);reply=completed(owner,submission.ticket);CHECK(reply&&reply->status==PreferencesReplyStatus::Saved&&read(targets[0])==documents[0]);
    owner.requestStop();CHECK(owner.snapshot().state==PreferencesOwnerState::Stopped&&!owner.start());owner.stop();CHECK(owner.snapshot().settled);
    return finishTests("background owned private preferences, exact eight-store receipts, explicit unknown inspection and no auto retry");
}
