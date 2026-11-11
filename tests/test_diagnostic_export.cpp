// Owned scratch only; no real configuration, devices, source accounts, logs, network or miner execution.
#include "product/diagnostic_export.h"
#include "product_test_util.h"
#include <fstream>
#include <thread>
#ifndef _WIN32
#include <sys/resource.h>
#endif
namespace {
struct Scratch {
    std::filesystem::path original=std::filesystem::current_path(),root;
    bool owned=false;
    Scratch(){
        root=std::filesystem::temp_directory_path()/("canary-diagnostic-export-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code error;owned=std::filesystem::create_directory(root,error);
        if(owned)std::filesystem::current_path(root);
    }
    ~Scratch(){if(owned){std::filesystem::current_path(original);std::error_code error;std::filesystem::remove_all(root,error);}}
};
std::string read(const std::string &name){std::ifstream stream(name,std::ios::binary);return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
}
int main(){using namespace om;namespace fs=std::filesystem;Scratch scratch;CHECK(scratch.owned);if(!scratch.owned)return finishTests("diagnostic scratch refusal");
    DiagnosticBuild build;build.publicVersion="1.0.3";DiagnosticPreview preview,selected,empty;
    CHECK(makeDiagnosticPreview({},build,&preview));CHECK(makeDiagnosticPreview({true},build,&selected));
    const auto name="report.json";const auto digest=preview.digest();
    CHECK(exportDiagnostic(empty,name,digest)==DiagnosticExportResult::Refused&&!fs::exists(name));
    CHECK(exportDiagnostic(preview,"../outside-fixture.json",digest)==DiagnosticExportResult::Refused);
    CHECK(exportDiagnostic(preview,name,std::string(64,'0'))==DiagnosticExportResult::NotConfirmed&&!fs::exists(name));
    CHECK(exportDiagnostic(preview,name,selected.digest())==DiagnosticExportResult::NotConfirmed&&!fs::exists(name));
    volatile sig_atomic_t stop=1;
    CHECK(exportDiagnostic(preview,name,digest,&stop)==DiagnosticExportResult::Stopped&&!fs::exists(name));stop=0;
    CHECK(exportDiagnostic(preview,name,digest,&stop)==DiagnosticExportResult::Written);CHECK(read(name)==preview.bytes());
    CHECK(exportDiagnostic(selected,name,selected.digest())==DiagnosticExportResult::Refused&&read(name)==preview.bytes());
    std::ofstream("original.json",std::ios::binary)<<"synthetic-original-preserve";
    CHECK(exportDiagnostic(preview,"original.json",digest)==DiagnosticExportResult::Refused&&read("original.json")=="synthetic-original-preserve");
    CHECK(fs::create_directory("directory.json"));
    CHECK(exportDiagnostic(preview,"directory.json",digest)!=DiagnosticExportResult::Written&&fs::is_directory("directory.json"));
    DiagnosticExportResult a=DiagnosticExportResult::Unknown,b=a;
    std::thread first([&](){a=exportDiagnostic(preview,"race.json",digest);});
    std::thread second([&](){b=exportDiagnostic(preview,"race.json",digest);});first.join();second.join();
    CHECK((a==DiagnosticExportResult::Written)!=(b==DiagnosticExportResult::Written));CHECK(read("race.json")==preview.bytes());
    CHECK(exportDiagnostic(selected,"selected.json",selected.digest())==DiagnosticExportResult::Written&&read("selected.json")==selected.bytes());
#ifndef _WIN32
    struct stat info{};CHECK(stat(name,&info)==0&&S_ISREG(info.st_mode)&&(info.st_mode&0777)==0600);
    CHECK(symlink("original.json","link.json")==0);CHECK(exportDiagnostic(preview,"link.json",digest)==DiagnosticExportResult::Refused);
    CHECK(fs::is_symlink("link.json")&&read("original.json")=="synthetic-original-preserve");
    CHECK(symlink("missing-fixture","broken.json")==0);CHECK(exportDiagnostic(preview,"broken.json",digest)==DiagnosticExportResult::Refused&&!fs::exists("missing-fixture"));
    CHECK(mkfifo("fifo.json",0600)==0);CHECK(exportDiagnostic(preview,"fifo.json",digest)==DiagnosticExportResult::Refused);
    CHECK(lstat("fifo.json",&info)==0&&S_ISFIFO(info.st_mode));
    // Actual native write failure in this owned test process only. Restore OS state before reporting any assertion.
    struct rlimit previous{};const bool got=getrlimit(RLIMIT_FSIZE,&previous)==0;
    CHECK(got);if(got){
        const auto handler=std::signal(SIGXFSZ,SIG_IGN);struct rlimit limited=previous;limited.rlim_cur=0;
        const bool installed=handler!=SIG_ERR&&setrlimit(RLIMIT_FSIZE,&limited)==0;
        const auto result=installed?exportDiagnostic(preview,"partial.json",digest):DiagnosticExportResult::Refused;
        const bool restored=!installed||setrlimit(RLIMIT_FSIZE,&previous)==0;
        const bool signalRestored=handler==SIG_ERR||std::signal(SIGXFSZ,handler)!=SIG_ERR;
        CHECK(installed&&restored&&signalRestored);CHECK(result==DiagnosticExportResult::Unknown);
        CHECK(fs::exists("partial.json")&&fs::file_size("partial.json")==0);
        CHECK(exportDiagnostic(preview,"partial.json",digest)==DiagnosticExportResult::Refused&&fs::file_size("partial.json")==0);
    }
#endif
    CHECK(read(name)==preview.bytes()&&read("original.json")=="synthetic-original-preserve");
    return finishTests("native new-file-only diagnostic export, preservation and exact bytes");
}
