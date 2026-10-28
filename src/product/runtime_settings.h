// Native runtime-default editor. Pure state/rendering; persistence is an injected action.
#pragma once

#include <functional>
#include "dashboard.h"
#include "runtime_preferences.h"

namespace om {

enum class RuntimeSettingsResult { None, Close, Quit };
enum class RuntimeSettingsPage { Browse, EditWait, Review, Leave };

class RuntimeSettingsPanel {
   public:
    using Save = std::function<bool(const RuntimePreferences &, std::string *)>;
    bool visible = false;
    unsigned field = 0;
    unsigned reviewOffset = 0;
    RuntimeSettingsPage page = RuntimeSettingsPage::Browse;
    RuntimePreferences active, draft;
    RuntimeContext context;
    std::string input, message;
    bool replaceInput = true;
    void open(RuntimePreferences value, RuntimeContext current) {
        active = draft = value; context = current; visible = true;
        page = RuntimeSettingsPage::Browse; field = 0; reviewOffset = 0; message.clear();
    }
    bool dirty() const { return active != draft; }
    RuntimeSettingsResult key(int key, const Save &save) {
        if (key == 3) return RuntimeSettingsResult::Quit;
        if (page == RuntimeSettingsPage::EditWait) {
            if (key == 27) { page = RuntimeSettingsPage::Browse; message.clear(); }
            else if (key == '\r' || key == '\n') {
                unsigned value = 0;
                if (!parseWorkWaitMs(input, &value)) message = "Enter an integer from 0 to 1000 ms; Esc cancels this edit.";
                else { draft.workWaitMs = value; page = RuntimeSettingsPage::Browse; message.clear(); }
            } else if (key == 8 || key == 127) {
                if (replaceInput) input.clear(); else if (!input.empty()) input.pop_back();
                replaceInput = false;
            } else if (key >= 32 && key <= 126) {
                if (replaceInput) input.clear();
                replaceInput = false;
                if (input.size() < 16) input += static_cast<char>(key);
            }
            return RuntimeSettingsResult::None;  // Q/D/L/S are text here, not global shortcuts
        }
        if (key == 'q' || key == 'Q') return RuntimeSettingsResult::Quit;
        if (page == RuntimeSettingsPage::Leave) {
            if (key == 'd' || key == 'D') { draft = active; visible = false; return RuntimeSettingsResult::Close; }
            if (key == 27) page = RuntimeSettingsPage::Browse;
            else if (key == '\r' || key == '\n' || key == kKeyF2) page = RuntimeSettingsPage::Review;
        } else if (page == RuntimeSettingsPage::Review) {
            if (key == 27) page = RuntimeSettingsPage::Browse;
            else if (key == kKeyUp || key == kKeyPageUp) reviewOffset = reviewOffset > 0 ? reviewOffset - 1 : 0;
            else if (key == kKeyDown || key == kKeyPageDown) reviewOffset = std::min(30u, reviewOffset + 1);
            else if (key == '\r' || key == '\n' || key == kKeyF2) {
                std::string error;
                if (save && save(draft, &error)) { active = draft; page = RuntimeSettingsPage::Browse; message = "Saved. Next explicit start only; current run is unchanged."; }
                else message = error.empty() ? "Save failed; active defaults unchanged." : error;
            }
        } else if (key == 27) {
            if (dirty()) page = RuntimeSettingsPage::Leave;
            else { visible = false; return RuntimeSettingsResult::Close; }
        } else if (key == 'v' || key == 'V' || key == kKeyF2) { page = RuntimeSettingsPage::Review; reviewOffset = 0; }
        else if (key == kKeyUp || key == kKeyDown || key == '\t' || key == kKeyBackTab) field = 1 - field;
        else if (key == ' ' || key == '\r' || key == '\n') {
            message.clear();
            if (field == 1) draft.historyEnabled = !draft.historyEnabled;
            else { input = std::to_string(draft.workWaitMs); replaceInput = true; page = RuntimeSettingsPage::EditWait; }
        }
        return RuntimeSettingsResult::None;
    }
    std::string render(int columns, int rows, ColorLevel color) const {
        using namespace dash_detail;
        const size_t width = static_cast<size_t>(std::max(20, columns) - 1);
        if (rows > 0 && rows < 12) return "Terminal too short.\nResize to 12+ rows.\n[Ctrl+C] Stop [Esc] Back\n";
        const bool compact = width < 50 || (rows > 0 && rows < 20);
        std::vector<std::string> body = {std::string(compact ? "Runtime controls" : "CanaryMiner / Runtime controls") + (dirty() ? " *" : "")};
        auto line = [&](const std::string &text) { body.push_back(text); };
        if (page == RuntimeSettingsPage::EditWait) {
            line("Host wait (0..1000 milliseconds):"); line("> " + input + "_");
            line("Milliseconds between complete work batches, not duty cycle.");
            line("Q/D/L/S are input, not shortcuts. Ctrl+C still stops.");
        } else if (page == RuntimeSettingsPage::Leave) {
            line("Unsaved runtime defaults"); line("[Enter] Review/save [D] Discard [Esc] Continue editing");
        } else if (page == RuntimeSettingsPage::Review) {
            line("Review runtime-preferences.json only");
            line("Next explicit start only. Arrows scroll this review.");
            if (!dirty()) line("No changes.");
            if (active.workWaitMs != draft.workWaitMs) line("Host wait: " + std::to_string(active.workWaitMs) + " -> " + std::to_string(draft.workWaitMs) + " ms");
            if (active.historyEnabled != draft.historyEnabled) line("History collection: " + on(active.historyEnabled) + " -> " + on(draft.historyEnabled));
            line("Activation: " + std::string(activationName(ChangeActivation::NextStart)));
            line("Scope: next session's selected GPUs/local history. No deletion.");
            if (context.cliWorkWait || context.cliNoHistory) line("Existing CLI overrides still win on a future run using those flags.");
            line("UI drafts, credentials, donation and hardware are untouched.");
        } else {
            line(std::string(field == 0 ? "> " : "  ") + (compact ? "Wait: " : "Configured host wait: ") + std::to_string(draft.workWaitMs) + (compact ? " ms" : " ms [Enter to edit]"));
            line(std::string(field == 1 ? "> " : "  ") + (compact ? "History: " : "Configured history collection: ") + on(draft.historyEnabled) + (compact ? "" : " [Space to toggle]"));
            if (context.running) {
                line("Current host wait: " + std::to_string(context.effective.workWaitMs) + " ms (" + (context.cliWorkWait ? "CLI override" : "startup defaults") + ")");
                line("Current history: " + on(context.effective.historyEnabled) + (context.cliNoHistory ? " (CLI --no-history)" : " (startup defaults)"));
                const auto next = effectiveRuntimePreferences(draft, context);
                line("Next start with same flags: wait " + std::to_string(next.workWaitMs) + " ms; history " + on(next.historyEnabled));
            } else line("No active mining session. These are local defaults only.");
            line("Activation: next explicit start. No automatic reconnect/restart.");
            line("Zero wait is valid; no hardware tuning or automatic optimization.");
            line("Collection off preserves existing history. UI changes save separately.");
            line("History on requests collection; Main reports actual availability.");
        }
        const std::string footer = page == RuntimeSettingsPage::EditWait ? "[Enter] Apply draft [Esc] Cancel edit [Ctrl+C] Stop" :
            page == RuntimeSettingsPage::Review ? "[Enter/F2] Save defaults [Esc] Edit [Q] Stop" : "[Tab/Arrows] Field [Enter/Space] Change [V/F2] Review [Esc] Back [Q] Stop";
        const auto footerRows = wrapText(footer, width);
        const size_t budget = rows > 0 ? static_cast<size_t>(std::max(1, rows - static_cast<int>(footerRows.size()) - 2)) : 1000;
        std::vector<std::string> wrapped;
        for (const auto &text : body) for (const auto &part : wrapText(text, width)) wrapped.push_back(part);
        if (page == RuntimeSettingsPage::Review && wrapped.size() > budget) {
            const size_t start = std::min(static_cast<size_t>(reviewOffset), wrapped.size() - std::min(wrapped.size(), budget));
            wrapped.erase(wrapped.begin(), wrapped.begin() + start);
        }
        if (wrapped.size() > budget) wrapped.resize(budget);  // focused fields/editor always precede explanatory rows
        std::string text;
        for (size_t i = 0; i < wrapped.size(); ++i) text += paint(wrapped[i], i == 0 ? Tone::Canary : Tone::Plain, color) + "\n";
        text += paint(clip(message.empty() ? "Local defaults; never starts mining or deletes history." : message, width), Tone::Plain, color) + "\n";
        for (const auto &part : footerRows) text += paint(part, Tone::Plain, color) + "\n";
        return text;
    }
   private:
    static std::string on(bool value) { return value ? "on" : "off"; }
};

}  // namespace om
