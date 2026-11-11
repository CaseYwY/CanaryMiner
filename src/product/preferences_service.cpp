#include "preferences_service.h"
#include "local_task_process.h"
#include "ui_preferences.h"
#include "runtime_preferences.h"
#include "manual_market_preferences.h"
#include "electricity_preferences.h"
#include "game_preferences.h"
#include "pool_security_preferences.h"
#include "language_preferences.h"
#include "tariff_store.h"
#include <array>
#include <tuple>
namespace om {
namespace {
bool parseTariff(const std::string &text,JsonValue *value,std::string *){return parseTariffDeclaration(text,value,nullptr);}
std::string encodeTariff(const JsonValue &value){return tariffJson(value)+"\n";}
template<class Value,class Store,class Parse,class Encode>
void apply(const PreferencesRequest &request,PreferencesReply &reply,uint64_t &review,Store &store,Parse parse,Encode encode){
    std::string error;Value value;
    if(request.operation==PreferencesOperation::Load){
        review=0;
        if(!store.load(&value,&error))return;
        reply.document=encode(value);reply.review=request.ticket;reply.status=PreferencesReplyStatus::Loaded;
        std::string wire;if(!encodePreferencesReply(reply,&wire)){reply.document.clear();reply.review=0;reply.status=PreferencesReplyStatus::Rejected;return;}
        review=request.ticket;return;
    }
    if(!review||request.review!=review||!parse(request.document,&value,&error)||encode(value)!=request.document)return;
    // Prove the exact response is representable BEFORE any file mutation. Never write a value whose acknowledgement cannot fit.
    PreferencesReply success=reply;success.document=request.document;success.review=request.ticket;success.status=PreferencesReplyStatus::Saved;
    std::string wire;if(!encodePreferencesReply(success,&wire))return;
    review=0;
    if(!store.save(value,&error)){reply.status=PreferencesReplyStatus::Unknown;return;} // bool store failures cannot prove no replacement occurred
    review=request.ticket;reply=std::move(success);
}
}
struct PreferencesService::Impl {
    using Stores=std::tuple<UiPreferencesStore,RuntimePreferencesStore,ManualMarketStore,ElectricityPreferencesStore,GamePreferencesStore,PoolSecurityPreferencesStore,LanguagePreferencesStore,TariffDeclarationStore>;
    Stores stores;std::array<uint64_t,8> reviews{};std::array<bool,8> enabled{};uint64_t ticket=1;
    explicit Impl(const std::array<std::filesystem::path,8> &paths):stores(UiPreferencesStore(paths[0]),RuntimePreferencesStore(paths[1]),ManualMarketStore(paths[2]),ElectricityPreferencesStore(paths[3]),
        GamePreferencesStore(paths[4]),PoolSecurityPreferencesStore(paths[5]),LanguagePreferencesStore(paths[6]),TariffDeclarationStore(paths[7])){for(size_t i=0;i<8;++i)enabled[i]=!paths[i].empty();}
};
PreferencesService::PreferencesService()=default;
PreferencesService::~PreferencesService()=default;
bool PreferencesService::transact(const PreferencesRequest &request,PreferencesReply *out){
    if(!out||!validPreferencesRequest(request))return false;
    PreferencesReply reply;reply.ticket=request.ticket;reply.kind=request.kind;
    if(request.operation==PreferencesOperation::Init){
        if(impl_)return false;
        std::array<std::filesystem::path,8> paths{};
        for(size_t i=0;i<8;++i)if(!request.targets[i].empty()){
            paths[i]=std::filesystem::u8path(request.targets[i]);if(!localTaskPath(paths[i])||paths[i].lexically_normal()!=paths[i])return false;
            for(size_t j=0;j<i;++j)if(paths[i]==paths[j])return false;
        }
        impl_.reset(new Impl(paths));reply.status=PreferencesReplyStatus::Ready;*out=std::move(reply);return true;
    }
    if(!impl_||request.ticket!=impl_->ticket+1)return false;
    auto &state=*impl_;state.ticket=request.ticket;
    if(!state.enabled[static_cast<unsigned>(request.kind)]){*out=std::move(reply);return true;}
    auto &review=state.reviews[static_cast<unsigned>(request.kind)];
    switch(request.kind){
        case PreferencesKind::Ui:apply<UiPreferences>(request,reply,review,std::get<0>(state.stores),parseUiPreferences,uiPreferencesJson);break;
        case PreferencesKind::Runtime:apply<RuntimePreferences>(request,reply,review,std::get<1>(state.stores),parseRuntimePreferences,runtimePreferencesJson);break;
        case PreferencesKind::Manual:apply<ManualMarketPreferences>(request,reply,review,std::get<2>(state.stores),parseManualMarket,manualMarketJson);break;
        case PreferencesKind::Electricity:apply<ElectricityPreferences>(request,reply,review,std::get<3>(state.stores),parseElectricityPreferences,electricityPreferencesJson);break;
        case PreferencesKind::Game:apply<GamePreferences>(request,reply,review,std::get<4>(state.stores),parseGamePreferences,gamePreferencesJson);break;
        case PreferencesKind::Pool:apply<PoolSecurityPreferences>(request,reply,review,std::get<5>(state.stores),parsePoolSecurityPreferences,poolSecurityPreferencesJson);break;
        case PreferencesKind::Language:apply<LanguagePreferences>(request,reply,review,std::get<6>(state.stores),parseLanguagePreferences,languagePreferencesJson);break;
        case PreferencesKind::Tariff:apply<JsonValue>(request,reply,review,std::get<7>(state.stores),parseTariff,encodeTariff);break;
    }
    *out=std::move(reply);return true;
}
} // namespace om
