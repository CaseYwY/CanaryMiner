// Finite original-parent config inspection. Only the owned helper thread performs file/native transport work.
#pragma once
#include "config.h"
#include "startup_config_protocol.h"
#include "duplex_task_process.h"
#include "executable_path.h"
#include "session_intent.h"
#include "dashboard.h"
#include "settings_diagnostic_messages.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

namespace om {
enum class StartupConfigStatus { Ready, Stopped, Refused, RequiresTerminal };
struct StartupConfig {ProductConfig config;uint64_t nativeEpoch=0;};
namespace startup_config_detail {
inline bool read(const std::filesystem::path &helper,const StartupConfigRequest &request,const std::atomic<bool> &cancel,
                 uint64_t epoch,std::chrono::steady_clock::time_point admitted,ProductConfig *config,std::string *error){
    NativeProcessScope original(epoch);
    const auto current=[&]{return !cancel.load()&&nativeProcessEpochCurrent(epoch)&&nativeProcessAdmissionEpoch()==epoch&&
        std::chrono::steady_clock::now()-admitted<std::chrono::seconds(10);};
    std::string outgoing;
    if(!config||!error||!current()||!encodeStartupConfigRequest(request,&outgoing))return false;
    DuplexTaskProcess process;
    if(!process.start(helper,cancel)||!current()||!process.send(outgoing))return false;
    std::string wire;StartupConfigHeader header;size_t body=0;
    bool received=false;
    while(current()){
        const auto view=process.poll();
        if(view.channelFault||view.bytes.size()>startupConfigDocumentLimit+startupConfigHeaderLimit-wire.size())break;
        wire+=view.bytes;
        if(!received){
            const auto newline=wire.find('\n');
            if(newline!=wire.npos){
                body=newline+1;
                if(body>startupConfigHeaderLimit||!decodeStartupConfigHeader(wire.substr(0,body),&header)||header.ticket!=request.ticket)break;
                received=true;
            }else if(wire.size()>startupConfigHeaderLimit)break;
        }
        if(received&&wire.size()>body+header.bytes)break;
        if(view.state==DuplexTaskState::Exited&&view.exitCode!=0)break;
        if(view.state==DuplexTaskState::Failed||view.state==DuplexTaskState::Stopped||view.state==DuplexTaskState::Unreaped)break;
        if(view.eof){
            if(!received||wire.size()!=body+header.bytes)break;
            if(view.state==DuplexTaskState::Exited){
                if(!process.terminate(250)||!current())return false;
                if(header.status==StartupConfigReadStatus::Unavailable){*error="cannot read config file; original preserved";return false;}
                if(header.status==StartupConfigReadStatus::Oversized){*error="config exceeds size limit; original preserved";return false;}
                ProductConfig parsed;std::string problem;
                if(!parseConfig(wire.substr(body),&parsed,&problem)){*error="config file: "+problem;return false;}
                if(!current())return false;
                *config=std::move(parsed);return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    process.terminate(250);return false;
}
} // namespace startup_config_detail
inline StartupConfigStatus inspectStartupConfig(const std::string &path,volatile sig_atomic_t *stop,bool requiresTerminal,
                                                StartupConfig *out,std::string *error=nullptr){
#if defined(_WIN32)
    const bool terminal=_isatty(_fileno(stdin))&&_isatty(_fileno(stdout));
#else
    const bool terminal=isatty(STDIN_FILENO)&&isatty(STDOUT_FILENO);
#endif
    if(requiresTerminal&&!terminal){if(error)*error=i18n::settingsDiagnosticEnglish(i18n::SettingsDiagnosticMessageKey::RequiresTerminal);return StartupConfigStatus::RequiresTerminal;}
    if(stop&&*stop)return StartupConfigStatus::Stopped;
    const auto refuse=[&](const std::string &message){if(error)*error=message;return StartupConfigStatus::Refused;};
    if(!out)return refuse("Startup configuration unavailable; no synchronous fallback or mining was started.");
    SessionIntent intent;const auto token=intent.explicitStart();
    if(!intent.permits(token))return refuse("Startup configuration unavailable; original native admission was lost.");
    StartupConfig next;next.nativeEpoch=nativeProcessAdmissionEpoch();NativeProcessScope original(next.nativeEpoch);
    if(path.empty()){
        const bool current=intent.permits(token);intent.stop();
        if(stop&&*stop)return StartupConfigStatus::Stopped;
        if(!current)return refuse("Startup configuration unavailable; original native admission was lost.");
        *out=std::move(next);return StartupConfigStatus::Ready; // defaults require no module, file, helper or terminal key setup
    }
    const auto admitted=std::chrono::steady_clock::now();
    std::atomic<bool> cancel{false},done{false};std::thread thread;
    std::unique_ptr<KeyReader> keys;std::string failure;bool ready=false,quit=false;
    try{
        if(terminal)keys=std::make_unique<KeyReader>();
        thread=std::thread([&]{
            NativeProcessScope background(next.nativeEpoch);
            try{
                const auto module=selfExecutablePath();
                if(!module.empty()){
                    const auto helper=std::filesystem::u8path(module).parent_path()/
#if defined(_WIN32)
                        "canary_preferences_worker.exe";
#else
                        "canary_preferences_worker";
#endif
                    // Preserve '..' through possible symlinks: absolute anchoring is not lexical/canonical collapse.
                    StartupConfigRequest request;request.ticket=1;request.target=std::filesystem::absolute(std::filesystem::u8path(path)).u8string();
                    ready=startup_config_detail::read(helper,request,cancel,next.nativeEpoch,admitted,&next.config,&failure);
                }
            }catch(...){ready=false;failure.clear();}
            done.store(true,std::memory_order_release); // all helper ownership/destruction and parsing precede publication
        });
        if(terminal)fprintf(stderr,"Startup configuration PENDING; no mining started. Q/^C/Esc Stop.\n");
        while(!done.load(std::memory_order_acquire)){
            if(stop&&*stop)break;
            if(keys)for(int n=0;n<32;++n){const auto key=keys->poll();if(!key)break;if(key==3||key==27||key=='q'||key=='Q'){quit=true;break;}}
            if(quit||!intent.permits(token))break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }catch(...){cancel=true;}
    cancel=true;
    if(thread.joinable())thread.join();
    const bool current=intent.permits(token);intent.stop();
    if(quit||(stop&&*stop))return StartupConfigStatus::Stopped;
    if(!current||!ready)return refuse(current&&!failure.empty()?failure:"Startup configuration unavailable; no synchronous fallback or mining was started.");
    *out=std::move(next);return StartupConfigStatus::Ready;
}
} // namespace om
