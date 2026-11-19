// Runtime defaults and contextual native editing. Synthetic files/clocks only; no mining/network.
#include <fstream>
#include <iterator>
#include "../src/product/settings.h"
#include "product_test_util.h"
#include "product/activation_messages.h"

using namespace om;

static void panelTests() {
    RuntimeSettingsPanel panel;
    RuntimeContext context;
    context.running = true;
    context.cliWorkWait = 0;  // presence matters: explicit zero overrides a stored positive wait
    context.cliNoHistory = true;
    context.effective.historyEnabled = false;
    panel.open({}, context);
    unsigned saves = 0;
    auto save = [&](const RuntimePreferences &, std::string *) { ++saves; return true; };
    panel.key('\r', save);
    CHECK(panel.page == RuntimeSettingsPage::EditWait);
    CHECK(panel.key('q', save) == RuntimeSettingsResult::None && panel.visible && panel.input == "q");
    panel.key('\r', save);
    CHECK(panel.page == RuntimeSettingsPage::EditWait && !panel.message.empty() && !panel.dirty());
    panel.key(27, save);
    panel.key('\r', save);
    for (char ch : std::string("345")) panel.key(ch, save);
    panel.key('\r', save);
    CHECK(panel.draft.workWaitMs == 345 && panel.active.workWaitMs == 0 && panel.dirty());
    panel.key('\t', save);
    panel.key(' ', save);
    CHECK(!panel.draft.historyEnabled && panel.active.historyEnabled);
    const auto configured = panel.render(120, 30, ColorLevel::None);
    CHECK(configured.find("CLI override") != std::string::npos);
    CHECK(configured.find("Next start with same flags: wait 0 ms; history off") != std::string::npos);
    CHECK(configured.find("Current host wait: 0 ms") != std::string::npos);
    panel.key('v', save);
    CHECK(panel.page == RuntimeSettingsPage::Review && saves == 0);
    const auto review = panel.render(120, 30, ColorLevel::None);
    CHECK(review.find("Host wait: 0 -> 345 ms") != std::string::npos && review.find("No deletion") != std::string::npos);
    panel.key('\r', [](const RuntimePreferences &, std::string *error) { *error = "synthetic failure"; return false; });
    CHECK(panel.dirty() && panel.message == "synthetic failure" && saves == 0);
    panel.key('\r', save);
    CHECK(!panel.dirty() && saves == 1 && panel.active.workWaitMs == 345);
    CHECK(panel.context.effective.workWaitMs == 0 && !panel.context.effective.historyEnabled);
    panel.key(' ', save);
    panel.key(27, save);
    CHECK(panel.page == RuntimeSettingsPage::Leave);
    panel.key(27, save);
    CHECK(panel.page == RuntimeSettingsPage::Browse && panel.dirty());
    panel.key(27, save);
    CHECK(panel.key('d', save) == RuntimeSettingsResult::Close && !panel.dirty() && !panel.visible);
    panel.open({}, {});
    panel.key('\r', save);
    CHECK(panel.key(3, save) == RuntimeSettingsResult::Quit);
    panel.open({}, {});
    panel.field = 1;
    for (int width : {20, 40, 80, 120}) for (int rows : {12, 16, 24}) {
        const auto text = panel.render(width, rows, ColorLevel::None);
        CHECK(text.find(width < 50 || rows < 20 ? "> History:" : "> Configured history") != std::string::npos);
        CHECK(std::count(text.begin(), text.end(), '\n') < rows);
        size_t begin = 0;
        for (size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', begin)) {
            CHECK(end - begin < static_cast<size_t>(width));
            begin = end + 1;
        }
    }
    CHECK(std::string(activationName(ChangeActivation::SeparatePermission)).find("permission") != std::string::npos);
    const std::array<ChangeActivation,5> phases={{ChangeActivation::Presentation,ChangeActivation::WorkBoundary,ChangeActivation::NextStart,ChangeActivation::SeparatePermission,static_cast<ChangeActivation>(999)}};
    const std::array<std::string,5> descriptions={{"live presentation","safe work boundary","next explicit start; no automatic restart","separate permission required","unsupported"}};
    std::array<const char *,5> pointers{};const auto original=runtimePreferencesJson(panel.active);
    for(std::size_t i=0;i<phases.size();++i){pointers[i]=activationName(phases[i]);CHECK(std::string(pointers[i])==descriptions[i]);}
    for(const auto &pack:i18n::activationLocalePacks())(void)i18n::activationMessage(pack.id,i18n::ActivationMessageKey::WorkBoundary);
    for(std::size_t i=0;i<phases.size();++i)CHECK(std::string(pointers[i])==descriptions[i]);
    CHECK(std::string(activationName(static_cast<ChangeActivation>(-1)))=="unsupported");
    CHECK(runtimePreferencesJson(panel.active)==original);
    panel.field = 2;
    CHECK(panel.render(20,12,ColorLevel::None).find("> Economy req [off]") != std::string::npos);
    panel.key(' ', save);
    CHECK(panel.draft.economicModeRequested && !panel.active.economicModeRequested);
    const auto compactBrowse = panel.render(20,12,ColorLevel::None);
    CHECK(compactBrowse.find("> Economy req [on]") != std::string::npos &&
          compactBrowse.find("Economy inactive") != std::string::npos);
    panel.key('v', save);
    const auto compactReview = panel.render(20,12,ColorLevel::None);
    CHECK(compactReview.find("Econ req off -> on") != std::string::npos &&
          compactReview.find("Economy inactive") != std::string::npos);
    const auto request = panel.render(100,24,ColorLevel::None);
    CHECK(request.find("Economic mode request: off -> on") != std::string::npos);
    CHECK(request.find("Economic mode is inactive") != std::string::npos);
    CHECK(request.find("next explicit start") == std::string::npos);
    CHECK(request.find("selected GPUs") == std::string::npos);
    panel.key('\r', save);
    CHECK(panel.active.economicModeRequested && !panel.context.effective.economicModeRequested);
    CHECK(panel.message == "Request saved. Economic mode is inactive.");
}

