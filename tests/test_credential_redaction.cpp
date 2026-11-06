// Synthetic credential-shaped text and owned log/console files only. Never reads a real config, log or account.
#include "product/log_sink.h"
#include "product_test_util.h"
#include <fstream>
#include <sstream>
namespace {
using namespace om;
std::string slurp(const std::string &path){std::ifstream file(path,std::ios::binary);std::ostringstream text;text<<file.rdbuf();return text.str();}
struct Example {const char *input,*expected;};
int quoted(){Redactor redactor;const Example cases[]={
        {"--pass \"fixture secret\" tail","--pass \"<redacted>\" tail"},
        {"--wallet='fixture address' tail","--wallet='<redacted>' tail"},
        {"--user   fixture-user tail","--user   <redacted> tail"},
        {"--api-key\tfixture-key tail","--api-key\t<redacted> tail"},
        {"--password =fixture-value tail","--password <redacted> tail"},
        {"--pass=\"fixture\\\"value\" tail","--pass=\"<redacted>\" tail"},
        {"--pass \"fixture unterminated","--pass \"<redacted>"},
        {"--wallet='fixture unterminated","--wallet='<redacted>"},
        {"{\"password\"\r\n:\n\"fixture secret\"}","{\"password\"\r\n:\n\"<redacted>\"}"},
        {"{\"authorization\":\"fixture unterminated","{\"authorization\":\"<redacted>"},
        {"{\"wallet\":\"fixture escaped\\\" tail","{\"wallet\":\"<redacted>"},
        {"--username fixture-public --password-extra fixture-public","--username fixture-public --password-extra fixture-public"},
        {"--pass plain --wallet=plain","--pass <redacted> --wallet=<redacted>"},
        {"{\"password\":\"fixture\\\"value\"}","{\"password\":\"<redacted>\"}"},
        {"--pass fixture\" secret\" tail","--pass <redacted> tail"},
        {"--pass \"fixture\"-suffix tail","--pass <redacted> tail"},
        {"--pass 'fixture'' secret' tail","--pass <redacted> tail"},
        {"--pass fixture\\ secret tail","--pass <redacted> tail"},
        {"--pass fixture,secret;tail public","--pass <redacted> public"},
        {"--pass \"fixture\"suffix\"more data\" tail","--pass <redacted> tail"},
    };
    const auto directory=makeScratchDir("quoted-credentials");LogSinkConfig config;config.dir=directory;config.maxFiles=1;config.maxFileBytes=8192;
    LogSink sink(config,&redactor);std::string error,expected;CHECK(sink.open(&error));
    for(const auto &item:cases){CHECK(redactor.apply(item.input)==item.expected);CHECK(redactor.apply(redactor.apply(item.input))==redactor.apply(item.input));
        sink.write(item.input);expected+=item.expected;expected+='\n';}
    sink.close();CHECK(slurp(sink.currentPath())==expected);CHECK(std::filesystem::file_size(sink.currentPath())<=config.maxFileBytes);
    std::error_code ec;std::filesystem::remove_all(directory,ec);CHECK(!ec);
    return finishTests("synthetic quoted/whitespace/truncated credentials and actual log sink");
}
int duplicate(int fd){
#if defined(_WIN32)
    return _dup(fd);
#else
    return ::dup(fd);
#endif
}
void restore(int saved,int fd){
#if defined(_WIN32)
    CHECK(_dup2(saved,fd)==0);_close(saved);
#else
    CHECK(::dup2(saved,fd)>=0);::close(saved);
#endif
    clearerr(fd==1?stdout:stderr);
}
int stream(){const auto directory=makeScratchDir("quoted-stream");const auto console=(std::filesystem::path(directory)/"console.txt").string();
    const auto errors=(std::filesystem::path(directory)/"errors.txt").string();
    Redactor redactor;LogSinkConfig config;config.dir=(std::filesystem::path(directory)/"logs").string();config.maxFiles=1;config.maxFileBytes=8192;
    LogSink sink(config,&redactor);std::string error;CHECK(sink.open(&error));fflush(stdout);fflush(stderr);const auto saved=duplicate(1),savedError=duplicate(2);CHECK(saved>=0&&savedError>=0);
    if(saved<0||savedError<0)return finishTests("owned stream capture readiness");
    const bool redirected=freopen(console.c_str(),"wb",stdout)!=nullptr;
    const bool errorRedirected=freopen(errors.c_str(),"wb",stderr)!=nullptr;
    if(!redirected||!errorRedirected){restore(saved,1);restore(savedError,2);CHECK(redirected&&errorRedirected);return finishTests("owned console capture readiness");}
    bool started=false,restarted=false;
    {OutputTee tee(&sink,&redactor);started=tee.start();
        if(started){const auto line=std::string(65530,'v')+"--pass \"fixture-split-secret\"\n";
            CHECK(fwrite(line.data(),1,line.size(),stdout)==line.size());
            const auto json=std::string("{\"password\":\"")+std::string(70000,'s')+"fixture-json-tail\"}\n";
            CHECK(fwrite(json.data(),1,json.size(),stderr)==json.size());
            std::puts("fixture-followup");
            const auto unfinished=std::string("--wallet=\"")+std::string(70000,'w')+"fixture-eof-tail";
            CHECK(fwrite(unfinished.data(),1,unfinished.size(),stderr)==unfinished.size());}
        tee.stop();restarted=started&&tee.start();
        if(restarted){std::puts("fixture-stdout-restarted");std::fputs("fixture-stderr-restarted\n",stderr);}
        tee.stop();}
    fflush(stdout);fflush(stderr);restore(saved,1);restore(savedError,2);sink.close();CHECK(started&&restarted);
    const auto logged=slurp(sink.currentPath()),shown=slurp(console)+slurp(errors);
    CHECK(logged.find("fixture-split-secret")==logged.npos&&shown.find("fixture-split-secret")==shown.npos);
    CHECK(logged.find("fixture-json-tail")==logged.npos&&shown.find("fixture-json-tail")==shown.npos);
    CHECK(logged.find("fixture-eof-tail")==logged.npos&&shown.find("fixture-eof-tail")==shown.npos);
    CHECK(logged.find("output omitted: oversized line")!=logged.npos&&shown.find("output omitted: oversized line")!=shown.npos);
    CHECK(logged.find("fixture-followup")!=logged.npos&&shown.find("fixture-followup")!=shown.npos);
    for(const char *text:{"fixture-stdout-restarted","fixture-stderr-restarted"})CHECK(logged.find(text)!=logged.npos&&shown.find(text)!=shown.npos);
    CHECK(std::filesystem::file_size(sink.currentPath())<=config.maxFileBytes);
    std::error_code ec;std::filesystem::remove_all(directory,ec);CHECK(!ec);
    return finishTests("bounded synthetic stdout/stderr redaction without fragment disclosure");
}
}
int main(int argc,char **argv){if(argc!=2)return 4;const std::string action=argv[1];
    if(action=="quoted")return quoted();
    if(action=="stream")return stream();
    return 4;
}
