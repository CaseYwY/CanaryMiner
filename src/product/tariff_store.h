#pragma once
#include "tariff_declaration.h"
#include "runtime_preferences.h"
namespace om {
class TariffDeclarationStore {
   public:
    explicit TariffDeclarationStore(std::filesystem::path path):path_(std::move(path)){}
    bool load(JsonValue *out,std::string *error){
        loaded_=false;if(!out)return fail(error,"Tariff output unavailable.");
        std::string text;bool exists=false;if(!read(&text,&exists,error))return false;
        JsonValue value=tariffDeclarationTemplate();TariffProblem problem;
        if(exists&&!parseTariffDeclaration(text,&value,&problem))return fail(error,problem.path+": "+problem.message);
        original_=std::move(text);existed_=exists;baseline_=value;loaded_=true;*out=std::move(value);return true;
    }
    bool save(const JsonValue &value,std::string *error){
        if(!loaded_)return fail(error,"Tariff target must be successfully reopened before saving.");
        TariffDeclaration declaration;TariffProblem problem;
        if(!decodeTariffDeclaration(value,&declaration,&problem))return fail(error,problem.path+": "+problem.message);
        if(existed_){
            const auto *oldPlan=baseline_.find("plan"),*newPlan=value.find("plan");
            const auto identity=[](const JsonValue &v){return tariffJson(*v.find("plan"))+tariffJson(*v.find("country"))+tariffJson(*v.find("source_reference"))+tariffJson(*v.find("reset_policy"));};
            if(oldPlan->find("key")->text==newPlan->find("key")->text&&identity(baseline_)!=identity(value))
                return fail(error,"Declaration changed: choose a NEW plan.key. Rules/identity/source/clock cannot reuse the loaded version.");
            if(baseline_.find("opening")->find("key")->text==value.find("opening")->find("key")->text&&
               (tariffJson(*baseline_.find("opening"))!=tariffJson(*value.find("opening"))||identity(baseline_)!=identity(value)))
                return fail(error,"Opening or its plan binding changed: choose a NEW opening.key; this is not a ledger reset.");
        }
        std::error_code ec;if(path_.has_parent_path())std::filesystem::create_directories(path_.parent_path(),ec);
        if(ec)return fail(error,"Cannot create tariff declaration directory.");
        auto lock=path_;lock+=".lock";RuntimePreferencesLease lease;
        if(!lease.acquire(lock))return fail(error,"Tariff declaration is locked or lock file is invalid.");
        std::string text;bool exists=false;if(!read(&text,&exists,error))return false;
        if(exists!=existed_||text!=original_)return fail(error,"Tariff declaration changed externally; reopen before saving.");
        static std::atomic<unsigned> serial{0};auto temporary=path_;
#if defined(_WIN32)
        temporary+=".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
        int fd=-1;if(_wsopen_s(&fd,temporary.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0)return fail(error,"Cannot create temporary tariff declaration.");
        FILE *f=_fdopen(fd,"wb");
#else
        temporary+=".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
        const int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0)return fail(error,"Cannot create temporary tariff declaration.");
        FILE *f=fdopen(fd,"wb");
#endif
        if(!f){
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temporary,ec);return fail(error,"Cannot open temporary tariff declaration.");
        }
        const auto encoded=tariffJson(value)+"\n";bool ok=fwrite(encoded.data(),1,encoded.size(),f)==encoded.size()&&fflush(f)==0;
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
        if(!ok){std::filesystem::remove(temporary,ec);return fail(error,"Cannot replace tariff declaration; draft retained.");}
#if !defined(_WIN32)
        const auto parent=path_.has_parent_path()?path_.parent_path():std::filesystem::path(".");
        const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        bool synced=directory>=0;if(directory>=0){synced=fsync(directory)==0;::close(directory);}
        if(!synced){loaded_=false;return fail(error,"Replacement may be saved; directory sync failed. Reopen and review, do not assume rollback.");}
#endif
        original_=encoded;existed_=true;baseline_=value;return true;
    }
   private:
    std::filesystem::path path_;std::string original_;JsonValue baseline_;bool loaded_=false,existed_=false;
    static bool fail(std::string *error,const std::string &s){if(error)*error=s;return false;}
    bool read(std::string *text,bool *exists,std::string *error)const{
        std::error_code ec;const auto status=std::filesystem::symlink_status(path_,ec);
        if(ec==std::errc::no_such_file_or_directory||(!ec&&status.type()==std::filesystem::file_type::not_found)){text->clear();*exists=false;return true;}
        if(ec||!std::filesystem::is_regular_file(status))return fail(error,"Tariff declaration must be a regular file; preserved.");
        std::ifstream f(path_,std::ios::binary);if(!f)return fail(error,"Cannot read tariff declaration; preserved.");
        std::string data(tariffDeclarationMaxBytes+1,'\0');f.read(&data[0],static_cast<std::streamsize>(data.size()));
        if(f.bad()||static_cast<size_t>(f.gcount())>tariffDeclarationMaxBytes)return fail(error,"Tariff declaration oversized/unreadable; preserved.");
        data.resize(static_cast<size_t>(f.gcount()));*text=std::move(data);*exists=true;return true;
    }
};
} // namespace om
