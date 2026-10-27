// Keyboard settings state machine + pure renderer. No network, tuning, mining or privileged actions.
#pragma once

#include <functional>
#include <signal.h>
#include <string>
#include <vector>

#include "../core/telemetry.h"
#include "dashboard.h"
#include "ui_preferences.h"

namespace om {

enum class SettingsPage { Browse, Review, Leave };
enum class SettingsResult { None, Close, Quit };

class SettingsPanel {
   public:
    using Save = std::function<bool(const UiPreferences &, std::string *)>;
    bool visible = false;
    bool groupsFocused = true;
    int group = 0, field = 0;
    SettingsPage page = SettingsPage::Browse;
    UiPreferences active, draft;
    std::string message;

    void open(const UiPreferences &value) {
        active = draft = value;
        visible = true;
        groupsFocused = true;
        page = SettingsPage::Browse;
        message.clear();
    }
    bool dirty() const { return active != draft; }

    static const std::vector<std::string> &groups() {
        static const std::vector<std::string> names = {"Display", "Automatic mining", "Electricity", "Market and timing", "Profiles and tests",
                                                      "Games", "Pools and TLS", "Language", "Privacy"};
        return names;
    }
    int fields() const { return group == 0 ? 3 : group == 8 ? 1 : 0; }

    SettingsResult key(int key, const Save &save) {
        if (key == 3 || key == 'q' || key == 'Q') return SettingsResult::Quit;
        if (page == SettingsPage::Leave) {
            if (key == 'd' || key == 'D') { draft = active; visible = false; return SettingsResult::Close; }
            if (key == 27 || key == 'c' || key == 'C') { page = SettingsPage::Browse; return SettingsResult::None; }
            if (key == '\r' || key == '\n' || key == kKeyF2) page = SettingsPage::Review;
            return SettingsResult::None;
        }
        if (page == SettingsPage::Review) {
            if (key == 27) { page = SettingsPage::Browse; message.clear(); }
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
        return result;
    }

    std::string render(int columns, int rows, ColorLevel color) const {
        using namespace dash_detail;
        const size_t width = static_cast<size_t>(std::max(20, columns) - 1);
        const bool compact = rows > 0 && rows < 20;
        if (rows > 0 && rows < 12) return "Terminal too short.\nResize to 12+ rows.\n[Q] Stop [Esc] Back\n";
        std::vector<Line> body;
        body.push_back(one("(o> CanaryMiner / Settings" + std::string(dirty() ? " * unsaved" : ""), Tone::Canary));
        if (!compact) {
            body.push_back(one("Presentation only. Mining continues under its existing policy.", Tone::Plain));
            body.push_back(one(""));
        }
        if (page == SettingsPage::Review) {
            body.push_back(one("Review changes / live after save", Tone::Canary));
            const auto diff = changes();
            if (diff.empty()) body.push_back(one("No changes."));
            for (const auto &line : diff) body.push_back(one(line));
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
                for (const auto &line : content) body.push_back(Line{line});
            }
            body.push_back(one(groupsFocused ? "Focus: groups / Tab or Enter to edit" : "Focus: fields / Tab to choose a group", Tone::Plain));
        }
        const std::string footer = page == SettingsPage::Review ? "[Enter/F2] Save  [Esc] Edit  [Q] Stop" :
                                   "[Arrows] Move [Tab] Pane [Space/Enter] Change [V/F2] Review [Esc] Back [Q] Stop";
        const auto footerRows = wrapText(footer, width);
        const size_t budget = rows > 0 ? static_cast<size_t>(std::max(1, rows - static_cast<int>(footerRows.size()) - 2)) : body.size();
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
    void change() {
        message.clear();
        if (group == 8) draft.hideWorker = !draft.hideWorker;
        else if (group == 0) {
            if (field == 0) {
                const unsigned choices[] = {100, 250, 500, 1000};
                for (size_t i = 0; i < 4; ++i) if (draft.refreshMs == choices[i]) { draft.refreshMs = choices[(i + 1) % 4]; break; }
            } else if (field == 1) draft.color = !draft.color;
            else draft.showEvents = !draft.showEvents;
        } else message = "Not implemented yet; no switch or mining behaviour was changed.";
    }
    std::vector<dash_detail::Seg> fieldsText() const {
        using namespace dash_detail;
        std::vector<Seg> result;
        auto row = [&](int index, const std::string &text) {
            result.push_back({std::string(!groupsFocused && field == index ? "> " : "  ") + text,
                              !groupsFocused && field == index ? Tone::Canary : Tone::Plain});
        };
        if (group == 0) {
            row(0, "Display refresh    < " + std::to_string(draft.refreshMs) + " ms >");
            row(1, std::string("Terminal color     [") + (draft.color ? "x" : " ") + "]");
            row(2, std::string("Recent events      [") + (draft.showEvents ? "x" : " ") + "]");
            result.push_back({"", Tone::Plain});
            result.push_back({"Display refresh does not change sampling", Tone::Plain});
            result.push_back({"or mining intensity. NO_COLOR still wins.", Tone::Plain});
        } else if (group == 8) {
            row(0, std::string("Hide worker label  [") + (draft.hideWorker ? "x" : " ") + "]");
            result.push_back({"Presentation only, not a diagnostic scrubber.", Tone::Plain});
            result.push_back({"Logs, API and files are not anonymized by it.", Tone::Plain});
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

inline int runSettingsCommand(const std::string &stateDir, volatile sig_atomic_t *stop) {
#if defined(_WIN32)
    if (!_isatty(_fileno(stdout)) || !_isatty(_fileno(stdin))) {
#else
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
#endif
        fprintf(stderr, "settings requires an interactive terminal; no mining was started\n");
        return 2;
    }
    UiPreferencesStore store(std::filesystem::path(stateDir) / "ui-preferences.json");
    UiPreferences preferences;
    std::string error;
    if (!store.load(&preferences, &error)) { fprintf(stderr, "%s\n", error.c_str()); return 3; }
    enableAnsiOnWindows();
    KeyReader keys;
    SettingsPanel panel;
    panel.open(preferences);
    printf("\x1b[2J\x1b[?25l");
    while (panel.visible && !(stop && *stop)) {
        for (int n = 0; n < 32; ++n) {
            const int key = keys.poll();
            if (!key) break;
            const auto result = panel.key(key, [&](const UiPreferences &p, std::string *e) { return store.save(p, e); });
            if (result != SettingsResult::None) { panel.visible = false; break; }
        }
        const auto color = panel.active.color ? detectColorLevel() : ColorLevel::None;
        printSettingsFrame(panel.render(terminalColumns(80), terminalRows(24), color));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    printf("\x1b[2J\x1b[H\x1b[?25hSettings closed. No mining was started.\n");
    return 0;
}

}  // namespace om
