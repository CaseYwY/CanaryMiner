// Bounded private declaration store. Small synchronous saves retain a physical-disk/stop-latency qualification gate.
#pragma once
#include "runtime_preferences.h"
namespace om {
template<class Value,bool (*Parse)(const std::string &,Value *,std::string *),std::string (*Encode)(const Value &),bool (*Valid)(const Value &,std::string *)>
class LocalPreferencesStore {
   public:
    explicit LocalPreferencesStore(std::filesystem::path path,std::size_t limit=8192):path_(std::move(path)),limit_(limit){}
    bool load(Value *out,std::string *error){
        loaded_=false;if(!out||!limit_||limit_>65536)return fail(error,"Preferences output or bound unavailable.");
        std::string text;bool exists=false;if(!read(&text,&exists,error))return false;
        Value value;if(exists&&!Parse(text,&value,error))return false;
        original_=std::move(text);existed_=exists;loaded_=true;*out=std::move(value);return true;
    }
    bool save(const Value &value,std::string *error){
        if(!loaded_)return fail(error,"Successfully reopen these preferences before saving.");
        if(!Valid(value,error))return false;
        const auto encoded=Encode(value);if(encoded.size()>limit_)return fail(error,"Preferences exceed the document bound; not saved.");
        std::error_code ec;if(path_.has_parent_path())std::filesystem::create_directories(path_.parent_path(),ec);
        if(ec)return fail(error,"Cannot create private preferences directory.");
        auto lock=path_;lock+=".lock";RuntimePreferencesLease lease;if(!lease.acquire(lock))return fail(error,"Preferences locked or invalid lock file.");
        std::string text;bool exists=false;if(!read(&text,&exists,error))return false;
        if(exists!=existed_||text!=original_)return fail(error,"Preferences changed externally; reopen before saving.");
        static std::atomic<unsigned> serial{0};auto temporary=path_;
#if defined(_WIN32)
        temporary+=".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
        int fd=-1;if(_wsopen_s(&fd,temporary.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0)return fail(error,"Cannot create private preference temporary.");
        FILE *file=_fdopen(fd,"wb");
#else
        temporary+=".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
        const int fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return fail(error,"Cannot create private preference temporary.");
        FILE *file=fdopen(fd,"wb");
#endif
        if(!file){
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temporary,ec);return fail(error,"Cannot open private preference temporary.");
        }
        bool ok=fwrite(encoded.data(),1,encoded.size(),file)==encoded.size()&&fflush(file)==0;
#if defined(_WIN32)
        ok=(_commit(fd)==0)&&ok;
#else
        ok=(fsync(fd)==0)&&ok;
#endif
        ok=(fclose(file)==0)&&ok;
        if(ok){
#if defined(_WIN32)
            ok=MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
            std::filesystem::rename(temporary,path_,ec);ok=!ec;
#endif
        }
        if(!ok){std::filesystem::remove(temporary,ec);return fail(error,"Cannot replace preferences; draft retained.");}
#if !defined(_WIN32)
        const auto parent=path_.has_parent_path()?path_.parent_path():std::filesystem::path(".");const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        bool synced=directory>=0;if(directory>=0){synced=fsync(directory)==0;::close(directory);}
        if(!synced){loaded_=false;return fail(error,"Preferences may be saved; directory sync failed. Reopen and review.");}
#endif
        original_=encoded;existed_=true;return true;
    }
   private:
    std::filesystem::path path_;std::size_t limit_;std::string original_;bool loaded_=false,existed_=false;
    static bool fail(std::string *error,const char *s){if(error)*error=s;return false;}
    bool read(std::string *text,bool *exists,std::string *error)const{
        std::string bytes(limit_+1,'\0');bool ok=false;std::size_t count=0;
#if defined(_WIN32)
        struct File {HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}} file;
        file.handle=CreateFileW(path_.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file.handle==INVALID_HANDLE_VALUE){const auto reason=GetLastError();if(reason==ERROR_FILE_NOT_FOUND||reason==ERROR_PATH_NOT_FOUND){text->clear();*exists=false;return true;}return fail(error,"Cannot read private preferences; preserved.");}
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};DWORD got=0;
        ok=GetFileType(file.handle)==FILE_TYPE_DISK&&GetFileInformationByHandle(file.handle,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
            GetFileSizeEx(file.handle,&size)&&size.QuadPart>=0&&static_cast<uint64_t>(size.QuadPart)<=limit_&&ReadFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==size.QuadPart;
        count=got;
#else
        struct File {int handle=-1;~File(){if(handle>=0)::close(handle);}} file;
        file.handle=::open(path_.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if(file.handle<0){if(errno==ENOENT){text->clear();*exists=false;return true;}return fail(error,"Cannot read private preferences; preserved.");}
        struct stat info{};
        if(fstat(file.handle,&info)==0&&S_ISREG(info.st_mode)&&info.st_size>=0&&static_cast<uint64_t>(info.st_size)<=limit_){const auto got=::read(file.handle,bytes.data(),bytes.size());ok=got>=0&&got==info.st_size;if(got>=0)count=static_cast<std::size_t>(got);}
#endif
        if(!ok||count>limit_)return fail(error,"Private preferences must be bounded readable regular files; preserved.");
        bytes.resize(count);*text=std::move(bytes);*exists=true;return true;
    }
};
} // namespace om
