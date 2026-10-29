// Keyboard settings state machine + pure renderer. No network, tuning, mining or privileged actions.
#pragma once

#include <functional>
#include <signal.h>
#include <string>
#include <vector>

#include "../core/telemetry.h"
#include "dashboard.h"
#include "ui_preferences.h"
#include "runtime_settings.h"
#include "manual_market_settings.h"
#include "electricity_settings.h"
#include "tariff_settings.h"
#include "credit_review.h"
#include "energy_review.h"

namespace om {

enum class SettingsPage { Browse, Review, Leave };
enum class SettingsResult { None, Close, Quit, OpenRuntime, OpenManualMarket, OpenElectricity, OpenTariff };

class SettingsPanel {
   public:
    using Save = std::function<bool(const UiPreferences &, std::string *)>;
    bool visible = false;
    bool groupsFocused = true;
    int group = 0, field = 0;
    int reviewOffset = 0;
    SettingsPage page = SettingsPage::Browse;
    UiPreferences active, draft;
    std::string message;
    RuntimeSettingsPanel runtime;
    ManualMarketSettingsPanel manual;
    ElectricitySettingsPanel electricity;
    TariffSettingsPanel tariff;
    CreditReviewPanel credit;
    CreditReviewAccess creditAccess; // default empty; this does not construct or activate a credit owner
    EnergyReviewPanel energyReview;
    EnergyReviewAccess energyAccess; // independent, default empty; opening the page is not persistence authorization

    void open(const UiPreferences &value) {
        active = draft = value;
        visible = true;
        groupsFocused = true;
        page = SettingsPage::Browse;
        reviewOffset = 0;
        message.clear();
        runtime.visible = false;
        manual.visible = false;
        electricity.visible = false;
        tariff.visible = false;
        credit.visible = false;
        energyReview.visible = false;
    }
    bool dirty() const { return active != draft; }

    static const std::vector<std::string> &groups() {
        static const std::vector<std::string> names = {"Display", "Automatic mining", "Electricity", "Market and timing", "Profiles and tests",
                                                      "Games", "Pools and TLS", "Language", "Privacy", "Details", "Log"};
        return names;
    }
    int fields() const { return group == 8 ? 4 : group == 0 || group == 2 ? 3 : (group == 1 || group == 3) ? 1 : group == 9 ? 5 : group == 10 ? 7 : 0; }
    void updateCreditReview(){if(credit.visible&&creditAccess.snapshot)credit.update(creditAccess.snapshot());}
    void updateEnergyReview(){if(energyReview.visible&&energyAccess.snapshot)energyReview.update(energyAccess.snapshot());}

