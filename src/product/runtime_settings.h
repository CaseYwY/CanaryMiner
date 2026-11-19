// Native runtime-default editor. Pure state/rendering; persistence is an injected action.
#pragma once

#include <functional>
#include "dashboard.h"
#include "runtime_preferences.h"
#include "runtime_field_messages.h"
#include "runtime_panel_messages.h"
#include "ui_field_messages.h"
#include "market_readiness_messages.h"
#include "market_ticker_settings_messages.h"

namespace om {

enum class RuntimeSettingsResult { None, Close, Quit };
enum class RuntimeSettingsPage { Browse, EditWait, Review, Leave };
enum class RuntimeSettingsSection { Controls, History, PublicTickers };

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
    void open(RuntimePreferences value, RuntimeContext current, RuntimeSettingsSection section=RuntimeSettingsSection::Controls) {
        active = draft = value; context = current; visible = true;
        publicTickers_ = section == RuntimeSettingsSection::PublicTickers;
        page = RuntimeSettingsPage::Browse; field = section == RuntimeSettingsSection::History ? 1 : 0;
        reviewOffset = 0; message.clear();
    }
    bool dirty() const { return active != draft; }
    void confirmSaved(const RuntimePreferences &value) {
        const bool economyChanged = active.economicModeRequested != value.economicModeRequested;
        active = draft = value;
        page = RuntimeSettingsPage::Browse;
        message = publicTickers_ ? i18n::marketTickerSettingsEnglish(i18n::MarketTickerSettingsKey::SaveNotice) :
            economyChanged ? i18n::marketReadinessEnglish(i18n::MarketReadinessMessageKey::EconomySaved) :
            i18n::runtimePanelEnglish(i18n::RuntimePanelMessageKey::Saved);
    }
    RuntimeSettingsResult key(int key, const Save &save) {
        if (key == 3) return RuntimeSettingsResult::Quit;
        if (page == RuntimeSettingsPage::EditWait) {
            if (key == 27) { page = RuntimeSettingsPage::Browse; message.clear(); }
            else if (key == '\r' || key == '\n') {
                unsigned value = 0;
                if (!parseWorkWaitMs(input, &value)) message = i18n::runtimePanelEnglish(i18n::RuntimePanelMessageKey::InvalidWait);
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
                if (save && save(draft, &error)) confirmSaved(draft);
                else message = error.empty() ? i18n::runtimePanelEnglish(i18n::RuntimePanelMessageKey::SaveFailed) : error;
            }
        } else if (key == 27) {
            if (dirty()) page = RuntimeSettingsPage::Leave;
            else { visible = false; return RuntimeSettingsResult::Close; }
        } else if (key == 'v' || key == 'V' || key == kKeyF2) { page = RuntimeSettingsPage::Review; reviewOffset = 0; }
        else if (publicTickers_) {
            if (key == ' ' || key == '\r' || key == '\n') {
                draft.publicTickersRequested = !draft.publicTickersRequested;
                message.clear();
            }
        }
        else if (key == kKeyUp || key == kKeyBackTab) field = (field + 2) % 3;
        else if (key == kKeyDown || key == '\t') field = (field + 1) % 3;
        else if (key == ' ' || key == '\r' || key == '\n') {
            message.clear();
            if (field == 1) draft.historyEnabled = !draft.historyEnabled;
            else if (field == 2) draft.economicModeRequested = !draft.economicModeRequested;
            else { input = std::to_string(draft.workWaitMs); replaceInput = true; page = RuntimeSettingsPage::EditWait; }
        }
        return RuntimeSettingsResult::None;
    }
    std::string render(int columns, int rows, ColorLevel color) const {
        using namespace dash_detail;
        using FieldKey=i18n::RuntimeFieldMessageKey;
        using PanelKey=i18n::RuntimePanelMessageKey;
        using MarketKey=i18n::MarketReadinessMessageKey;
        using TickerKey=i18n::MarketTickerSettingsKey;
        using i18n::marketReadinessEnglish;
        using i18n::marketTickerSettingsEnglish;
        const size_t width = static_cast<size_t>(std::max(20, columns) - 1);
        if (rows > 0 && rows < 12) return i18n::runtimePanelEnglish(PanelKey::ShortTitle)+"\n"+i18n::runtimePanelEnglish(PanelKey::ShortResize)+"\n"+i18n::runtimePanelEnglish(PanelKey::ShortKeys)+"\n";
        const bool compact = width < 50 || (rows > 0 && rows < 20);
        const bool narrowTickers=publicTickers_&&width<50;
        const auto tickerTitle=narrowTickers?(page==RuntimeSettingsPage::Review?TickerKey::CompactReview:TickerKey::CompactTitle):TickerKey::Title;
        const auto title=publicTickers_?marketTickerSettingsEnglish(tickerTitle):
            i18n::runtimePanelEnglish(compact?PanelKey::TitleCompact:PanelKey::TitleFull);
        std::vector<std::string> body = {title + (dirty()?i18n::runtimePanelEnglish(PanelKey::UnsavedSuffix):std::string{})};
        auto line = [&](const std::string &text) { body.push_back(text); };
        if (page == RuntimeSettingsPage::EditWait) {
            line(i18n::runtimePanelEnglish(PanelKey::EditPrompt)); line(i18n::runtimePanelEnglishInput(input));
            line(i18n::runtimePanelEnglish(PanelKey::EditExplanation));
            line(i18n::runtimePanelEnglish(PanelKey::EditKeys));
        } else if (page == RuntimeSettingsPage::Leave) {
            line(i18n::runtimePanelEnglish(PanelKey::LeaveTitle)); line(i18n::runtimePanelEnglish(PanelKey::LeaveKeys));
        } else if (publicTickers_) {
            if (page == RuntimeSettingsPage::Review) {
                if(!narrowTickers)line(i18n::runtimePanelEnglish(PanelKey::ReviewHeading));
                if (dirty()) line(marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactChange:TickerKey::Change,
                    {{"before",on(active.publicTickersRequested)},{"after",on(draft.publicTickersRequested)}}));
                else line(i18n::runtimePanelEnglish(PanelKey::NoChanges));
            } else {
                line("> "+marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactRequest:TickerKey::Request,{{"request",on(draft.publicTickersRequested)}}));
                if (context.running) line(marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactCurrent:TickerKey::Current,
                    {{"request",on(context.effective.publicTickersRequested)}}));
            }
            if (context.cliNoMarket) line(marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactCliBlocked:TickerKey::CliBlocked));
            line(marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactUnavailable:TickerKey::Unavailable));
            line(marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactNextStart:TickerKey::NextStart));
            line(marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactScope:TickerKey::Scope));
            if(narrowTickers&&page==RuntimeSettingsPage::Review){
                line(marketTickerSettingsEnglish(TickerKey::CompactGate));
                line(marketTickerSettingsEnglish(TickerKey::CompactKraken));
            }
            line(marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactConflict:TickerKey::Conflict));
        } else if (page == RuntimeSettingsPage::Review) {
            const bool economyChanged = active.economicModeRequested != draft.economicModeRequested;
            const bool nextStartChanged = active.workWaitMs != draft.workWaitMs || active.historyEnabled != draft.historyEnabled;
            const bool showNextStart = !economyChanged || nextStartChanged;
            if (economyChanged) {
                line(marketReadinessEnglish(compact ? MarketKey::EconomyChangeCompact : MarketKey::EconomyChange,
                    {{"before",on(active.economicModeRequested)},{"after",on(draft.economicModeRequested)}}));
                line(marketReadinessEnglish(compact ? MarketKey::EconomyOffCompact : MarketKey::EconomyOff));
            }
            line(i18n::runtimePanelEnglish(PanelKey::ReviewHeading));
            if (showNextStart) line(i18n::runtimePanelEnglish(PanelKey::NextStartReview));
            if (!dirty()) line(i18n::runtimePanelEnglish(PanelKey::NoChanges));
            if (active.workWaitMs != draft.workWaitMs) line(i18n::runtimeFieldEnglish(FieldKey::WaitChange,{{"before",std::to_string(active.workWaitMs)},{"after",std::to_string(draft.workWaitMs)+" ms"}}));
            if (active.historyEnabled != draft.historyEnabled) line(i18n::runtimeFieldEnglish(FieldKey::HistoryChange,{{"before",on(active.historyEnabled)},{"after",on(draft.historyEnabled)}}));
            if (showNextStart) {
                line(i18n::runtimeFieldEnglish(FieldKey::Activation,{{"activation",activationName(ChangeActivation::NextStart)}}));
                line(i18n::runtimePanelEnglish(PanelKey::Scope));
                if (context.cliWorkWait || context.cliNoHistory) line(i18n::runtimePanelEnglish(PanelKey::FutureCli));
            }
            line(i18n::runtimePanelEnglish(PanelKey::Untouched));
        } else {
            const auto economy = std::string(field == 2 ? "> " : "  ") +
                marketReadinessEnglish(MarketKey::Economy, {{"request",on(draft.economicModeRequested)}});
            if (compact && field == 2) {
                line(economy);
                line(marketReadinessEnglish(MarketKey::EconomyOffCompact));
            }
            line(std::string(field == 0 ? "> " : "  ") + i18n::runtimeFieldEnglish(compact?FieldKey::WaitCompact:FieldKey::WaitConfigured,{{"wait",std::to_string(draft.workWaitMs)+" ms"}}));
            line(std::string(field == 1 ? "> " : "  ") + i18n::runtimeFieldEnglish(compact?FieldKey::HistoryCompact:FieldKey::HistoryConfigured,{{"history",on(draft.historyEnabled)}}));
            if (!compact || field != 2) line(economy);
            if (context.running) {
                line(i18n::runtimeFieldEnglish(FieldKey::CurrentWait,{{"wait",std::to_string(context.effective.workWaitMs)+" ms"},{"source",i18n::runtimeFieldEnglish(context.cliWorkWait?FieldKey::SourceCli:FieldKey::SourceDefaults)}}));
                line(i18n::runtimeFieldEnglish(context.cliNoHistory?FieldKey::CurrentHistoryCli:FieldKey::CurrentHistoryDefaults,{{"history",on(context.effective.historyEnabled)}}));
                const auto next = effectiveRuntimePreferences(draft, context);
                line(i18n::runtimeFieldEnglish(FieldKey::NextSameFlags,{{"wait",std::to_string(next.workWaitMs)+" ms"},{"history",on(next.historyEnabled)}}));
            } else line(i18n::runtimePanelEnglish(PanelKey::NoSession));
            line(marketReadinessEnglish(MarketKey::EconomyOff));
            line(i18n::runtimePanelEnglish(PanelKey::ActivationNotice));
            line(i18n::runtimePanelEnglish(PanelKey::ZeroWait));
            line(i18n::runtimePanelEnglish(PanelKey::CollectionOff));
            line(i18n::runtimePanelEnglish(PanelKey::CollectionOn));
        }
        std::string footer=i18n::runtimePanelEnglish(page==RuntimeSettingsPage::EditWait?PanelKey::FooterEdit:page==RuntimeSettingsPage::Review?PanelKey::FooterReview:PanelKey::FooterBrowse);
        if(publicTickers_&&page==RuntimeSettingsPage::Browse)
            footer=marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactFooter:TickerKey::Footer);
        if(publicTickers_&&page==RuntimeSettingsPage::Review)
            footer=marketTickerSettingsEnglish(narrowTickers?TickerKey::CompactReviewFooter:TickerKey::ReviewFooter);
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
        text += paint(clip(message.empty() ? i18n::runtimePanelEnglish(PanelKey::LocalDefaults) : message, width), Tone::Plain, color) + "\n";
        for (const auto &part : footerRows) text += paint(part, Tone::Plain, color) + "\n";
        return text;
    }
   private:
    bool publicTickers_=false;
    static std::string on(bool value) { return i18n::uiFieldEnglish(value?i18n::UiFieldMessageKey::On:i18n::UiFieldMessageKey::Off); }
};

}  // namespace om
