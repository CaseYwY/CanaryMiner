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
#include "game_settings.h"
#include "pool_security_settings.h"
#include "language_settings.h"
#include "settings_messages.h"
#include "ui_field_messages.h"
#include "settings_change_messages.h"
#include "settings_action_messages.h"
#include "manual_market_settings.h"
#include "electricity_settings.h"
#include "tariff_settings.h"
#include "credit_review.h"
#include "energy_review.h"
#include "energy_authorization_settings.h"
#include "energy_runtime_access.h"
#include "billing_review.h"

namespace om {

enum class SettingsPage { Browse, Review, Leave };
enum class SettingsResult { None, Close, Quit, OpenRuntime, OpenManualMarket, OpenElectricity, OpenTariff, OpenGame, OpenPoolSecurity, OpenLanguage };

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
    GameSettingsPanel game;
    PoolSecuritySettingsPanel poolSecurity;
    LanguageSettingsPanel language;
    ManualMarketSettingsPanel manual;
    ElectricitySettingsPanel electricity;
    TariffSettingsPanel tariff;
    CreditReviewPanel credit;
    CreditReviewAccess creditAccess; // default empty; this does not construct or activate a credit owner
    EnergyReviewPanel energyReview;
    EnergyReviewAccess energyAccess; // independent, default empty; opening the page is not persistence authorization
    EnergyAuthorizationPanel energyAuthorization;
    EnergyAuthorizationAccess energyAuthorizationAccess; // injected memory-only helper admission; no automatic plan issuance
    BillingReviewPanel billingReview;
    BillingReviewAccess billingAccess; // default empty; caller retains ownership, stop/join is not a renderer operation

    void open(const UiPreferences &value) {
        active = draft = value;
        visible = true;
        groupsFocused = true;
        page = SettingsPage::Browse;
        reviewOffset = 0;
        message.clear();
        runtime.visible = false;
        game.visible = false;
        poolSecurity.visible=false;
        language.visible=false;
        manual.visible = false;
        electricity.visible = false;
        tariff.visible = false;
        credit.visible = false;
        energyReview.visible = false;
        energyAuthorization.visible = false;
        billingReview=BillingReviewPanel{};
    }
    bool dirty() const { return active != draft; }

    static const std::vector<std::string> &groups() {
        // Source extraction only: requested translations remain unactivated in the legacy byte renderer.
        static const std::vector<std::string> names=[](){std::vector<std::string> result;
            for(auto key:i18n::settingsGroupKeys())result.push_back(i18n::settingsEnglish(key));
            return result;}();
        return names;
    }
    int fields() const { return group == 8 ? 6 : group == 2 ? 5 : group == 0 ? 3 : (group == 1 || group == 3 || group == 5 || group == 6 || group == 7) ? 1 : group == 9 ? 5 : group == 10 ? 7 : 0; }
    void updateCreditReview(){if(credit.visible&&creditAccess.snapshot)credit.update(creditAccess.snapshot());}
    void updateEnergyReview(){if(energyReview.visible&&energyAccess.snapshot)energyReview.update(energyAccess.snapshot());}
    void updateEnergyAuthorization(){if(energyAuthorization.visible&&energyAuthorizationAccess.snapshot)energyAuthorization.update(energyAuthorizationAccess.snapshot());}
    void updateBillingReview(){if(billingReview.visible)billingReview.sync(billingAccess);}
    void stopBillingReview(){billingReview.key(3,billingAccess);}