static void storeTests(const std::filesystem::path &directory) {
    RuntimePreferences value;
    std::string error;
    CHECK(parseRuntimePreferences(runtimePreferencesJson(value), &value, &error));
    CHECK(parseRuntimePreferences("{\"schema_version\":1}", &value, &error) && value.workWaitMs == 0 && value.historyEnabled &&
          !value.economicModeRequested && !value.publicTickersRequested);
    for (const auto &text : {"{}", "[]", "{\"schema_version\":2}", "{\"schema_version\":1,\"work_wait_ms\":1001}",
            "{\"schema_version\":1,\"work_wait_ms\":-1}", "{\"schema_version\":1,\"work_wait_ms\":1.5}",
            "{\"schema_version\":1,\"history_enabled\":1}", "{\"schema_version\":1,\"schema_version\":1}",
            "{\"schema_version\":1,\"economic_mode_requested\":1}",
            "{\"schema_version\":1,\"public_tickers_requested\":\"on\"}",
            "{\"schema_version\":1,\"public_tickers_requested\":true,\"public_tickers_requested\":false}",
            "{\"schema_version\":1,\"unknown\":true}"}) {
        const auto prior = value;
        CHECK(!parseRuntimePreferences(text, &value, &error));
        CHECK(value == prior);
    }
    CHECK(!parseRuntimePreferences("{\"synthetic-confidential-key\":1,\"synthetic-confidential-key\":2}", &value, &error));
    CHECK(error.find("synthetic-confidential-key") == std::string::npos);
    RuntimeContext overrides;
    overrides.cliWorkWait = 0;
    overrides.cliNoHistory = true;
    value.workWaitMs = 500;
    auto effective = effectiveRuntimePreferences(value, overrides);
    CHECK(effective.workWaitMs == 0 && !effective.historyEnabled && value.workWaitMs == 500 && value.historyEnabled);
    const auto path = directory / std::filesystem::u8path(u8"合成-runtime.json");
    RuntimePreferencesStore store(path);
    CHECK(!store.save(value, &error));
    CHECK(store.load(&value, &error) && !std::filesystem::exists(path));
    CHECK(!value.economicModeRequested);
    value.workWaitMs = 345;
    value.historyEnabled = false;
    value.economicModeRequested = true;
    value.publicTickersRequested = true;
    CHECK(store.save(value, &error));
    RuntimePreferencesStore second(path);
    RuntimePreferences loaded;
    CHECK(second.load(&loaded, &error) && loaded == value);
    CHECK(loaded.economicModeRequested && loaded.publicTickersRequested);
    value.workWaitMs = 50;
    value.economicModeRequested = false;
    value.publicTickersRequested = false;
    CHECK(store.save(value, &error));
    CHECK(!second.save(loaded, &error) && error.find("externally") != std::string::npos);
    CHECK(second.load(&loaded, &error) && loaded == value);
    CHECK(runtimePreferencesJson(loaded).find("economic_mode_requested") == std::string::npos);
    CHECK(runtimePreferencesJson(loaded).find("public_tickers_requested") == std::string::npos);
    auto lockPath = path; lockPath += ".lock";
    {
        RuntimePreferencesLease lease;
        CHECK(lease.acquire(lockPath));
        CHECK(!second.save(loaded, &error));
    }
    CHECK(second.save(loaded, &error));  // OS lease release, no stale lock stealing/removal
    { std::ofstream file(path); file << std::string(4097, 'x'); }
    CHECK(!second.load(&loaded, &error));
    CHECK(loaded == value);
    { std::ofstream file(path); file << "{\"schema_version\":99}"; }
    CHECK(!second.load(&loaded, &error));
    CHECK(!second.save(value, &error));
    std::ifstream file(path);
    CHECK(std::string(std::istreambuf_iterator<char>(file), {}) == "{\"schema_version\":99}");
}

