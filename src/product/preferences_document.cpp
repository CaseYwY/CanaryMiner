#include "preferences_document.h"
#include "ui_preferences.h"
#include "runtime_preferences.h"
#include "manual_market_preferences.h"
#include "electricity_preferences.h"
#include "game_preferences.h"
#include "pool_security_preferences.h"
#include "language_preferences.h"
#include "tariff_declaration.h"
namespace om {
namespace {
template<class Value,class Parse,class Encode>bool canonical(const std::string &text,Parse parse,Encode encode){Value value;return parse(text,&value,nullptr)&&encode(value)==text;}
}
bool canonicalPreferencesDocument(PreferencesKind kind,const std::string &text,bool allowEmptyTariffDraft){
    if(text.empty()||text.size()>preferencesDocumentLimit)return false;
    switch(kind){
        case PreferencesKind::Ui:return canonical<UiPreferences>(text,parseUiPreferences,uiPreferencesJson);
        case PreferencesKind::Runtime:return canonical<RuntimePreferences>(text,parseRuntimePreferences,runtimePreferencesJson);
        case PreferencesKind::Manual:return canonical<ManualMarketPreferences>(text,parseManualMarket,manualMarketJson);
        case PreferencesKind::Electricity:return canonical<ElectricityPreferences>(text,parseElectricityPreferences,electricityPreferencesJson);
        case PreferencesKind::Game:return canonical<GamePreferences>(text,parseGamePreferences,gamePreferencesJson);
        case PreferencesKind::Pool:return canonical<PoolSecurityPreferences>(text,parsePoolSecurityPreferences,poolSecurityPreferencesJson);
        case PreferencesKind::Language:return canonical<LanguagePreferences>(text,parseLanguagePreferences,languagePreferencesJson);
        case PreferencesKind::Tariff:{JsonValue value;if(parseTariffDeclaration(text,&value,nullptr)&&tariffJson(value)+"\n"==text)return true;
            return allowEmptyTariffDraft&&text==tariffJson(tariffDeclarationTemplate())+"\n";}
        default:return false;
    }
}
} // namespace om