    SettingsResult key(int key, const Save &save, const RuntimeSettingsPanel::Save &runtimeSave = {}, const ManualMarketSettingsPanel::Save &manualSave = {}, uint64_t nowUtc = 0, const ElectricitySettingsPanel::Save &electricitySave = {}, const TariffSettingsPanel::Save &tariffSave = {}) {
        if(energyReview.visible){updateEnergyReview();return energyReview.key(key,energyAccess)==EnergyReviewResult::Quit?SettingsResult::Quit:SettingsResult::None;}
        if(tariff.visible)return tariff.key(key,tariffSave,nowUtc)==TariffSettingsResult::Quit?SettingsResult::Quit:SettingsResult::None;
        if(credit.visible){updateCreditReview();return credit.key(key,creditAccess)==CreditReviewResult::Quit?SettingsResult::Quit:SettingsResult::None;}
        if (electricity.visible) return electricity.key(key,electricitySave,nowUtc) == ElectricitySettingsResult::Quit ? SettingsResult::Quit : SettingsResult::None;
        if (manual.visible) return manual.key(key, manualSave, nowUtc) == ManualMarketSettingsResult::Quit ? SettingsResult::Quit : SettingsResult::None;
        if (runtime.visible) return runtime.key(key, runtimeSave) == RuntimeSettingsResult::Quit ? SettingsResult::Quit : SettingsResult::None;
        if (key == 3 || key == 'q' || key == 'Q') return SettingsResult::Quit;
        if (page == SettingsPage::Leave) {
            if (key == 'd' || key == 'D') { draft = active; visible = false; return SettingsResult::Close; }
            if (key == 27 || key == 'c' || key == 'C') { page = SettingsPage::Browse; return SettingsResult::None; }
            if (key == '\r' || key == '\n' || key == kKeyF2) page = SettingsPage::Review;
            return SettingsResult::None;
        }
        if (page == SettingsPage::Review) {
            if (key == 27) { page = SettingsPage::Browse; message.clear(); }
            else if (key == kKeyUp || key == kKeyPageUp) reviewOffset = std::max(0, reviewOffset - (key == kKeyUp ? 1 : 5));
            else if (key == kKeyDown || key == kKeyPageDown)
                reviewOffset = std::min(std::max(0, static_cast<int>(changes().size()) - 1), reviewOffset + (key == kKeyDown ? 1 : 5));
            else if (key == '\r' || key == '\n' || key == kKeyF2) {
                std::string error;
                if (save && save(draft, &error)) {
                    active = draft;
                    message = "Saved. Presentation changes are live; mining settings were not changed.";
                    page = SettingsPage::Browse;
                } else message = error.empty() ? "Save failed; active settings are unchanged." : error;
            }
            return SettingsResult::None;
        }
        if (key == 27) {
            if (dirty()) page = SettingsPage::Leave;
            else { visible = false; return SettingsResult::Close; }
        } else if (key == kKeyF2 || key == 'v' || key == 'V') {
            page = SettingsPage::Review;
            reviewOffset = 0;
        } else if (key == '\t' || key == kKeyBackTab) {
            groupsFocused = !groupsFocused;
        } else if (key == kKeyLeft || key == kKeyRight) {
            group = (group + (key == kKeyRight ? 1 : static_cast<int>(groups().size()) - 1)) % static_cast<int>(groups().size());
            field = 0;
        } else if (key == kKeyUp || key == kKeyDown) {
            const int count = groupsFocused ? static_cast<int>(groups().size()) : fields();
            int &index = groupsFocused ? group : field;
            if (count) index = (index + (key == kKeyDown ? 1 : count - 1)) % count;
            if (groupsFocused) field = 0;
        } else if (key == '\r' || key == '\n' || key == ' ') {
            if (groupsFocused) groupsFocused = false;
            else if (group == 1 || (group == 8 && field == 1)) return SettingsResult::OpenRuntime;
            else if (group == 3) return SettingsResult::OpenManualMarket;
            else if ((group==2&&field==2)||(group==8&&field==3)) energyReview.open(energyAccess.snapshot?energyAccess.snapshot():nullptr);
            else if (group == 2) return field==0?SettingsResult::OpenElectricity:SettingsResult::OpenTariff;
            else if (group == 8 && field == 2) credit.open(creditAccess.snapshot?creditAccess.snapshot():nullptr);
            else change();
        }
        return SettingsResult::None;
    }