static void publicTickerTests() {
    SettingsPanel root;
    root.open({});
    root.group=3;
    root.groupsFocused=false;
    root.field=2;
    CHECK(root.fields()==3 && root.key('\r',{})==SettingsResult::OpenRuntime);
    RuntimeContext context;
    context.running=true;
    context.cliNoMarket=true;
    RuntimePreferences original;
    original.workWaitMs=123;
    original.historyEnabled=false;
    root.runtime.open(original,context,RuntimeSettingsSection::PublicTickers);
    const auto save=[&](const RuntimePreferences &value,std::string *) {
        CHECK(value.publicTickersRequested && !value.economicModeRequested);
        CHECK(value.workWaitMs==123 && !value.historyEnabled);
        return true;
    };
    CHECK(root.runtime.render(60,12,ColorLevel::None).find("> Public tickers [off]")!=std::string::npos);
    const auto narrowBrowse=root.runtime.render(20,12,ColorLevel::None);
    CHECK(narrowBrowse.find("> Tickers [off]")!=std::string::npos);
    CHECK(narrowBrowse.find("Session req: off")!=std::string::npos && narrowBrowse.find("CLI: --no-market")!=std::string::npos);
    CHECK(narrowBrowse.find("Polling unavailable")!=std::string::npos && narrowBrowse.find("Next explicit run")!=std::string::npos);
    CHECK(narrowBrowse.find("Latest refs only")!=std::string::npos && narrowBrowse.find("Both on: withheld")!=std::string::npos);
    root.key(' ',{},save);
    root.key('v',{},save);
    const auto review=root.runtime.render(80,16,ColorLevel::None);
    CHECK(review.find("Public tickers: off -> on")!=std::string::npos);
    CHECK(review.find("--no-market")!=std::string::npos);
    CHECK(review.find("Polling unavailable")!=std::string::npos);
    const auto narrowReview=root.runtime.render(20,12,ColorLevel::None);
    CHECK(narrowReview.find("Ticker: off -> on")!=std::string::npos && narrowReview.find("Polling unavailable")!=std::string::npos);
    CHECK(narrowReview.find("Gate PRL/USDT")!=std::string::npos && narrowReview.find("Kraken USDT/USD")!=std::string::npos);
    CHECK(narrowReview.find("Both on: withheld")!=std::string::npos);
    CHECK(narrowReview.find("Arrows Enter Esc Q")!=std::string::npos);
    CHECK(std::count(narrowReview.begin(),narrowReview.end(),'\n')<12);
    root.key('\r',{},save);
    CHECK(root.runtime.active.publicTickersRequested && !root.runtime.context.effective.publicTickersRequested);
    CHECK(root.runtime.message=="Request saved.");
    root.key(' ',{},save);
    root.key(27,{},save);
    root.key('d',{},save);
    CHECK(!root.runtime.visible && root.runtime.active.publicTickersRequested && !root.runtime.dirty());
    const auto english=i18n::marketTickerSettingsMessage("en",i18n::MarketTickerSettingsKey::Entry);
    const auto fallback=i18n::marketTickerSettingsMessage("zh-Hans",i18n::MarketTickerSettingsKey::Entry);
    CHECK(english.review==i18n::ReviewStatus::Source && !english.fallback && fallback.fallback && english.text==fallback.text);
}

static void independentTargets(const std::filesystem::path &directory) {
    SettingsPanel panel;
    panel.open({});
    panel.draft.refreshMs = 1000;
    panel.group = 1;
    panel.groupsFocused = false;
    unsigned uiSaves = 0;
    auto uiSave = [&](const UiPreferences &, std::string *) { ++uiSaves; return true; };
    CHECK(panel.key('\r', uiSave) == SettingsResult::OpenRuntime);
    RuntimePreferencesStore store(directory / "separate-runtime.json");
    openRuntimeControls(&panel, &store, {});
    CHECK(panel.runtime.visible && panel.dirty());
    panel.key('\t', uiSave);
    panel.key(' ', uiSave);
    panel.key('v', uiSave);
    panel.key('\r', uiSave, [&](const RuntimePreferences &p, std::string *error) { return store.save(p, error); });
    CHECK(!panel.runtime.active.historyEnabled && panel.active.refreshMs == 500 && panel.draft.refreshMs == 1000 && uiSaves == 0);
    panel.key(27, uiSave);
    CHECK(!panel.runtime.visible && panel.visible && panel.dirty());
}

int main() {
    const auto directory = std::filesystem::path(makeScratchDir("runtime-preferences"));
    panelTests();
    publicTickerTests();
    storeTests(directory);
    independentTargets(directory);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);  // only this test's newly created synthetic directory
    return finishTests("product_runtime_settings");
}
