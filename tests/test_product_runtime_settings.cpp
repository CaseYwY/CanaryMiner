// Runtime defaults and contextual native editing. Synthetic files/clocks only; no mining/network.
#include <fstream>
#include <iterator>
#include "../src/product/settings.h"
#include "product_test_util.h"

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
}

static void storeTests(const std::filesystem::path &directory) {
    RuntimePreferences value;
    std::string error;
    CHECK(parseRuntimePreferences(runtimePreferencesJson(value), &value, &error));
    CHECK(parseRuntimePreferences("{\"schema_version\":1}", &value, &error) && value.workWaitMs == 0 && value.historyEnabled);
    for (const auto &text : {"{}", "[]", "{\"schema_version\":2}", "{\"schema_version\":1,\"work_wait_ms\":1001}",
            "{\"schema_version\":1,\"work_wait_ms\":-1}", "{\"schema_version\":1,\"work_wait_ms\":1.5}",
            "{\"schema_version\":1,\"history_enabled\":1}", "{\"schema_version\":1,\"schema_version\":1}",
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
    value.workWaitMs = 345;
    value.historyEnabled = false;
    CHECK(store.save(value, &error));
    RuntimePreferencesStore second(path);
    RuntimePreferences loaded;
    CHECK(second.load(&loaded, &error) && loaded == value);
    value.workWaitMs = 50;
    CHECK(store.save(value, &error));
    CHECK(!second.save(loaded, &error) && error.find("externally") != std::string::npos);
    CHECK(second.load(&loaded, &error) && loaded == value);
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
    storeTests(directory);
    independentTargets(directory);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);  // only this test's newly created synthetic directory
    return finishTests("product_runtime_settings");
}
