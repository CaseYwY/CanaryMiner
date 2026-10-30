// Private next-parent game defaults. Parsing/review never starts detection, a game or mining.
#pragma once
#include "game_detection_owner.h"
#include "runtime_preferences.h"
#include "../core/http.h"
namespace om {
struct GamePreferences {
    GameIntent mode=GameIntent::Off;
    std::string executable;
#if defined(_WIN32)
    GameProcessAdapter adapter=GameProcessAdapter::WindowsSessionImage;
#else
    GameProcessAdapter adapter=GameProcessAdapter::LinuxUidNativeImage;
#endif
    unsigned pollMs=500,exitDebounceMs=2000,maximumDetectionAgeMs=2000;
    bool operator==(const GamePreferences &v)const{return mode==v.mode&&executable==v.executable&&adapter==v.adapter&&
        pollMs==v.pollMs&&exitDebounceMs==v.exitDebounceMs&&maximumDetectionAgeMs==v.maximumDetectionAgeMs;}
    bool operator!=(const GamePreferences &v)const{return !(*this==v);}
};
inline const char *gameModeName(GameIntent v){return v==GameIntent::Off?"off":v==GameIntent::Pause?"pause":v==GameIntent::Coexist?"coexist":"invalid";}
inline const char *gameScopeName(GameProcessAdapter v){return v==GameProcessAdapter::WindowsSessionImage?"windows_session_image":
    v==GameProcessAdapter::LinuxUidNativeImage?"linux_native_uid_image":"invalid";}
inline bool validGamePreferences(const GamePreferences &v,std::string *error=nullptr,unsigned *field=nullptr){
    auto fail=[&](unsigned at,const char *s){if(error)*error=s;if(field)*field=at;return false;};
    if(v.mode!=GameIntent::Off&&v.mode!=GameIntent::Pause&&v.mode!=GameIntent::Coexist)return fail(0,"Choose off, pause or coexist.");
    if((v.executable.empty()&&v.mode!=GameIntent::Off)||(!v.executable.empty()&&!validGameProcessName(v.executable)))
        return fail(1,"Target must be an exact ASCII basename (letters, digits, dot, dash, underscore; 1..128). No path or pattern.");
    if(v.adapter!=GameProcessAdapter::WindowsSessionImage&&v.adapter!=GameProcessAdapter::LinuxUidNativeImage)return fail(2,"Choose an explicit Windows-session or Linux-native-UID scope.");
    if(v.pollMs<100||v.pollMs>10000)return fail(3,"Detection poll must be 100..10000 ms.");
    if(v.exitDebounceMs>30000)return fail(4,"Exit debounce must be 0..30000 ms.");
    if(v.maximumDetectionAgeMs<100||v.maximumDetectionAgeMs>30000)return fail(5,"Maximum detection age must be 100..30000 ms.");
    return true;
}
inline GameProcessTarget gamePreferencesTarget(const GamePreferences &v){return {"selected.game",v.executable,v.adapter};}
inline GameDetectionPolicy gamePreferencesDetection(const GamePreferences &v){GameDetectionPolicy p;p.enabled=v.mode!=GameIntent::Off;p.pollMs=v.pollMs;return p;}
inline GameGuardPolicy gamePreferencesGuard(const GamePreferences &v){GameGuardPolicy p;p.mode=v.mode;p.exitDebounceMs=v.exitDebounceMs;p.maximumDetectionGapMs=v.maximumDetectionAgeMs;return p;}
inline std::string gamePreferencesJson(const GamePreferences &v){
    return "{\n  \"schema_version\": 1,\n  \"mode\": "+jsonQuoted(gameModeName(v.mode))+",\n  \"target_executable\": "+jsonQuoted(v.executable)+
        ",\n  \"target_scope\": "+jsonQuoted(gameScopeName(v.adapter))+",\n  \"poll_ms\": "+std::to_string(v.pollMs)+
        ",\n  \"exit_debounce_ms\": "+std::to_string(v.exitDebounceMs)+",\n  \"maximum_detection_age_ms\": "+std::to_string(v.maximumDetectionAgeMs)+"\n}\n";
}
inline bool parseGamePreferences(const std::string &text,GamePreferences *out,std::string *error){
    auto fail=[&](const char *s){if(error)*error=s;return false;};JsonValue root;uint64_t version=0;
    if(!out||text.size()>4096||!parseJsonStrict(text,&root,nullptr)||!root.isObject()||root.members.size()!=7)return fail("Invalid game preferences document; no values applied.");
    const auto *schema=root.find("schema_version"),*mode=root.find("mode"),*target=root.find("target_executable"),*scope=root.find("target_scope");
    const auto *poll=root.find("poll_ms"),*debounce=root.find("exit_debounce_ms"),*age=root.find("maximum_detection_age_ms");
    if(!schema||!schema->asUint64(&version)||version!=1)return fail("Unsupported game preferences schema; preserved.");
    if(!mode||!mode->isString()||!target||!target->isString()||!scope||!scope->isString()||!poll||!debounce||!age)return fail("Missing or invalid game preference field.");
    GamePreferences v;
    if(mode->text=="off")v.mode=GameIntent::Off;else if(mode->text=="pause")v.mode=GameIntent::Pause;
    else if(mode->text=="coexist")v.mode=GameIntent::Coexist;else return fail("Unsupported game mode.");
    if(scope->text=="windows_session_image")v.adapter=GameProcessAdapter::WindowsSessionImage;
    else if(scope->text=="linux_native_uid_image")v.adapter=GameProcessAdapter::LinuxUidNativeImage;else return fail("Unsupported game detection scope; no platform fallback.");
    uint64_t p=0,d=0,a=0;
    if(!poll->asUint64(&p)||p>10000||!debounce->asUint64(&d)||d>30000||!age->asUint64(&a)||a>30000)return fail("Invalid game timing type or range.");
    v.executable=target->text;v.pollMs=static_cast<unsigned>(p);v.exitDebounceMs=static_cast<unsigned>(d);v.maximumDetectionAgeMs=static_cast<unsigned>(a);
    if(!validGamePreferences(v,error))return false;
    *out=std::move(v);return true;
}
class GamePreferencesStore {
   public:
    explicit GamePreferencesStore(std::filesystem::path path):path_(std::move(path)){}
    bool load(GamePreferences *out,std::string *error){
        loaded_=false;if(!out)return fail(error,"Game preferences output unavailable.");
        std::string text;bool exists=false;if(!read(&text,&exists,error))return false;
        GamePreferences value;if(exists&&!parseGamePreferences(text,&value,error))return false;
        original_=std::move(text);existed_=exists;loaded_=true;*out=std::move(value);return true;
    }
    bool save(const GamePreferences &v,std::string *error){
        if(!loaded_)return fail(error,"Game defaults must be successfully reopened before saving.");
        if(!validGamePreferences(v,error))return false;
        std::error_code ec;if(path_.has_parent_path())std::filesystem::create_directories(path_.parent_path(),ec);
        if(ec)return fail(error,"Cannot create game preferences directory.");
        auto lock=path_;lock+=".lock";RuntimePreferencesLease lease;
        if(!lease.acquire(lock))return fail(error,"Game defaults locked or invalid lock file.");
        std::string text;bool exists=false;if(!read(&text,&exists,error))return false;
        if(exists!=existed_||text!=original_)return fail(error,"Game defaults changed externally; reopen before saving.");
        static std::atomic<unsigned> serial{0};auto temporary=path_;
#if defined(_WIN32)
        temporary+=".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
        int fd=-1;if(_wsopen_s(&fd,temporary.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0)return fail(error,"Cannot create temporary game defaults.");
        FILE *f=_fdopen(fd,"wb");
#else
        temporary+=".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
        const int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0)return fail(error,"Cannot create temporary game defaults.");
        FILE *f=fdopen(fd,"wb");
#endif
        if(!f){
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temporary,ec);return fail(error,"Cannot open temporary game defaults.");
        }
        const auto encoded=gamePreferencesJson(v);bool ok=fwrite(encoded.data(),1,encoded.size(),f)==encoded.size()&&fflush(f)==0;
#if defined(_WIN32)
        ok=(_commit(fd)==0)&&ok;
#else
        ok=(fsync(fd)==0)&&ok;
#endif
        ok=(fclose(f)==0)&&ok;
        if(ok){
#if defined(_WIN32)
            ok=MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
            std::filesystem::rename(temporary,path_,ec);ok=!ec;
#endif
        }
        if(!ok){std::filesystem::remove(temporary,ec);return fail(error,"Cannot replace game defaults; draft retained.");}
#if !defined(_WIN32)
        const auto parent=path_.has_parent_path()?path_.parent_path():std::filesystem::path(".");
        const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);bool synced=directory>=0;
        if(directory>=0){synced=fsync(directory)==0;::close(directory);}
        if(!synced){loaded_=false;return fail(error,"Game defaults may be saved; directory sync failed. Reopen and review.");}
#endif
        original_=encoded;existed_=true;return true;
    }
   private:
    std::filesystem::path path_;std::string original_;bool loaded_=false,existed_=false;
    static bool fail(std::string *error,const char *s){if(error)*error=s;return false;}
    bool read(std::string *text,bool *exists,std::string *error)const{
        std::error_code ec;const auto status=std::filesystem::symlink_status(path_,ec);
        if(ec==std::errc::no_such_file_or_directory||(!ec&&status.type()==std::filesystem::file_type::not_found)){text->clear();*exists=false;return true;}
        if(ec||!std::filesystem::is_regular_file(status))return fail(error,"Game defaults must be a readable regular file; preserved.");
        std::ifstream f(path_,std::ios::binary);if(!f)return fail(error,"Cannot read game defaults; preserved.");
        char data[4097];f.read(data,sizeof(data));if(f.bad()||f.gcount()>4096)return fail(error,"Game defaults oversized/unreadable; preserved.");
        text->assign(data,static_cast<size_t>(f.gcount()));*exists=true;return true;
    }
};
} // namespace om
