// Restricted idle diagnostic arguments. No configuration, paths or raw arguments enter a report or error.
#pragma once
#include "diagnostic_report.h"
#include "local_export_name.h"
namespace om {
struct DiagnosticOptions {
    DiagnosticSelection selection;
    bool help=false;
    std::string exportName,confirmation;
};
inline bool diagnosticDigest(const std::string &value){
    if(value.size()!=64)return false;
    for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline bool diagnosticExportName(const std::string &value){return localExportName(value,".json");}
inline bool parseDiagnosticCli(int argc,const char *const *argv,int begin,DiagnosticOptions *out,std::string *error){
    const auto fail=[&](){if(error)*error="diagnostics: invalid options; use diagnostics --help. No export or mining was started.";return false;};
    if(!out||!argv||begin<0||argc<begin)return fail();
    DiagnosticOptions options;bool preview=false,build=false,exported=false,confirmed=false;
    if(argc-begin==1&&argv[begin]&&(std::string(argv[begin])=="--help"||std::string(argv[begin])=="-h")){
        options.help=true;*out=std::move(options);return true;
    }
    for(int i=begin;i<argc;++i){
        if(!argv[i])return fail();
        const std::string arg=argv[i];
        if(arg=="--preview"&&!preview)preview=true;
        else if(arg=="--include-build"&&!build){build=true;options.selection.build=true;}
        else if(arg=="--export"&&!exported){
            if(i+1>=argc||!argv[i+1])return fail();
            exported=true;options.exportName=argv[++i];
        }else if(arg=="--confirm"&&!confirmed){
            if(i+1>=argc||!argv[i+1])return fail();
            confirmed=true;options.confirmation=argv[++i];
        }else return fail();
    }
    if(preview&&exported)return fail();
    if(exported!=confirmed||(exported&&(!diagnosticExportName(options.exportName)||!diagnosticDigest(options.confirmation))))return fail();
    *out=std::move(options);return true;
}
inline const char *diagnosticHelpText(){
    return "canary-miner diagnostics [--preview] [--include-build]\n"
           "canary-miner diagnostics [--include-build] --export NAME.json --confirm SHA256\n"
           "Default: preview only; all optional groups excluded. --include-build selects public build facts.\n"
           "Preview stdout is the exact JSON; stderr names its SHA-256 (not a signature or proof of review).\n"
           "Review the preview, then explicitly select the same groups, a new basename and its exact digest.\n"
           "Export creates a new file in the operator-selected working directory; never overwrites or uploads.\n"
           "No identity, configuration, runtime, network, log, financial or game-activity data is read.\n"
           "Filesystem/console calls are synchronous in this idle command; no hard latency or durability claim.\n"
           "A failed/cancelled creation may leave a partial file: inspect it yourself; no deletion or repair.\n";
}
} // namespace om
