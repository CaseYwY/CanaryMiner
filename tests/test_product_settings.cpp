// Settings/navigation/storage tests: synthetic data only, no GPU or network.
#include <fstream>
#include <string>

#include "../src/product/settings.h"
#include "product_test_util.h"

using namespace om;

static void decoderTests() {
    TerminalKeyDecoder d;
    d.feed(27, 0);
    CHECK(d.poll(20) == 0);
    d.feed('[', 21);
    d.feed('A', 22);
    CHECK(d.poll(22) == kKeyUp);
    CHECK(d.poll(200) == 0);
    d.feed(27, 300);
    CHECK(d.poll(399) == 0);
    CHECK(d.poll(400) == 27);
    uint64_t now = 500;
    for (const auto &pair : {std::make_pair("\x1b[D", kKeyLeft), {"\x1b[C", kKeyRight}, {"\x1b[Z", kKeyBackTab},
                             {"\x1bOQ", kKeyF2}, {"\x1b[12~", kKeyF2}, {"\x1b[5~", kKeyPageUp}, {"\x1b[6~", kKeyPageDown}}) {
        for (const char c : std::string(pair.first)) d.feed(static_cast<unsigned char>(c), ++now);
        CHECK(d.poll(++now) == pair.second);
    }
    for (const unsigned char c : std::string("\x1b[99~")) d.feed(c, ++now);
    CHECK(d.poll(++now) == 0);
    d.feed(27, ++now);
    d.feed('q', ++now);
    CHECK(d.poll(++now) == 0);
    d.feed('q', ++now);
    CHECK(d.poll(++now) == 'q');
}

static void panelTests() {
    SettingsPanel p;
    p.open(UiPreferences{});
    CHECK(!p.dirty());
    int saved = 0;
    auto ok = [&](const UiPreferences &, std::string *) { ++saved; return true; };
    p.key('\t', ok);
    p.key(' ', ok);
    CHECK(p.dirty() && p.active.refreshMs == 500 && p.draft.refreshMs == 1000);
    CHECK(saved == 0);
    CHECK(p.changes().size() == 1);
    p.key(kKeyF2, ok);
    CHECK(p.page == SettingsPage::Review);
    p.key(27, ok);
    CHECK(p.page == SettingsPage::Browse && saved == 0);
    p.key('v', ok);
    p.key('\r', [](const UiPreferences &, std::string *error) { *error = "synthetic write failure"; return false; });
    CHECK(p.active.refreshMs == 500 && p.dirty());
    CHECK(p.message == "synthetic write failure");
    p.key(kKeyF2, ok);
    CHECK(saved == 1 && !p.dirty() && p.active.refreshMs == 1000);
    p.key(kKeyDown, ok);
    p.key(' ', ok);
    CHECK(p.draft.color == false && p.active.color == true);
    p.key(27, ok);
    CHECK(p.page == SettingsPage::Leave);
    p.key(27, ok);
    CHECK(p.page == SettingsPage::Browse && p.dirty());
    p.key(27, ok);
    CHECK(p.key('d', ok) == SettingsResult::Close && !p.visible && !p.dirty());
    p.open(UiPreferences{});
    p.key(kKeyRight, ok);
    p.key('\t', ok);
    p.key(' ', ok);
    CHECK(!p.dirty());
    CHECK(p.message.find("Not implemented") != std::string::npos);
    CHECK(p.key('q', ok) == SettingsResult::Quit);
    CHECK(p.key(3, ok) == SettingsResult::Quit);
    p.open(UiPreferences{});
    p.group = 8;
    p.groupsFocused = false;
    p.key(' ', ok);
    CHECK(p.draft.hideWorker && !p.active.hideWorker);
    const std::string frame = p.render(100, 24, ColorLevel::None);
    CHECK(frame.find("Privacy") != std::string::npos && frame.find("Hide worker") != std::string::npos);
    for (int width : {20, 40, 80, 120}) {
        const auto text = p.render(width, 24, ColorLevel::None);
        CHECK(std::count(text.begin(), text.end(), '\n') < 24);
        size_t from = 0;
        for (size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', from)) {
            CHECK(end - from < static_cast<size_t>(width));
            from = end + 1;
        }
    }
    p.group = 0;
    p.field = 2;
    for (int width : {40, 80, 120}) {
        const auto text = p.render(width, 12, ColorLevel::None);
        CHECK(text.find("> Recent events") != std::string::npos);
        CHECK(std::count(text.begin(), text.end(), '\n') < 12);
    }
    DashModel model;
    model.worker = "synthetic-sensitive-label";
    model.events.push_back({"00:00:00", "synthetic event"});
    DashView view;
    view.columns = 120;
    view.hideWorker = true;
    view.showEvents = false;
    const auto dashboard = renderDashboard(model, view);
    CHECK(dashboard.find(model.worker) == std::string::npos);
    CHECK(dashboard.find("synthetic event") == std::string::npos);
    CHECK(dashboard.find("[S] Settings") != std::string::npos);
}

