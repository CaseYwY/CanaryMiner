// Owned synthetic preference originals only. A known failed reopen/conflict cannot regain save authority by byte restoration.
#include "product/ui_preferences.h"
#include "product/runtime_preferences.h"
#include "product/manual_market_preferences.h"
#include "product/electricity_preferences.h"
#include "product/game_preferences.h"
#include "product/pool_security_preferences.h"
#include "product/language_preferences.h"
#include "product/tariff_store.h"
#include "product_test_util.h"
#include <sstream>
namespace {
namespace fs=std::filesystem;
void put(const fs::path &path,const std::string &text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(text.data(),static_cast<std::streamsize>(text.size()));CHECK(file.good());}
std::string read(const fs::path &path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
template<class Store,class Value,class Encode>void exercise(const fs::path &root,const char *tag,const Value &original,Encode encode){
    const auto directory=root/tag;CHECK(fs::create_directory(directory));const auto path=directory/"preferences.json";const auto bytes=encode(original);std::string error;
    for(unsigned scenario=0;scenario<7;++scenario){
        put(path,bytes);Store store(path);Value loaded=original;CHECK(store.load(&loaded,&error)&&encode(loaded)==bytes);
        if(scenario==6){CHECK(!store.load(nullptr,&error));}
        else if(scenario<4){
            if(scenario==0)put(path,"{malformed-synthetic-preference");
            if(scenario==1)put(path,std::string(131073,'x'));
            if(scenario==2){CHECK(fs::remove(path));CHECK(fs::create_directory(path));}
            if(scenario==3)put(path,"{\"schema_version\":999}");
            CHECK(!store.load(&loaded,&error)&&encode(loaded)==bytes);
            if(scenario==2)CHECK(fs::remove(path));
        }else{
            if(scenario==4)put(path,bytes+"\n");
            else {CHECK(fs::remove(path));CHECK(fs::create_directory(path));}
            CHECK(!store.save(original,&error)); // a known external conflict/unreadable target must demand a successful reopen
            if(scenario==5)CHECK(fs::remove(path));
        }
        put(path,bytes); // another actor restores the old bytes; this is NOT the required successful reopen
        CHECK(!store.save(original,&error)&&read(path)==bytes);
        CHECK(store.load(&loaded,&error)&&encode(loaded)==bytes&&store.save(original,&error));
        CHECK(read(path)==bytes);
    }
    CHECK(fs::remove(path));Store missing(path);Value loaded=original;CHECK(missing.load(&loaded,&error));
    put(path,"{malformed-synthetic-preference");CHECK(!missing.load(&loaded,&error));CHECK(fs::remove(path));
    CHECK(!missing.save(original,&error)&&!fs::exists(path));
    CHECK(missing.load(&loaded,&error)&&missing.save(original,&error)&&read(path)==bytes);
}
}
int main(){using namespace om;
    const auto root=fs::temp_directory_path()/("canary-preferences-reopen-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;const bool owned=fs::create_directory(root,error);CHECK(owned);if(!owned)return finishTests("preferences scratch refusal");
    struct Cleanup{fs::path path;~Cleanup(){std::error_code ignored;fs::remove_all(path,ignored);}} cleanup{root};
    exercise<UiPreferencesStore>(root,"ui",UiPreferences{},uiPreferencesJson);
    exercise<RuntimePreferencesStore>(root,"runtime",RuntimePreferences{},runtimePreferencesJson);
    exercise<ManualMarketStore>(root,"manual",ManualMarketPreferences{},manualMarketJson);
    exercise<ElectricityPreferencesStore>(root,"electricity",ElectricityPreferences{},electricityPreferencesJson);
    exercise<GamePreferencesStore>(root,"game",GamePreferences{},gamePreferencesJson);
    exercise<PoolSecurityPreferencesStore>(root,"pool",PoolSecurityPreferences{},poolSecurityPreferencesJson);
    exercise<LanguagePreferencesStore>(root,"language",LanguagePreferences{},languagePreferencesJson);
    std::ifstream fixture("config/tariff-declaration.example.json");std::stringstream text;text<<fixture.rdbuf();JsonValue tariff;TariffProblem problem;
    CHECK(fixture.good()&&parseTariffDeclaration(text.str(),&tariff,&problem));
    exercise<TariffDeclarationStore>(root,"tariff",tariff,[](const JsonValue &value){return tariffJson(value)+"\n";});
    return finishTests("failed preference reopen and external-conflict authority latch");
}
