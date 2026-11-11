// Explicit idle-only settings entry. All eight preference files belong to the background helper.
#pragma once
#include "executable_path.h"
#include "preferences_settings.h"
#include <chrono>
#include <thread>

namespace om {
inline int runSettingsCommand(const std::string &stateDir, volatile sig_atomic_t *stop, bool noMarket = false, bool noEnergy = false, CreditReviewAccess creditAccess = {}, EnergyReviewAccess energyAccess = {}, EnergyAuthorizationAccess authorizationAccess = {}, EnergyRuntimeFactory runtimeFactory = {}, const std::vector<std::string> &selectedDevices = {}, BillingReviewAccess billingAccess = {},LanguageSettingsContext languageContext={}) {
#if defined(_WIN32)
    if (!_isatty(_fileno(stdout)) || !_isatty(_fileno(stdin))) {
#else
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
#endif
        fprintf(stderr, "%s\n", i18n::settingsDiagnosticEnglish(i18n::SettingsDiagnosticMessageKey::RequiresTerminal).c_str());
        return 2;
    }
    if(stop&&*stop){if(billingAccess.requestStop)billingAccess.requestStop();return 0;}
    SessionIntent intent;
    const auto token=intent.explicitStart(); // explicit idle helper lifetime, never a fleet/mining start
    if(!intent.permits(token)){if(billingAccess.requestStop)billingAccess.requestStop();return 3;}
    std::unique_ptr<PreferencesOwner> owner;
    try{
        const auto module=selfExecutablePath();
        if(!module.empty()){
            const auto directory=std::filesystem::u8path(stateDir);
            const char *names[]={"ui-preferences.json","runtime-preferences.json","manual-market-preferences.json","electricity-preferences.json","game-preferences.json","pool-security-preferences.json","language-preferences.json","tariff-declaration.json"};
            std::array<std::filesystem::path,8> targets{};
            for(size_t i=0;i<targets.size();++i)targets[i]=directory/names[i];
            const auto helper=std::filesystem::u8path(module).parent_path()/
#if defined(_WIN32)
                "canary_preferences_worker.exe";
#else
                "canary_preferences_worker";
#endif
            owner=std::make_unique<PreferencesOwner>(helper,std::move(targets),intent,token);
            if(!owner->start())owner.reset();
        }
    }catch(...){owner.reset();}
    if(!owner){intent.stop();if(billingAccess.requestStop)billingAccess.requestStop();fprintf(stderr,"Idle preferences unavailable; no synchronous fallback or file changes.\n");return stop&&*stop?0:3;}
    enableAnsiOnWindows();
    KeyReader keys;
    SettingsPanel panel;
    panel.open(UiPreferences{});
    panel.creditAccess=std::move(creditAccess);
    panel.energyAccess=std::move(energyAccess);
    panel.energyAuthorizationAccess=std::move(authorizationAccess);
    panel.billingAccess=std::move(billingAccess);
    PreferencesSettings preferences(*owner,panel);
    preferences.context.language=std::move(languageContext);
    preferences.context.manual.cliDisabled=noMarket;
    preferences.context.manualProjectionAvailable=true; // idle declarations need no running display projector
    preferences.context.electricity.cliNoEnergy=noEnergy;
    std::unique_ptr<EnergyRuntime> runtime;
    bool initial=true,loadIssued=false,runtimeAttempted=false,quit=false;
    int result=0;
    printf("\x1b[2J\x1b[?25l");
    try{while(panel.visible&&!quit){
        if(stop&&*stop)intent.stop();
        if(!intent.permits(token))break;
        for(int n=0;n<32;++n){
            if(stop&&*stop){intent.stop();break;}
            const int key=keys.poll();if(!key)break;
            if(initial){if(key==3||key==27||key=='q'||key=='Q')quit=true;}
            else{
                const auto utc=static_cast<uint64_t>(time(nullptr));
                const auto mono=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                const auto action=preferences.key(key,utc,mono);
                if(action==SettingsResult::Quit||action==SettingsResult::Close)quit=true;
            }
            if(quit)break;
        }
        if(quit||!intent.permits(token)||(stop&&*stop))break;
        const auto state=owner->snapshot();
        if(initial){
            if(state.state==PreferencesOwnerState::Faulted||state.state==PreferencesOwnerState::Stopped){result=3;break;}
            if(!loadIssued&&state.state==PreferencesOwnerState::Ready){loadIssued=preferences.load(PreferencesKind::Ui);if(!loadIssued){result=3;break;}}
        }
        const auto completed=preferences.poll();
        if(initial&&loadIssued){
            if(completed&&completed->reply&&completed->reply->kind==PreferencesKind::Ui&&completed->reply->status==PreferencesReplyStatus::Loaded)initial=false;
            else if(!preferences.pending()){result=3;break;}
        }
        if(!initial&&!runtimeAttempted){
            runtimeAttempted=true;
            if(runtimeFactory&&intent.permits(token)&&!(stop&&*stop)){
                runtime=runtimeFactory(false,noEnergy);
                if((stop&&*stop)||!intent.permits(token)){if(runtime)runtime->requestStop();break;}
                if(runtime&&runtime->start(1,selectedDevices)){
                    if((stop&&*stop)||!intent.permits(token)){runtime->requestStop();break;}
                    panel.energyAccess=energyRuntimeReview(*runtime);
                    panel.energyAuthorizationAccess=energyRuntimeAuthorization(*runtime);
                }else{runtime.reset();panel.message=i18n::settingsDiagnosticEnglish(i18n::SettingsDiagnosticMessageKey::EnergyHelper);}
            }
        }
        if(runtime){const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();runtime->tick(utc,0);}
        panel.updateCreditReview();panel.updateEnergyReview();panel.updateEnergyAuthorization();panel.updateBillingReview();
        const auto color=panel.active.color?detectColorLevel():ColorLevel::None;
        printSettingsFrame(initial?"Preferences PENDING - NOT Saved\nInitial idle inspection\nNo mining is started\nQ/^C Stop; Esc Close\n":preferences.overlay()?preferences.render(terminalColumns(80),terminalRows(24),color):panel.render(terminalColumns(80),terminalRows(24),color));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    }catch(...){result=3;}
    if(!intent.permits(token)&&!quit&&!(stop&&*stop))result=3;
    intent.stop();owner->requestStop();panel.stopBillingReview();
    if(runtime){runtime->requestStop();runtime->stop();}
    owner->stop(); // optional join after revocation; no mining worker exists in this command
    printf("\x1b[2J\x1b[H\x1b[?25h%s\n",i18n::settingsDiagnosticEnglish(i18n::SettingsDiagnosticMessageKey::Closed).c_str());
    if(result)fprintf(stderr,"Idle preference inspection refused; no synchronous fallback or automatic retry. An issued write may be UNKNOWN.\n");
    return stop&&*stop?0:result;
}
} // namespace om