static void storeTests() {
    UiPreferences p;
    std::string error;
    CHECK(parseUiPreferences(uiPreferencesJson(p), &p, &error));
    const auto before = p;
    for (const std::string invalid : {"{}", "[]", "{\"schema_version\":3}", "{\"schema_version\":1,\"refresh_ms\":1}",
                                      "{\"schema_version\":1,\"hide_worker\":1}", "{\"schema_version\":1,\"unknown\":true}",
                                      "{\"schema_version\":1,\"schema_version\":1}"}) {
        CHECK(!parseUiPreferences(invalid, &p, &error));
        CHECK(p == before);
    }
    const auto dir = std::filesystem::path(makeScratchDir("ui-preferences"));
    const auto path = dir / "preferences.json";
    UiPreferencesStore store(path);
    CHECK(!store.save(p, &error));
    CHECK(store.load(&p, &error));
    CHECK(!std::filesystem::exists(path));
    p.hideWorker = true;
    CHECK(store.save(p, &error));
    UiPreferencesStore second(path);
    UiPreferences loaded;
    CHECK(second.load(&loaded, &error) && loaded == p);
    p.refreshMs = 250;
    CHECK(store.save(p, &error));
    CHECK(!second.save(loaded, &error));
    CHECK(error.find("externally") != std::string::npos);
    CHECK(second.load(&loaded, &error) && loaded == p);
    auto lockPath = path;
    lockPath += ".lock";
    CHECK(std::filesystem::create_directory(lockPath));
    CHECK(!second.save(loaded, &error));
    CHECK(std::filesystem::remove(lockPath));
    CHECK(second.save(loaded, &error));
    CHECK(second.save(loaded, &error));  // replacement must work on Windows too
    { std::ofstream file(path); file << std::string(16385, 'x'); }
    CHECK(!second.load(&loaded, &error));
    CHECK(std::filesystem::remove(path));
    { std::ofstream file(path); file << "{\"schema_version\":1,\"hide_worker\":true}"; }
    UiPreferencesStore legacy(path);
    CHECK(legacy.load(&loaded, &error) && loaded.hideWorker && loaded.detailsEnabled && loaded.logEnabled);
    auto backup = path;
    backup += ".v1.bak";
    { std::ofstream file(backup); file << "invalid synthetic rollback"; }
    CHECK(!legacy.save(loaded, &error));
    CHECK(legacy.load(&loaded, &error) && loaded.hideWorker);
    CHECK(std::filesystem::remove(backup));
    CHECK(legacy.save(loaded, &error));
    CHECK(std::filesystem::exists(backup));
    CHECK(std::filesystem::remove(backup));
    CHECK(std::filesystem::remove(path));
    CHECK(std::filesystem::remove(dir));
}

int main() {
    decoderTests();
    panelTests();
    storeTests();
    return finishTests("product_settings");
}
