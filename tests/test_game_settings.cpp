// Private synthetic files and pure native UI state. No detector/helper/GPU/game activation.
#include "cpu_telemetry_fixture.h"
#include "product/settings.h"
#include "product_test_util.h"
using namespace om;
namespace {
std::string changed(std::string s,const std::string &before,const std::string &after){const auto at=s.find(before);if(at==s.npos)std::abort();s.replace(at,before.size(),after);return s;}
std::string contents(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void parsing(){
    GamePreferences value;std::string error;CHECK(validGamePreferences(value));const auto original=gamePreferencesJson(value);
    CHECK(parseGamePreferences(original,&value,&error));
    for(auto mode:{GameIntent::Off,GameIntent::Pause,GameIntent::Coexist})for(auto scope:{GameProcessAdapter::WindowsSessionImage,GameProcessAdapter::LinuxUidNativeImage}){
        value.mode=mode;value.adapter=scope;value.executable="fixture-game.exe";GamePreferences other;
        CHECK(parseGamePreferences(gamePreferencesJson(value),&other,&error)&&other==value);
        CHECK(validGameProcessTarget(gamePreferencesTarget(value)));
        CHECK(gamePreferencesDetection(value).enabled==(mode!=GameIntent::Off)&&gamePreferencesDetection(value).pollMs==value.pollMs);
        const auto guard=gamePreferencesGuard(value);CHECK(guard.mode==mode&&guard.exitDebounceMs==value.exitDebounceMs&&guard.maximumDetectionGapMs==value.maximumDetectionAgeMs&&guard.tenMinuteProfitRule&&guard.coexistence==ForecastQualification::Unsupported);
    }
    for(const std::string &bad:std::vector<std::string>{"{}","[]",changed(original,"\"schema_version\": 1","\"schema_version\": 9"),
        changed(original,"\"poll_ms\": 500","\"poll_ms\": 99"),changed(original,"\"poll_ms\": 500","\"poll_ms\": 10001"),
        changed(original,"\"poll_ms\": 500","\"poll_ms\": 1.0"),changed(original,"\"exit_debounce_ms\": 2000","\"exit_debounce_ms\": -1"),
        changed(original,"\"maximum_detection_age_ms\": 2000","\"maximum_detection_age_ms\": 0"),
        changed(original,"\"mode\": \"off\"","\"mode\": \"pause\""),changed(original,"\"mode\": \"off\"","\"mode\": true"),
        changed(original,"\"target_scope\": ","\"unknown_field\": "),changed(original,"\"schema_version\": 1","\"schema_version\": 1, \"schema_version\": 1"),
        changed(original,"\"target_executable\": \"\"","\"target_executable\": \"../private-target\""),
        changed(original,"\"target_executable\": \"\"","\"target_executable\": \"fixture*.exe\""),
        changed(original,"\"target_executable\": \"\"","\"target_executable\": \"fixture\\u001btarget\""),std::string(4097,'x')}){
        const auto old=value;CHECK(!parseGamePreferences(bad,&value,&error)&&value==old);CHECK(error.find("private-target")==error.npos);
    }
    value={};value.pollMs=100;value.exitDebounceMs=0;value.maximumDetectionAgeMs=30000;CHECK(validGamePreferences(value));
    value.adapter=static_cast<GameProcessAdapter>(99);CHECK(!validGamePreferences(value));value={};value.mode=static_cast<GameIntent>(99);CHECK(!validGamePreferences(value));
}
void storage(const std::filesystem::path &dir){
    const auto path=dir/std::filesystem::u8path(u8"fixture-游戏.json");GamePreferences value;std::string error;GamePreferencesStore first(path),other(path);
    CHECK(!first.save(value,&error));CHECK(first.load(&value,&error)&&!std::filesystem::exists(path));
    value.mode=GameIntent::Pause;value.executable="fixture-game.exe";CHECK(first.save(value,&error));GamePreferences copy;
    CHECK(other.load(&copy,&error)&&copy==value);value.pollMs=1000;CHECK(first.save(value,&error));
    const auto saved=contents(path);CHECK(!other.save(copy,&error)&&error.find("externally")!=error.npos&&contents(path)==saved);
    CHECK(other.load(&copy,&error)&&copy==value);auto lock=path;lock+=".lock";
    {RuntimePreferencesLease lease;CHECK(lease.acquire(lock));CHECK(!other.save(value,&error));}
    CHECK(other.save(value,&error));
    {std::ofstream f(path);f<<"{\"schema_version\":99}";}
    CHECK(!other.load(&copy,&error)&&copy==value);CHECK(!other.save(value,&error)&&contents(path)=="{\"schema_version\":99}");
    {std::ofstream f(path);f<<saved;}
    CHECK(!other.save(value,&error)); // even restored original bytes need a successful reopen after failed load
    CHECK(other.load(&copy,&error));CHECK(other.save(value,&error));
    {std::ofstream f(path);f<<std::string(4097,'x');}
    CHECK(!other.load(&copy,&error)&&copy==value);
#if !defined(_WIN32)
    const auto symlink=dir/"fixture-link.json";std::filesystem::create_symlink(path.filename(),symlink);GamePreferencesStore linked(symlink);
    CHECK(!linked.load(&copy,&error)&&!linked.save(value,&error));
#endif
}
void panel(){
    GameSettingsPanel p;GameSettingsContext context;context.running=true;context.effective=GamePreferences{};p.open({},context);
    unsigned saved=0;auto save=[&](const GamePreferences &,std::string *){++saved;return true;};
    p.key('\r',save);CHECK(p.draft.mode==GameIntent::Pause&&p.dirty());p.key('v',save);CHECK(p.page==GameSettingsPage::Browse&&p.field==1&&!p.message.empty()&&saved==0);
    p.key('\r',save);for(char c:std::string("QDLS.exe"))CHECK(p.key(c,save)==GameSettingsResult::None);
    p.key('\r',save);CHECK(p.draft.executable=="QDLS.exe");p.key('v',save);CHECK(p.page==GameSettingsPage::Review&&saved==0);
    const auto review=p.render(100,30,ColorLevel::None);
    CHECK_MSG(review.c_str(),review.find("non-off request refuses startup")!=std::string::npos);
    CHECK(review.find("Current session (frozen):\nMode: off\n")!=review.npos&&review.find("Target:  -> QDLS.exe")!=review.npos);
    p.key('\r',[](const GamePreferences &,std::string *e){*e="fixture save failed";return false;});CHECK(p.dirty()&&p.message=="fixture save failed");
    p.key('\r',save);CHECK(saved==1&&!p.dirty()&&p.context.effective->mode==GameIntent::Off&&p.context.effective->executable.empty());
    p.field=0;p.key(' ',save);CHECK(p.draft.mode==GameIntent::Coexist);p.key(27,save);CHECK(p.page==GameSettingsPage::Leave);
    p.key(27,save);CHECK(p.dirty());p.key(27,save);CHECK(p.key('d',save)==GameSettingsResult::Close&&!p.visible&&!p.dirty());
    p.open({},context);p.field=3;p.key('\r',save);p.key('q',save);p.key('\r',save);CHECK(p.page==GameSettingsPage::Edit&&p.input=="q"&&saved==1);
    p.key(27,save);CHECK(!p.dirty());p.key('\r',save);for(char c:std::string("10001"))p.key(c,save);p.key('\r',save);CHECK(p.page==GameSettingsPage::Edit&&!p.dirty());
    p.key(27,save);p.key('\r',save);CHECK(p.key(3,save)==GameSettingsResult::Quit);
    p.open({},context);p.draft.executable=std::string(124,'x')+".exe";
    for(int width:{20,40,80,120})for(int rows:{12,16,24}){
        p.page=GameSettingsPage::Browse;p.field=5;auto frame=p.render(width,rows,ColorLevel::None);CHECK(frame.find("> Max age ms")!=frame.npos);
        CHECK(std::count(frame.begin(),frame.end(),'\n')<rows);
        size_t begin=0;for(size_t end=frame.find('\n');end!=frame.npos;end=frame.find('\n',begin)){CHECK(end-begin<static_cast<size_t>(width));begin=end+1;}
        p.key('v');for(unsigned i=0;i<200;++i)p.key(kKeyPageDown);frame=p.render(width,rows,ColorLevel::None);
        CHECK(frame.find("[End of game")!=frame.npos);p.key(27);
    }
    p.field=1;p.key('\r');CHECK(p.render(20,12,ColorLevel::None).find(".exe_")!=std::string::npos);
}
void independent(const std::filesystem::path &dir){
    SettingsPanel p;p.open({});p.draft.color=false;p.group=5;p.groupsFocused=false;unsigned ui=0,game=0;
    auto uiSave=[&](const UiPreferences &,std::string *){++ui;return true;};CHECK(p.fields()==1&&p.key('\r',uiSave)==SettingsResult::OpenGame);
    GamePreferencesStore store(dir/"separate-game.json");openGameControls(&p,&store,{});CHECK(p.game.visible&&p.dirty());
    p.game.draft.pollMs=1000;auto key=[&](int c){return p.key(c,uiSave,{},{},0,{},{},[&](const GamePreferences &v,std::string *e){++game;return store.save(v,e);});};
    key('v');key('\r');CHECK(game==1&&ui==0&&p.game.active.pollMs==1000&&p.dirty());key(27);CHECK(!p.game.visible&&p.visible);
    openGameControls(&p,nullptr,{});CHECK(!p.game.visible&&!p.message.empty());openGameControls(&p,&store,{});p.game.field=1;key('\r');CHECK(key('Q')==SettingsResult::None&&p.game.input=="Q");CHECK(key(3)==SettingsResult::Quit);
}
}
int main(){const auto dir=std::filesystem::path(makeScratchDir("game-settings"));parsing();storage(dir);panel();independent(dir);
    std::error_code error;std::filesystem::remove_all(dir,error);return finishTests("native game defaults");}
