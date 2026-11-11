// Finite read-only preference inspection before device/source setup. No synchronous store fallback or launch authority.
#pragma once
#include "executable_path.h"
#include "preferences_owner.h"
#include "runtime_preferences.h"
#include "game_preferences.h"
#include "pool_security_preferences.h"
#include "language_preferences.h"
#include "dashboard.h"
#include "settings_diagnostic_messages.h"
#include <csignal>
#include <chrono>
#include <thread>

namespace om {
enum class PreRunPreferencesMode { IdleLanguage, Ordinary, PoolOnly };
enum class PreRunPreferencesStatus { Ready, Stopped, Refused, RequiresTerminal };
struct PreRunPreferences {
    // Only the selected mode's requested fields are inspected; defaults in other fields grant no authority.
    LanguagePreferences language;
    RuntimePreferences runtime;
    GamePreferences game;
    PoolSecurityPreferences pool;
    bool languageAvailable=true;
    uint64_t nativeEpoch=0; // original admission only; a later cleared fault cannot renew this observation
};
inline PreRunPreferencesStatus inspectPreRunPreferences(const std::string &stateDir,volatile sig_atomic_t *stop,PreRunPreferencesMode mode,PreRunPreferences *out){
#if defined(_WIN32)
    const bool terminal=_isatty(_fileno(stdin))&&_isatty(_fileno(stdout));
#else
    const bool terminal=isatty(STDIN_FILENO)&&isatty(STDOUT_FILENO);
#endif
    if(mode==PreRunPreferencesMode::IdleLanguage&&!terminal){
        fprintf(stderr,"%s\n",i18n::settingsDiagnosticEnglish(i18n::SettingsDiagnosticMessageKey::RequiresTerminal).c_str());
        return PreRunPreferencesStatus::RequiresTerminal;
    }
    if(stop&&*stop)return PreRunPreferencesStatus::Stopped;
    if(!out||(mode!=PreRunPreferencesMode::IdleLanguage&&mode!=PreRunPreferencesMode::Ordinary&&mode!=PreRunPreferencesMode::PoolOnly))return PreRunPreferencesStatus::Refused;
    SessionIntent intent;const auto token=intent.explicitStart();
    if(!intent.permits(token))return PreRunPreferencesStatus::Refused;
    PreRunPreferences next;next.nativeEpoch=nativeProcessAdmissionEpoch();next.languageAvailable=mode!=PreRunPreferencesMode::PoolOnly;
    NativeProcessScope original(next.nativeEpoch);
    std::unique_ptr<PreferencesOwner> owner;
    std::unique_ptr<KeyReader> keys;
    const char *failure="Pre-run preferences unavailable";
    auto status=PreRunPreferencesStatus::Refused;
    bool quit=false;
    try{
        if(terminal)keys=std::make_unique<KeyReader>();
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
        if(owner){
            std::array<PreferencesKind,4> kinds={PreferencesKind::Language,PreferencesKind::Runtime,PreferencesKind::Game,PreferencesKind::Pool};
            if(mode==PreRunPreferencesMode::PoolOnly)kinds[0]=PreferencesKind::Pool;
            const size_t count=mode==PreRunPreferencesMode::Ordinary?4:1;
            size_t phase=0;uint64_t ticket=0;
            if(terminal)fprintf(stderr,"Pre-run preferences PENDING; no mining started. Q/^C/Esc Stop.\n");
            for(;;){
                if(stop&&*stop){status=PreRunPreferencesStatus::Stopped;break;}
                if(keys)for(int n=0;n<32;++n){const auto key=keys->poll();if(!key)break;if(key==3||key==27||key=='q'||key=='Q'){quit=true;break;}}
                if(quit){status=PreRunPreferencesStatus::Stopped;break;}
                if(!intent.permits(token))break;
                const auto view=owner->snapshot();
                if(view.state==PreferencesOwnerState::Faulted||view.state==PreferencesOwnerState::Stopped)break;
                if(!ticket&&view.state==PreferencesOwnerState::Ready){
                    const auto submitted=owner->load(kinds[phase]);
                    if(submitted.status!=PreferencesAdmission::Queued)break;
                    ticket=submitted.ticket;
                }
                const auto reply=ticket?owner->takeReply(ticket):std::shared_ptr<const PreferencesReply>{};
                if(reply){
                    if((stop&&*stop)||!intent.permits(token))break;
                    const auto kind=kinds[phase];bool parsed=false;
                    if(reply->ticket!=ticket||reply->kind!=kind)break;
                    if(reply->status==PreferencesReplyStatus::Rejected&&kind==PreferencesKind::Language){next.languageAvailable=false;parsed=true;}
                    else if(reply->status==PreferencesReplyStatus::Loaded)switch(kind){
                        case PreferencesKind::Language:parsed=parseLanguagePreferences(reply->document,&next.language,nullptr);break;
                        case PreferencesKind::Runtime:parsed=parseRuntimePreferences(reply->document,&next.runtime,nullptr);break;
                        case PreferencesKind::Game:parsed=parseGamePreferences(reply->document,&next.game,nullptr);break;
                        case PreferencesKind::Pool:parsed=parsePoolSecurityPreferences(reply->document,&next.pool,nullptr);break;
                        default:break;
                    }
                    if(!parsed){failure=kind==PreferencesKind::Runtime?"runtime preferences unavailable":kind==PreferencesKind::Game?"game preferences unavailable":kind==PreferencesKind::Pool?"pool security declarations unavailable":"Pre-run preferences unavailable";break;}
                    ticket=0;
                    if(++phase==count){status=PreRunPreferencesStatus::Ready;break;}
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }catch(...){status=PreRunPreferencesStatus::Refused;}
    intent.stop();
    if(owner){owner->requestStop();owner->stop();}
    if(quit||(stop&&*stop))return PreRunPreferencesStatus::Stopped;
    if(!nativeProcessEpochCurrent(next.nativeEpoch)||nativeProcessAdmissionEpoch()!=next.nativeEpoch)status=PreRunPreferencesStatus::Refused;
    if(status==PreRunPreferencesStatus::Ready)*out=std::move(next);
    else if(status==PreRunPreferencesStatus::Refused)fprintf(stderr,"%s; no synchronous fallback or mining was started.\n",failure);
    return status;
}
} // namespace om
