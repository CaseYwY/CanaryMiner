// Closed build-facts-only diagnostic preview. Never accepts logs, configuration, paths or runtime data.
#pragma once
#include "hash.h"
#include <utility>
namespace om {
enum class DiagnosticPlatform { Windows, Linux, Other };
enum class DiagnosticArchitecture { HostOnly, Sm120 };
enum class DiagnosticCompiler { Msvc, Gcc, Clang, Unknown };
struct DiagnosticBuild {
    DiagnosticPlatform platform=DiagnosticPlatform::Other;
    DiagnosticArchitecture architecture=DiagnosticArchitecture::HostOnly;
    DiagnosticCompiler compiler=DiagnosticCompiler::Unknown;
    std::string publicVersion; // only canonical numeric x.y.z is publishable; arbitrary build labels are never echoed
};
struct DiagnosticSelection {bool build=false;}; // no persistent consent; every new preview starts with all optional groups excluded
class DiagnosticPreview {
  public:
    const std::string &bytes()const{return bytes_;}
    const std::string &digest()const{return digest_;} // exact canonical report bytes, not proof of human review or a signature
    bool valid()const{return !bytes_.empty();}
  private:
    std::string bytes_,digest_;
    friend bool makeDiagnosticPreview(DiagnosticSelection,const DiagnosticBuild &,DiagnosticPreview *);
};
inline bool diagnosticPublicVersion(const std::string &value){
    if(value.size()<5||value.size()>11)return false;
    unsigned groups=0;size_t begin=0;
    for(size_t i=0;i<=value.size();++i){
        if(i==value.size()||value[i]=='.'){
            if(i==begin||i-begin>3||(i-begin>1&&value[begin]=='0'))return false;
            ++groups;begin=i+1;
        }else if(value[i]<'0'||value[i]>'9')return false;
    }
    return groups==3;
}
inline bool makeDiagnosticPreview(DiagnosticSelection selection,const DiagnosticBuild &build,DiagnosticPreview *out){
    if(!out)return false;
    std::string bytes="{\"schema\":\"canaryminer-diagnostics-v1\",\"scope\":\"build-only\",\"selection\":{\"build\":";
    bytes+=selection.build?"true}":"false}";
    if(selection.build){
        const char *platform=nullptr,*architecture=nullptr,*compiler=nullptr;
        switch(build.platform){case DiagnosticPlatform::Windows:platform="windows";break;case DiagnosticPlatform::Linux:platform="linux";break;case DiagnosticPlatform::Other:platform="other";break;default:return false;}
        switch(build.architecture){case DiagnosticArchitecture::HostOnly:architecture="host-only";break;case DiagnosticArchitecture::Sm120:architecture="sm120";break;default:return false;}
        switch(build.compiler){case DiagnosticCompiler::Msvc:compiler="msvc";break;case DiagnosticCompiler::Gcc:compiler="gcc";break;case DiagnosticCompiler::Clang:compiler="clang";break;case DiagnosticCompiler::Unknown:compiler="unknown";break;default:return false;}
        bytes+=",\"build\":{\"platform\":\"";bytes+=platform;bytes+="\",\"architecture\":\"";bytes+=architecture;bytes+="\",\"compiler\":\"";bytes+=compiler;bytes+="\",\"version\":";
        if(diagnosticPublicVersion(build.publicVersion)){bytes+='"';bytes+=build.publicVersion;bytes+='"';}else bytes+="null";
        bytes+="}";
    }
    bytes+=",\"excluded\":[\"identity\",\"configuration\",\"runtime\",\"network\",\"logs\",\"financial\",\"game_activity\"],\"qualification\":\"build facts only; not runtime, hardware or release qualification\"}\n";
    if(bytes.size()>1024)return false;
    DiagnosticPreview preview;preview.digest_=sha256Hex(bytes);preview.bytes_=std::move(bytes);*out=std::move(preview);return true;
}
} // namespace om