    SettingsResult key(int key, const Save &save, const RuntimeSettingsPanel::Save &runtimeSave = {}, const ManualMarketSettingsPanel::Save &manualSave = {}, uint64_t nowUtc = 0, const ElectricitySettingsPanel::Save &electricitySave = {}, const TariffSettingsPanel::Save &tariffSave = {}, const GameSettingsPanel::Save &gameSave = {}, const PoolSecuritySettingsPanel::Save &poolSave = {},const LanguageSettingsPanel::Save &languageSave={}) {
        const auto result=routeKey(key,save,runtimeSave,manualSave,nowUtc,electricitySave,tariffSave,gameSave,poolSave,languageSave);
        if(result==SettingsResult::Quit)stopBillingReview();
        return result;
    }
    SettingsResult routeKey(int key,const Save &save,const RuntimeSettingsPanel::Save &runtimeSave,const ManualMarketSettingsPanel::Save &manualSave,uint64_t nowUtc,const ElectricitySettingsPanel::Save &electricitySave,const TariffSettingsPanel::Save &tariffSave,const GameSettingsPanel::Save &gameSave,const PoolSecuritySettingsPanel::Save &poolSave,const LanguageSettingsPanel::Save &languageSave){
        if(language.visible)return language.key(key,languageSave)==LanguageSettingsResult::Quit?SettingsResult::Quit:SettingsResult::None;
        if(poolSecurity.visible)return poolSecurity.key(key,poolSave)==PoolSecuritySettingsResult::Quit?SettingsResult::Quit:SettingsResult::None;
        if(game.visible)return game.key(key,gameSave)==GameSettingsResult::Quit?SettingsResult::Quit:SettingsResult::None;
        if(billingReview.visible)return billingReview.key(key,billingAccess)==BillingReviewResult::Quit?SettingsResult::Quit:SettingsResult::None;
        if(energyAuthorization.visible){updateEnergyAuthorization();return energyAuthorization.key(key,energyAuthorizationAccess)==EnergyAuthorizationPanelResult::Quit?SettingsResult::Quit:SettingsResult::None;}
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
                    message = i18n::settingsEnglish(i18n::SettingsMessageKey::Saved);
                    page = SettingsPage::Browse;
                } else message = error.empty() ? i18n::settingsEnglish(i18n::SettingsMessageKey::SaveFailed) : error;
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
            else if (group == 5) return SettingsResult::OpenGame;
            else if (group == 6) return SettingsResult::OpenPoolSecurity;
            else if (group == 7) return SettingsResult::OpenLanguage;
            else if ((group==2&&field==4)||(group==8&&field==5)) billingReview.open(billingAccess.snapshot?billingAccess.snapshot():nullptr);
            else if ((group==2&&field==3)||(group==8&&field==4)) energyAuthorization.open(energyAuthorizationAccess.snapshot?energyAuthorizationAccess.snapshot():nullptr);
            else if ((group==2&&field==2)||(group==8&&field==3)) energyReview.open(energyAccess.snapshot?energyAccess.snapshot():nullptr);
            else if (group == 2) return field==0?SettingsResult::OpenElectricity:SettingsResult::OpenTariff;
            else if (group == 8 && field == 2) credit.open(creditAccess.snapshot?creditAccess.snapshot():nullptr);
            else change();
        }
        return SettingsResult::None;
    }

    std::vector<std::string> changes() const {
        using ChangeKey=i18n::SettingsChangeMessageKey;
        std::vector<std::string> result;
        if (active.refreshMs != draft.refreshMs) result.push_back(i18n::settingsChangeEnglish(ChangeKey::Refresh,std::to_string(active.refreshMs),std::to_string(draft.refreshMs)+" ms"));
        if (active.color != draft.color) result.push_back(i18n::settingsChangeEnglish(ChangeKey::Color,on(active.color),on(draft.color)));
        if (active.showEvents != draft.showEvents) result.push_back(i18n::settingsChangeEnglish(ChangeKey::Events,on(active.showEvents),on(draft.showEvents)));
        if (active.hideWorker != draft.hideWorker) result.push_back(i18n::settingsChangeEnglish(ChangeKey::HideWorker,on(active.hideWorker),on(draft.hideWorker)));
        const std::pair<ChangeKey, std::pair<bool, bool>> switches[] = {
            {ChangeKey::DetailsPage, {active.detailsEnabled, draft.detailsEnabled}}, {ChangeKey::LogPage, {active.logEnabled, draft.logEnabled}},
            {ChangeKey::DetailsIdentity, {active.detailsIdentity, draft.detailsIdentity}}, {ChangeKey::DetailsPool, {active.detailsPool, draft.detailsPool}},
            {ChangeKey::DetailsRestarts, {active.detailsRestarts, draft.detailsRestarts}}, {ChangeKey::DetailsErrors, {active.detailsErrors, draft.detailsErrors}},
            {ChangeKey::UnclassifiedLogs, {active.logUnclassified, draft.logUnclassified}}, {ChangeKey::LogTimestamps, {active.logTimestamps, draft.logTimestamps}},
            {ChangeKey::IsoUtcTime, {active.logIsoTime, draft.logIsoTime}}, {ChangeKey::LogFollow, {active.logFollow, draft.logFollow}}};
        for (const auto &item : switches) if (item.second.first != item.second.second)
            result.push_back(i18n::settingsChangeEnglish(item.first,on(item.second.first),on(item.second.second)));
        if (active.logSeverity != draft.logSeverity) result.push_back(i18n::settingsChangeEnglish(ChangeKey::LogLevel,level(active.logSeverity),level(draft.logSeverity)));
        if (active.logRows != draft.logRows) result.push_back(i18n::settingsChangeEnglish(ChangeKey::LogRecords,std::to_string(active.logRows),std::to_string(draft.logRows)));
        return result;
    }

    std::string render(int columns, int rows, ColorLevel color) const {
        if(language.visible)return language.render(columns,rows,color);
        if(poolSecurity.visible)return poolSecurity.render(columns,rows,color);
        if(game.visible)return game.render(columns,rows,color);
        if(billingReview.visible)return billingReview.render(columns,rows,color);
        if(energyAuthorization.visible)return energyAuthorization.render(columns,rows,color,energyAuthorizationAccess);
        if(energyReview.visible)return energyReview.render(columns,rows,color);
        if(tariff.visible)return tariff.render(columns,rows,color);
        if(credit.visible)return credit.render(columns,rows,color);
        if (electricity.visible) return electricity.render(columns,rows,color);
        if (manual.visible) return manual.render(columns, rows, color);
        if (runtime.visible) return runtime.render(columns, rows, color);
        using namespace dash_detail;
        const size_t width = static_cast<size_t>(std::max(20, columns) - 1);
        const bool compact = rows > 0 && rows < 20;
        using ShellKey=i18n::SettingsMessageKey;
        if (rows > 0 && rows < 12) return i18n::settingsEnglish(ShellKey::ShortTitle)+"\n"+i18n::settingsEnglish(ShellKey::ShortResize)+"\n"+i18n::settingsEnglish(ShellKey::ShortKeys)+"\n";
        const std::string footer = i18n::settingsEnglish(page==SettingsPage::Review?ShellKey::FooterReview:ShellKey::FooterBrowse);
        const auto footerRows = wrapText(footer, width);
        const size_t budget = rows > 0 ? static_cast<size_t>(std::max(1, rows - static_cast<int>(footerRows.size()) - 2)) : 1000;
        std::vector<Line> body;
        body.push_back(one(i18n::settingsEnglish(ShellKey::Title) + (dirty()?i18n::settingsEnglish(ShellKey::UnsavedSuffix):std::string{}), Tone::Canary));
        if (!compact) {
            body.push_back(one(i18n::settingsEnglish(ShellKey::SeparateSaves), Tone::Plain));
            body.push_back(one(""));
        }
        if (page == SettingsPage::Review) {
            const auto diff = changes();
            body.push_back(one(i18n::settingsEnglishField(ShellKey::ReviewHeading,std::to_string(diff.size())), Tone::Canary));
            if (diff.empty()) body.push_back(one(i18n::settingsEnglish(ShellKey::NoChanges)));
            const size_t start = diff.empty() ? 0 : std::min(static_cast<size_t>(reviewOffset), diff.size() - 1);
            for (size_t i = start; i < diff.size(); ++i) body.push_back(one(diff[i]));
            body.push_back(one(i18n::settingsEnglish(ShellKey::Untouched)));
        } else if (page == SettingsPage::Leave) {
            body.push_back(one(i18n::settingsEnglish(ShellKey::Unsaved), Tone::Canary));
            body.push_back(one(i18n::settingsEnglish(ShellKey::LeaveKeys)));
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
                body.push_back(one(i18n::settingsEnglishField(ShellKey::GroupHeading,groups()[group]), Tone::Canary));
                const size_t available = budget > body.size() + 1 ? budget - body.size() - 1 : 1;
                const size_t start = !groupsFocused && static_cast<size_t>(field) >= available ? static_cast<size_t>(field) - available + 1 : 0;
                for (size_t i = start; i < content.size() && i < start + available; ++i) body.push_back(Line{content[i]});
            }
            body.push_back(one(i18n::settingsEnglish(groupsFocused?ShellKey::FocusGroups:ShellKey::FocusFields), Tone::Plain));
        }
        if (body.size() > budget) body.resize(budget);
        std::string output;
        for (const auto &line : body) output += paintLine(fitLine(line, width), color) + "\n";
        const std::string note = message.empty() ? i18n::settingsEnglish(ShellKey::PrivacyNote) : message;
        output += paint(clip(note, width), Tone::Plain, color) + "\n";
        for (const auto &line : footerRows) output += paint(line, Tone::Plain, color) + "\n";
        return output;
    }

   private:
    static std::string on(bool value) { return i18n::uiFieldEnglish(value?i18n::UiFieldMessageKey::On:i18n::UiFieldMessageKey::Off); }
    static std::string level(unsigned value) { return i18n::uiFieldEnglish(value==1?i18n::UiFieldMessageKey::Warnings:value==2?i18n::UiFieldMessageKey::Errors:i18n::UiFieldMessageKey::All); }
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
            else message = i18n::settingsActionEnglish(i18n::SettingsActionMessageKey::RawDrain);
        } else message = i18n::settingsActionEnglish(i18n::SettingsActionMessageKey::NotChanged);
    }
    std::vector<dash_detail::Seg> fieldsText() const {
        using namespace dash_detail;
        using FieldKey=i18n::UiFieldMessageKey;
        using ActionKey=i18n::SettingsActionMessageKey;
        std::vector<Seg> result;
        auto row = [&](int index, const std::string &text) {
            result.push_back({std::string(!groupsFocused && field == index ? "> " : "  ") + text,
                              !groupsFocused && field == index ? Tone::Canary : Tone::Plain});
        };
        auto toggle = [&](int index, FieldKey key, bool value) { row(index, i18n::uiFieldEnglish(key,value?"x":" ")); };
        if (group == 0) {
            row(0, i18n::uiFieldEnglish(FieldKey::Refresh,std::to_string(draft.refreshMs)+" ms"));
            toggle(1,FieldKey::Color,draft.color);
            toggle(2,FieldKey::Events,draft.showEvents);
            result.push_back({"", Tone::Plain});
            result.push_back({i18n::uiFieldEnglish(FieldKey::SamplingNotice), Tone::Plain});
            result.push_back({i18n::uiFieldEnglish(FieldKey::IntensityNotice), Tone::Plain});
        } else if (group == 8) {
            row(0, i18n::settingsActionEnglish(ActionKey::HideWorker,draft.hideWorker?"x":" "));
            row(1, i18n::settingsActionEnglish(ActionKey::HistoryDefaults));
            row(2, i18n::settingsActionEnglish(ActionKey::CreditStatus));
            row(3, i18n::settingsActionEnglish(ActionKey::EnergyStatus));
            row(4, i18n::settingsActionEnglish(ActionKey::EnergyAuthorization));
            row(5, i18n::settingsActionEnglish(ActionKey::BillingReview));
            result.push_back({i18n::settingsActionEnglish(ActionKey::PrivacyPresentation), Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::PrivacyNotAnonymized), Tone::Plain});
        } else if (group == 1) {
            row(0, i18n::settingsActionEnglish(ActionKey::HostWait));
            result.push_back({i18n::settingsActionEnglish(ActionKey::ManualControls), Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::NextStart), Tone::Plain});
        } else if (group == 3) {
            row(0, i18n::settingsActionEnglish(ActionKey::ManualReferences));
            result.push_back({i18n::settingsActionEnglish(ActionKey::DeclaredMarket), Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::NoMarket), Tone::Plain});
        } else if (group == 5) {
            row(0,i18n::settingsActionEnglish(ActionKey::GameDefaults));
            result.push_back({i18n::settingsActionEnglish(ActionKey::GameConfigured),Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::GameRefusal),Tone::Plain});
        } else if(group==6){
            row(0,i18n::settingsActionEnglish(ActionKey::PoolDeclarations));
            result.push_back({i18n::settingsActionEnglish(ActionKey::PoolConsent),Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::PoolRefusal),Tone::Plain});
        } else if(group==7){
            row(0,i18n::settingsActionEnglish(ActionKey::LanguageRequests));
            result.push_back({i18n::settingsActionEnglish(ActionKey::LanguageSeparate),Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::LanguageEffective),Tone::Plain});
        } else if (group == 2) {
            row(0,i18n::settingsActionEnglish(ActionKey::SessionEnergy));
            row(1,i18n::settingsActionEnglish(ActionKey::CombinedTariff));
            row(2,i18n::settingsActionEnglish(ActionKey::EnergyRecovery));
            row(3,i18n::settingsActionEnglish(ActionKey::EnergyAuthorization));
            row(4,i18n::settingsActionEnglish(ActionKey::BillingReview));
            result.push_back({i18n::settingsActionEnglish(ActionKey::EnergyNextStart),Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::EnergyNotWhole),Tone::Plain});
        } else if (group == 9) {
            toggle(0, FieldKey::DetailsPage, draft.detailsEnabled);
            toggle(1, FieldKey::DeviceIdentity, draft.detailsIdentity);
            toggle(2, FieldKey::PoolState, draft.detailsPool);
            toggle(3, FieldKey::Restarts, draft.detailsRestarts);
            toggle(4, FieldKey::LastError, draft.detailsErrors);
            result.push_back({i18n::uiFieldEnglish(FieldKey::SafetyNotice), Tone::Plain});
        } else if (group == 10) {
            toggle(0, FieldKey::LogPage, draft.logEnabled);
            row(1, i18n::uiFieldEnglish(FieldKey::Severity,level(draft.logSeverity)));
            toggle(2, FieldKey::IncludeUnclassified, draft.logUnclassified);
            row(3, i18n::uiFieldEnglish(FieldKey::Records,std::to_string(draft.logRows)));
            toggle(4, FieldKey::ObservedTime, draft.logTimestamps);
            row(5, i18n::uiFieldEnglish(FieldKey::TimeFormat,draft.logIsoTime?"ISO UTC":"UTC HH:MM:SS"));
            toggle(6, FieldKey::Follow, draft.logFollow);
            result.push_back({i18n::uiFieldEnglish(FieldKey::RawSeverity), Tone::Plain});
            result.push_back({i18n::uiFieldEnglish(FieldKey::FreezeNotice), Tone::Plain});
        } else {
            result.push_back({i18n::settingsActionEnglish(ActionKey::Unimplemented), Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::NoActionable), Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::SafetyRequired), Tone::Plain});
            result.push_back({i18n::settingsActionEnglish(ActionKey::Roadmap), Tone::Plain});
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
inline void openGameControls(SettingsPanel *panel,GamePreferencesStore *store,const GameSettingsContext &context){
    if(!store){panel->message="Game defaults unavailable: no local target.";return;}
    GamePreferences value;std::string error;
    if(!store->load(&value,&error)){panel->message="Game defaults unavailable: "+error;return;}
    panel->game.open(std::move(value),context);
}
inline void openPoolSecurityControls(SettingsPanel *panel,PoolSecurityPreferencesStore *store,const PoolSecuritySettingsContext &context){
    if(!store){panel->message="Pool security declarations unavailable: no local target.";return;}
    PoolSecurityPreferences value;std::string error;
    if(!store->load(&value,&error)){panel->message="Pool security declarations unavailable: "+error;return;}
    panel->poolSecurity.open(std::move(value),context);
}
inline void openLanguageControls(SettingsPanel *panel,LanguagePreferencesStore *store,const LanguageSettingsContext &context){
    if(!store){panel->message="Language requests unavailable: no local target.";return;}
    LanguagePreferences value;std::string error;
    if(!store->load(&value,&error)){panel->message="Language requests unavailable: "+error;return;}
    panel->language.open(std::move(value),context);
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

inline int runSettingsCommand(const std::string &stateDir, volatile sig_atomic_t *stop, bool noMarket = false, bool noEnergy = false, CreditReviewAccess creditAccess = {}, EnergyReviewAccess energyAccess = {}, EnergyAuthorizationAccess authorizationAccess = {}, EnergyRuntimeFactory runtimeFactory = {}, const std::vector<std::string> &selectedDevices = {}, BillingReviewAccess billingAccess = {},LanguageSettingsContext languageContext={}) {
#if defined(_WIN32)
    if (!_isatty(_fileno(stdout)) || !_isatty(_fileno(stdin))) {
#else
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
#endif
        fprintf(stderr, "settings requires an interactive terminal; no mining was started\n");
        return 2;
    }
    if(stop&&*stop){if(billingAccess.requestStop)billingAccess.requestStop();return 0;}
    UiPreferencesStore store(std::filesystem::u8path(stateDir) / "ui-preferences.json");
    RuntimePreferencesStore runtimeStore(std::filesystem::u8path(stateDir) / "runtime-preferences.json");
    GamePreferencesStore gameStore(std::filesystem::u8path(stateDir) / "game-preferences.json");
    PoolSecurityPreferencesStore poolStore(std::filesystem::u8path(stateDir) / "pool-security-preferences.json");
    LanguagePreferencesStore languageStore(std::filesystem::u8path(stateDir)/"language-preferences.json");
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
    panel.energyAuthorizationAccess=std::move(authorizationAccess);
    panel.billingAccess=std::move(billingAccess);
    std::unique_ptr<EnergyRuntime> runtime;
    if(runtimeFactory&&!(stop&&*stop)){
        runtime=runtimeFactory(false,noEnergy);
        if(stop&&*stop){if(runtime)runtime->requestStop();}
        else if(runtime&&runtime->start(1,selectedDevices)){
            panel.energyAccess=energyRuntimeReview(*runtime);
            panel.energyAuthorizationAccess=energyRuntimeAuthorization(*runtime);
        }else{runtime.reset();panel.message="Energy persistence unavailable; check the development helper installation.";}
    }
    printf("\x1b[2J\x1b[?25l");
    while (panel.visible && !(stop && *stop)) {
        if(runtime){const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();runtime->tick(utc,0);}
        for (int n = 0; n < 32; ++n) {
            if(stop&&*stop)break;
            const int key = keys.poll();
            if (!key) break;
            const auto result = panel.key(key, [&](const UiPreferences &p, std::string *e) { return store.save(p, e); },
                [&](const RuntimePreferences &p, std::string *e) { return runtimeStore.save(p, e); },
                [&](ManualMarketPreferences &p, std::string *e) { return confirmManualMarket(&p,static_cast<uint64_t>(time(nullptr)),e) && manualStore.save(p,e); }, static_cast<uint64_t>(time(nullptr)),
                [&](ElectricityPreferences &p,std::string *e){return confirmElectricityPreferences(&p,static_cast<uint64_t>(time(nullptr)),e)&&electricityStore.save(p,e);},
                [&](const JsonValue &p,std::string *e){return tariffStore.save(p,e);},
                [&](const GamePreferences &p,std::string *e){return gameStore.save(p,e);},
                [&](const PoolSecurityPreferences &p,std::string *e){return poolStore.save(p,e);},
                [&](const LanguagePreferences &p,std::string *e){return languageStore.save(p,e);});
            if (result == SettingsResult::OpenRuntime) openRuntimeControls(&panel, &runtimeStore, {});
            else if (result == SettingsResult::OpenManualMarket) { ManualMarketContext context; context.cliDisabled=noMarket; openManualMarketControls(&panel,&manualStore,context); }
            else if (result == SettingsResult::OpenElectricity) {ElectricitySettingsContext context;context.cliNoEnergy=noEnergy;openElectricityControls(&panel,&electricityStore,context);}
            else if (result == SettingsResult::OpenTariff) openTariffControls(&panel,&tariffStore);
             else if (result == SettingsResult::OpenGame) openGameControls(&panel,&gameStore,{});
              else if (result == SettingsResult::OpenPoolSecurity) openPoolSecurityControls(&panel,&poolStore,{});
              else if (result == SettingsResult::OpenLanguage) openLanguageControls(&panel,&languageStore,languageContext);
            else if (result != SettingsResult::None) { panel.visible = false;if(runtime)runtime->requestStop();break; }
        }
        panel.updateCreditReview();
        panel.updateEnergyReview();
        panel.updateEnergyAuthorization();
        panel.updateBillingReview();
        const auto color = panel.active.color ? detectColorLevel() : ColorLevel::None;
        printSettingsFrame(panel.render(terminalColumns(80), terminalRows(24), color));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    panel.stopBillingReview();
    if(runtime){runtime->requestStop();runtime->stop();}
    printf("\x1b[2J\x1b[H\x1b[?25hSettings closed. No mining was started.\n");
    return 0;
}

}  // namespace om
