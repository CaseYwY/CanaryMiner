#include "product_test_util.h"
#include <sstream>
#if defined(__linux__)
#include <unistd.h>
namespace om {
bool gameDetectionTestIdentity(const std::string &,uint64_t,uint64_t &,bool &);
bool gameDetectionTestUser(const std::string &,bool &);
bool gameDetectionTestMount(const std::string &,uint64_t);
}
std::string stat(const std::string &state="R",const std::string &flags="0",const std::string &start="5678"){
    std::string s="42 (name ) with ( spaces\n) ";for(unsigned i=3;i<=22;++i)s+=(i==3?state:i==9?flags:i==22?start:"0")+' ';return s+'\n';
}
#elif defined(_WIN32)
namespace om {bool gameDetectionTestName(const std::wstring &,const std::string &);}
#endif
int main(){
#if defined(__linux__)
    uint64_t created=0;bool active=false,selected=false;
    CHECK(om::gameDetectionTestIdentity(stat(),42,created,active)&&created==5678&&active);
    for(const auto &state:{"Z","X","x"})CHECK(om::gameDetectionTestIdentity(stat(state),42,created,active)&&!active);
    CHECK(om::gameDetectionTestIdentity(stat("S","2097152"),42,created,active)&&!active);
    CHECK(!om::gameDetectionTestIdentity(stat(),43,created,active));
    for(const auto &value:{"0","-1","18446744073709551616","1x"})CHECK(!om::gameDetectionTestIdentity(stat("R","0",value),42,created,active));
    CHECK(!om::gameDetectionTestIdentity("42 (incomplete) R",42,created,active));
    CHECK(!om::gameDetectionTestIdentity(stat("RR"),42,created,active));
    const auto uid=std::to_string(geteuid());const auto line="Uid:\t"+uid+'\t'+uid+'\t'+uid+'\t'+uid+'\n';
    CHECK(om::gameDetectionTestUser("Name:\tfixture\n"+line+"State:\tR\n",selected)&&selected);
    CHECK(!om::gameDetectionTestUser(line+line,selected)&&!om::gameDetectionTestUser("Uid: 1 2 3\n",selected));
    CHECK(!om::gameDetectionTestUser("Uid: 1 2 3 4 5\n",selected)&&!om::gameDetectionTestUser("Uid: 1 2 3 x\n",selected));
    const auto other=std::to_string(static_cast<uint64_t>(geteuid())+1);CHECK(om::gameDetectionTestUser("Uid: "+other+' '+other+' '+other+' '+other+'\n',selected)&&!selected);
    const std::string original="31 20 0:1 / /proc rw - proc proc rw,hidepid=2\n",mounted="42 20 0:2 / /proc rw - proc proc rw\n";
    CHECK(om::gameDetectionTestMount(original+mounted,42)); // a covered mount is not the active procfs view
    CHECK(!om::gameDetectionTestMount(original+mounted,31)&&!om::gameDetectionTestMount(mounted+mounted,42)&&!om::gameDetectionTestMount(mounted,43));
    for(const auto &option:{"hidepid=1","hidepid=2","hidepid=4","hidepid=invisible","hidepid=ptraceable"})CHECK(!om::gameDetectionTestMount("42 20 0:2 / /proc rw - proc proc rw,"+std::string(option)+'\n',42));
    CHECK(om::gameDetectionTestMount("42 20 0:2 / /proc rw - proc proc rw,hidepid=0,subset=pid\n",42));
#elif defined(_WIN32)
    CHECK(om::gameDetectionTestName(L"FixtureGame.exe","fixturegame.EXE"));
    for(const auto &s:{L"FixtureGame.exe-extra",L"xFixtureGame.exe",L"FixtureGame",L"FixtureGame.exe.old",L"FixtureGame.exe ",L"F\u0131xtureGame.exe"})CHECK(!om::gameDetectionTestName(s,"FixtureGame.exe"));
    CHECK(!om::gameDetectionTestName(L"FixtureGame.exe","FixtureGame.exe.old"));
#endif
    return finishTests("game native identity parsing");
}
