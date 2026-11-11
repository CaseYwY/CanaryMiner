// Explicit new-file-only build diagnostic export. Idle command only, never a live control callback.
#pragma once
#include "diagnostic_cli.h"
#include "../core/canary_version.h"
#include <csignal>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace om {
enum class DiagnosticExportResult { Written, Refused, NotConfirmed, Stopped, Unknown };
inline DiagnosticExportResult exportDiagnostic(const DiagnosticPreview &preview,const std::string &name,const std::string &confirmation,
                                               const volatile sig_atomic_t *stop=nullptr){
    if(!preview.valid()||!diagnosticExportName(name))return DiagnosticExportResult::Refused;
    if(!diagnosticDigest(confirmation)||confirmation!=preview.digest())return DiagnosticExportResult::NotConfirmed;
    if(stop&&*stop)return DiagnosticExportResult::Stopped;
    // No replace/append/retry/remove path. Any uncertainty after creation preserves the possible original.
#ifdef _WIN32
    const std::wstring wide(name.begin(),name.end()); // already restricted to a portable ASCII basename
    HANDLE file=CreateFileW(wide.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){
        const auto error=GetLastError();
        return error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS?DiagnosticExportResult::Refused:DiagnosticExportResult::Unknown;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    bool ok=GetFileType(file)==FILE_TYPE_DISK&&GetFileInformationByHandle(file,&info)&&
            !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    if(stop&&*stop)ok=false;
    DWORD written=0;
    if(ok)ok=WriteFile(file,preview.bytes().data(),static_cast<DWORD>(preview.bytes().size()),&written,nullptr)&&written==preview.bytes().size();
    if(ok)ok=FlushFileBuffers(file)!=0;
    if(stop&&*stop)ok=false;
    if(!CloseHandle(file))ok=false;
#else
    const int directory=open(".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(directory<0)return DiagnosticExportResult::Refused;
    if(stop&&*stop){close(directory);return DiagnosticExportResult::Stopped;}
    const int file=openat(directory,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(file<0){const int error=errno;close(directory);return error==EEXIST?DiagnosticExportResult::Refused:DiagnosticExportResult::Unknown;}
    struct stat info{};
    bool ok=fstat(file,&info)==0&&S_ISREG(info.st_mode);
    if(stop&&*stop)ok=false;
    size_t offset=0;
    while(ok&&offset<preview.bytes().size()){
        if(stop&&*stop){ok=false;break;}
        const auto count=write(file,preview.bytes().data()+offset,preview.bytes().size()-offset);
        if(count<=0)ok=false;else offset+=static_cast<size_t>(count);
    }
    if(ok)ok=fsync(file)==0;
    if(stop&&*stop)ok=false;
    if(close(file)!=0)ok=false; // never retry an ambiguous close on a possibly reused descriptor
    if(ok)ok=fsync(directory)==0;
    if(close(directory)!=0)ok=false;
#endif
    if(stop&&*stop)ok=false;
    return ok?DiagnosticExportResult::Written:DiagnosticExportResult::Unknown;
}
inline DiagnosticBuild currentDiagnosticBuild(){
    DiagnosticBuild build;
#if defined(_WIN32)
    build.platform=DiagnosticPlatform::Windows;
#elif defined(__linux__)
    build.platform=DiagnosticPlatform::Linux;
#endif
#if defined(CANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE)
    build.architecture=DiagnosticArchitecture::Sm120;
#endif
#if defined(__clang__)
    build.compiler=DiagnosticCompiler::Clang;
#elif defined(_MSC_VER)
    build.compiler=DiagnosticCompiler::Msvc;
#elif defined(__GNUC__)
    build.compiler=DiagnosticCompiler::Gcc;
#endif
    build.publicVersion=CANARY_MINER_VERSION;return build;
}
inline int commandDiagnostics(const DiagnosticOptions &options,const volatile sig_atomic_t *stop){
    if(options.help){std::fputs(diagnosticHelpText(),stdout);return 0;}
    DiagnosticPreview preview;
    if(!makeDiagnosticPreview(options.selection,currentDiagnosticBuild(),&preview)){
        std::fputs("diagnostics: preview unavailable; no export or mining was started.\n",stderr);return 1;
    }
    if(!options.exportName.empty()){
        const auto result=exportDiagnostic(preview,options.exportName,options.confirmation,stop);
        if(result==DiagnosticExportResult::Written){std::fprintf(stderr,"diagnostics: new build-only file written; SHA256 %s. No upload.\n",preview.digest().c_str());return 0;}
        if(result==DiagnosticExportResult::Unknown)std::fputs("diagnostics: export outcome unknown; inspect the possible partial new file. No deletion, repair or retry.\n",stderr);
        else std::fputs("diagnostics: export refused or stopped; no existing file was overwritten.\n",stderr);
        return result==DiagnosticExportResult::NotConfirmed?2:1;
    }
    if(stop&&*stop){std::fputs("diagnostics: preview stopped.\n",stderr);return 1;}
#ifdef _WIN32
    if(_setmode(_fileno(stdout),_O_BINARY)==-1){std::fputs("diagnostics: output unavailable.\n",stderr);return 1;}
#endif
    if(std::fwrite(preview.bytes().data(),1,preview.bytes().size(),stdout)!=preview.bytes().size()||std::fflush(stdout)!=0)return 1;
    std::fprintf(stderr,"diagnostics: build-only preview; SHA256 %s. Excluded groups remain private; no file or upload.\n",preview.digest().c_str());
    return 0;
}
} // namespace om