    std::vector<std::string> changes() const {
        std::vector<std::string> result;
        if (active.refreshMs != draft.refreshMs) result.push_back("Display refresh: " + std::to_string(active.refreshMs) + " -> " + std::to_string(draft.refreshMs) + " ms");
        if (active.color != draft.color) result.push_back("Terminal color: " + on(active.color) + " -> " + on(draft.color));
        if (active.showEvents != draft.showEvents) result.push_back("Recent events: " + on(active.showEvents) + " -> " + on(draft.showEvents));
        if (active.hideWorker != draft.hideWorker) result.push_back("Hide worker label: " + on(active.hideWorker) + " -> " + on(draft.hideWorker));
        const std::pair<const char *, std::pair<bool, bool>> switches[] = {
            {"Details page", {active.detailsEnabled, draft.detailsEnabled}}, {"Log page", {active.logEnabled, draft.logEnabled}},
            {"Details identity", {active.detailsIdentity, draft.detailsIdentity}}, {"Details pool", {active.detailsPool, draft.detailsPool}},
            {"Details restarts", {active.detailsRestarts, draft.detailsRestarts}}, {"Details errors", {active.detailsErrors, draft.detailsErrors}},
            {"Unclassified logs", {active.logUnclassified, draft.logUnclassified}}, {"Log timestamps", {active.logTimestamps, draft.logTimestamps}},
            {"ISO UTC time", {active.logIsoTime, draft.logIsoTime}}, {"Log follow", {active.logFollow, draft.logFollow}}};
        for (const auto &item : switches) if (item.second.first != item.second.second)
            result.push_back(std::string(item.first) + ": " + on(item.second.first) + " -> " + on(item.second.second));
        if (active.logSeverity != draft.logSeverity) result.push_back("Log level: " + level(active.logSeverity) + " -> " + level(draft.logSeverity));
        if (active.logRows != draft.logRows) result.push_back("Log records: " + std::to_string(active.logRows) + " -> " + std::to_string(draft.logRows));
        return result;
    }

    std::string render(int columns, int rows, ColorLevel color) const {
        if(energyReview.visible)return energyReview.render(columns,rows,color);
        if(tariff.visible)return tariff.render(columns,rows,color);
        if(credit.visible)return credit.render(columns,rows,color);
        if (electricity.visible) return electricity.render(columns,rows,color);
        if (manual.visible) return manual.render(columns, rows, color);
        if (runtime.visible) return runtime.render(columns, rows, color);
        using namespace dash_detail;
        const size_t width = static_cast<size_t>(std::max(20, columns) - 1);
        const bool compact = rows > 0 && rows < 20;
        if (rows > 0 && rows < 12) return "Terminal too short.\nResize to 12+ rows.\n[Q] Stop [Esc] Back\n";
        const std::string footer = page == SettingsPage::Review ? "[Enter/F2] Save  [Esc] Edit  [Q] Stop" :
                                   "[Arrows] Move [Tab] Pane [Space/Enter] Change [V/F2] Review [Esc] Back [Q] Stop";
        const auto footerRows = wrapText(footer, width);
        const size_t budget = rows > 0 ? static_cast<size_t>(std::max(1, rows - static_cast<int>(footerRows.size()) - 2)) : 1000;
        std::vector<Line> body;
        body.push_back(one("(o> CanaryMiner / Settings" + std::string(dirty() ? " * unsaved" : ""), Tone::Canary));
        if (!compact) {
            body.push_back(one("UI, runtime and offline references save separately; no automatic miner restart.", Tone::Plain));
            body.push_back(one(""));
        }
        if (page == SettingsPage::Review) {
            const auto diff = changes();
            body.push_back(one("Review " + std::to_string(diff.size()) + " changes / arrows scroll", Tone::Canary));
            if (diff.empty()) body.push_back(one("No changes."));
            const size_t start = diff.empty() ? 0 : std::min(static_cast<size_t>(reviewOffset), diff.size() - 1);
            for (size_t i = start; i < diff.size(); ++i) body.push_back(one(diff[i]));
            body.push_back(one("Mining config, donation and hardware settings are untouched."));
        } else if (page == SettingsPage::Leave) {
            body.push_back(one("Unsaved changes", Tone::Canary));
            body.push_back(one("[Enter] Review/save   [D] Discard   [Esc] Continue editing"));
        } else {
            const auto content = fieldsText();
            if (width >= 80 && !compact) {
                const size_t count = std::max(groups().size(), content.size());
                for (size_t i = 0; i < count; ++i) {
                    const bool selected = static_cast<int>(i) == group;
                    Line line;
                    const std::string label = i < groups().size() ? std::string(selected ? "> " : "  ") + groups()[i] : "";
                    line.push_back({padRight(label, 24), selected ? Tone::Canary : Tone::Plain});
                    if (i < content.size()) line.push_back(content[i]);
                    body.push_back(line);
                }
            } else {
                body.push_back(one("< " + groups()[group] + " >  (Left/Right: group)", Tone::Canary));
                const size_t available = budget > body.size() + 1 ? budget - body.size() - 1 : 1;
                const size_t start = !groupsFocused && static_cast<size_t>(field) >= available ? static_cast<size_t>(field) - available + 1 : 0;
                for (size_t i = start; i < content.size() && i < start + available; ++i) body.push_back(Line{content[i]});
            }
            body.push_back(one(groupsFocused ? "Focus: groups / Tab or Enter to edit" : "Focus: fields / Tab to choose a group", Tone::Plain));
        }
        if (body.size() > budget) body.resize(budget);
        std::string output;
        for (const auto &line : body) output += paintLine(fitLine(line, width), color) + "\n";
        const std::string note = message.empty() ? "No private config values are shown here." : message;
        output += paint(clip(note, width), Tone::Plain, color) + "\n";
        for (const auto &line : footerRows) output += paint(line, Tone::Plain, color) + "\n";
        return output;
    }

