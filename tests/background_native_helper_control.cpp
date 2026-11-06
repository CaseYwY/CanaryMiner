// Test-only native parent: Windows local-task requests use inherited mappings, not POSIX stdin fixtures.
#include "product/local_task_process.h"
#include <cstdio>
#include <filesystem>
int main(int argc,char **argv){
    if(argc!=3)return 4;
    const std::string kind=argv[1];if(kind!="game"&&kind!="sampling")return 4;
    const std::string expected=kind=="game"?"SYNTHETIC IDENTITY FIXTURE ONLY\n":"SYNTHETIC SAMPLING FIXTURE ONLY\n";
    om::LocalTaskProcess process(std::filesystem::absolute(argv[2]));std::atomic<bool> cancel{false};
    const auto result=process.run("SYNTHETIC\n",cancel,2000);
    if(result.status!=om::LocalTaskStatus::Ok||!result.childStarted||result.response!=expected){std::fputs("synthetic native helper control failed\n",stderr);return 1;}
    return om::writeLocalTaskOutput(expected)?0:3;
}