   private:
    static std::string on(bool value) { return value ? "on" : "off"; }
    static std::string level(unsigned value) { return value == 1 ? "warnings/errors" : value == 2 ? "errors" : "all"; }
    void change() {
        message.clear();
        if (group == 8) draft.hideWorker = !draft.hideWorker;
        else if (group == 0) {
            if (field == 0) {
                const unsigned choices[] = {100, 250, 500, 1000};
                for (size_t i = 0; i < 4; ++i) if (draft.refreshMs == choices[i]) { draft.refreshMs = choices[(i + 1) % 4]; break; }
            } else if (field == 1) draft.color = !draft.color;
            else draft.showEvents = !draft.showEvents;
        } else if (group == 9) {
            bool *values[] = {&draft.detailsEnabled, &draft.detailsIdentity, &draft.detailsPool, &draft.detailsRestarts, &draft.detailsErrors};
            *values[field] = !*values[field];
        } else if (group == 10) {
            if (field == 0) draft.logEnabled = !draft.logEnabled;
            else if (field == 1) draft.logSeverity = (draft.logSeverity + 1) % 3;
            else if (field == 2) draft.logUnclassified = !draft.logUnclassified;
            else if (field == 3) {
                const unsigned choices[] = {25, 50, 100, 200};
                for (size_t i = 0; i < 4; ++i) if (draft.logRows == choices[i]) { draft.logRows = choices[(i + 1) % 4]; break; }
            } else if (field == 4) draft.logTimestamps = !draft.logTimestamps;
            else if (field == 5) draft.logIsoTime = !draft.logIsoTime;
            else if (field == 6) draft.logFollow = !draft.logFollow;
            else message = "Raw lines remain unclassified; filtering does not stop pipe draining.";
        } else message = "Not implemented yet; no switch or mining behaviour was changed.";
    }
    std::vector<dash_detail::Seg> fieldsText() const {
        using namespace dash_detail;
        std::vector<Seg> result;
        auto row = [&](int index, const std::string &text) {
            result.push_back({std::string(!groupsFocused && field == index ? "> " : "  ") + text,
                              !groupsFocused && field == index ? Tone::Canary : Tone::Plain});
        };
        auto toggle = [&](int index, const char *name, bool value) { row(index, std::string(name) + (value ? " [x]" : " [ ]")); };
        if (group == 0) {
            row(0, "Display refresh    < " + std::to_string(draft.refreshMs) + " ms >");
            row(1, std::string("Terminal color     [") + (draft.color ? "x" : " ") + "]");
            row(2, std::string("Recent events      [") + (draft.showEvents ? "x" : " ") + "]");
            result.push_back({"", Tone::Plain});
            result.push_back({"Display refresh does not change sampling", Tone::Plain});
            result.push_back({"or mining intensity. NO_COLOR still wins.", Tone::Plain});
        } else if (group == 8) {
            row(0, std::string("Hide worker label  [") + (draft.hideWorker ? "x" : " ") + "]");
            row(1, "History collection / runtime defaults [Enter]");
            row(2, "Credit recovery / collection status [Enter]");
            row(3, "Energy history / recovery status [Enter]");
            result.push_back({"Presentation only, not a diagnostic scrubber.", Tone::Plain});
            result.push_back({"Logs, API and files are not anonymized by it.", Tone::Plain});
        } else if (group == 1) {
            row(0, "Host wait / runtime defaults [Enter]");
            result.push_back({"Manual controls; automatic selection is not enabled.", Tone::Plain});
            result.push_back({"Saved defaults apply on the next explicit start only.", Tone::Plain});
        } else if (group == 3) {
            row(0, "Manual/offline price references [Enter]");
            result.push_back({"Default off; user-declared PRL/USDT and independent FX.", Tone::Plain});
            result.push_back({"No provider polling, 1h series, actual income or optimizer.", Tone::Plain});
        } else if (group == 2) {
            row(0,"Session board energy / declared flat energy-only rate [Enter]");
            row(1,"Combined tariff / declared whole-meter opening [Enter]");
            row(2,"Energy history / durable capture recovery [Enter]");
            result.push_back({"Default off; next explicit session only, no miner restart.",Tone::Plain});
            result.push_back({"Not whole-PC/incremental energy, shared utility bill or net income.",Tone::Plain});
        } else if (group == 9) {
            toggle(0, "Details page", draft.detailsEnabled);
            toggle(1, "Device identity", draft.detailsIdentity);
            toggle(2, "Pool state", draft.detailsPool);
            toggle(3, "Restart count", draft.detailsRestarts);
            toggle(4, "Last error", draft.detailsErrors);
            result.push_back({"Main safety/status remain visible.", Tone::Plain});
        } else if (group == 10) {
            toggle(0, "Log page", draft.logEnabled);
            row(1, "Severity < " + level(draft.logSeverity) + " >");
            toggle(2, "Include unclassified", draft.logUnclassified);
            row(3, "Records shown < " + std::to_string(draft.logRows) + " >");
            toggle(4, "Show observed time", draft.logTimestamps);
            row(5, std::string("Time format < ") + (draft.logIsoTime ? "ISO UTC" : "UTC HH:MM:SS") + " >");
            toggle(6, "Follow new output", draft.logFollow);
            result.push_back({"Raw severity: unclassified", Tone::Plain});
            result.push_back({"Off freezes this view, not data collection.", Tone::Plain});
        } else {
            result.push_back({"Not implemented in this development slice.", Tone::Plain});
            result.push_back({"No actionable switch until its backend", Tone::Plain});
            result.push_back({"and safety checks are implemented.", Tone::Plain});
            result.push_back({"See docs/feature-roadmap.md.", Tone::Plain});
        }
        return result;
    }
};

inline void printSettingsFrame(const std::string &text) {
    std::string out;
    for (char ch : text) {
        if (ch == '\n') out += "\x1b[K";
        out += ch;
    }
    printf("\x1b[H%s\x1b[K\x1b[J", out.c_str());
    fflush(stdout);
}

inline void openRuntimeControls(SettingsPanel *panel, RuntimePreferencesStore *store, const RuntimeContext &context) {
    if (!store) { panel->message = "Runtime controls unavailable: no local defaults target configured."; return; }
    RuntimePreferences configured;
    std::string error;
    if (!store->load(&configured, &error)) { panel->message = "Runtime controls unavailable: " + error; return; }
    panel->runtime.open(configured, context);
    if (panel->group == 8) panel->runtime.field = 1;
}

inline void openManualMarketControls(SettingsPanel *panel, ManualMarketStore *store, const ManualMarketContext &context) {
    if (!store) { panel->message = "Offline-market settings unavailable: no local target configured."; return; }
    ManualMarketPreferences configured; std::string error;
    if (!store->load(&configured,&error)) { panel->message = "Offline-market settings unavailable: "+error; return; }
    panel->manual.open(configured,context);
}
inline void openElectricityControls(SettingsPanel *panel,ElectricityPreferencesStore *store,const ElectricitySettingsContext &context) {
    if(!store){panel->message="Electricity settings unavailable: no local target.";return;}
    ElectricityPreferences p;std::string error;
    if(!store->load(&p,&error)){panel->message="Electricity settings unavailable: "+error;return;}
    panel->electricity.open(p,context);
}
inline void openTariffControls(SettingsPanel *panel,TariffDeclarationStore *store){
    if(!store){panel->message="Combined tariff settings unavailable: no local target.";return;}
    JsonValue value;std::string error;
    if(!store->load(&value,&error)){panel->message="Combined tariff unavailable: "+error;return;}
    panel->tariff.open(std::move(value));
}

inline int runSettingsCommand(const std::string &stateDir, volatile sig_atomic_t *stop, bool noMarket = false, bool noEnergy = false, CreditReviewAccess creditAccess = {}, EnergyReviewAccess energyAccess = {}) {
#if defined(_WIN32)
    if (!_isatty(_fileno(stdout)) || !_isatty(_fileno(stdin))) {
#else
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
#endif
        fprintf(stderr, "settings requires an interactive terminal; no mining was started\n");
        return 2;
    }
    UiPreferencesStore store(std::filesystem::u8path(stateDir) / "ui-preferences.json");
    RuntimePreferencesStore runtimeStore(std::filesystem::u8path(stateDir) / "runtime-preferences.json");
    ManualMarketStore manualStore(std::filesystem::u8path(stateDir) / "manual-market-preferences.json");
    ElectricityPreferencesStore electricityStore(std::filesystem::u8path(stateDir) / "electricity-preferences.json");
    TariffDeclarationStore tariffStore(std::filesystem::u8path(stateDir) / "tariff-declaration.json");
    UiPreferences preferences;
    std::string error;
    if (!store.load(&preferences, &error)) { fprintf(stderr, "%s\n", error.c_str()); return 3; }
    enableAnsiOnWindows();
    KeyReader keys;
    SettingsPanel panel;
    panel.open(preferences);
    panel.creditAccess=std::move(creditAccess);
    panel.energyAccess=std::move(energyAccess);
    printf("\x1b[2J\x1b[?25l");
    while (panel.visible && !(stop && *stop)) {
        for (int n = 0; n < 32; ++n) {
            const int key = keys.poll();
            if (!key) break;
            const auto result = panel.key(key, [&](const UiPreferences &p, std::string *e) { return store.save(p, e); },
                [&](const RuntimePreferences &p, std::string *e) { return runtimeStore.save(p, e); },
                [&](ManualMarketPreferences &p, std::string *e) { return confirmManualMarket(&p,static_cast<uint64_t>(time(nullptr)),e) && manualStore.save(p,e); }, static_cast<uint64_t>(time(nullptr)),
                [&](ElectricityPreferences &p,std::string *e){return confirmElectricityPreferences(&p,static_cast<uint64_t>(time(nullptr)),e)&&electricityStore.save(p,e);},
                [&](const JsonValue &p,std::string *e){return tariffStore.save(p,e);});
            if (result == SettingsResult::OpenRuntime) openRuntimeControls(&panel, &runtimeStore, {});
            else if (result == SettingsResult::OpenManualMarket) { ManualMarketContext context; context.cliDisabled=noMarket; openManualMarketControls(&panel,&manualStore,context); }
            else if (result == SettingsResult::OpenElectricity) {ElectricitySettingsContext context;context.cliNoEnergy=noEnergy;openElectricityControls(&panel,&electricityStore,context);}
            else if (result == SettingsResult::OpenTariff) openTariffControls(&panel,&tariffStore);
            else if (result != SettingsResult::None) { panel.visible = false; break; }
        }
        panel.updateCreditReview();
        panel.updateEnergyReview();
        const auto color = panel.active.color ? detectColorLevel() : ColorLevel::None;
        printSettingsFrame(panel.render(terminalColumns(80), terminalRows(24), color));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    printf("\x1b[2J\x1b[H\x1b[?25hSettings closed. No mining was started.\n");
    return 0;
}

}  // namespace om
