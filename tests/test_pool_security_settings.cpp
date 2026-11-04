// Synthetic declaration/files/UI and pure admission only. No resolver, TLS, helper, game or GPU.
#include "cpu_telemetry_fixture.h"
#include "product/settings.h"
#include "product_test_util.h"
using namespace om;
namespace {
std::string changed(std::string text,const std::string &a,const std::string &b){const auto at=text.find(a);if(at==text.npos)std::abort();text.replace(at,a.size(),b);return text;}
std::string contents(const std::filesystem::path &p){std::ifstream file(p,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
PoolSecurityRule rule(){return {PoolSecurityMode::PreferTls,"stratum+ssl://pool-fixture.test:8048","stratum+tcp://pool-fixture.test:7048",true};}
void urls(){
    PoolSecurityEndpoint out;CHECK(parsePoolSecurityUrl("stratum+ssl://POOL-Fixture.test:8048",&out)&&out.tls&&out.host=="pool-fixture.test"&&out.port==8048);
    CHECK(poolSecurityUrl(out)=="stratum+ssl://pool-fixture.test:8048");
    for(const char *good:{"stratum+ssl://localhost:1","stratum+ssl://xn--bcher-kva.test:65535","stratum+tcp://pool.test:2"})CHECK(parsePoolSecurityUrl(good,&out));
    for(const std::string &bad:std::vector<std::string>{"","pool.test:1234","https://pool.test:443","stratum+ssl://pool.test","stratum+SSL://pool.test:1",
        "stratum+ssl://user:pass@pool.test:1","stratum+ssl://user@pool.test:1","stratum+ssl://pool.test:0","stratum+ssl://pool.test:65536",
        "stratum+ssl://pool.test:01","stratum+ssl://pool.test:12345678901234567890","stratum+ssl://pool.test:1/path","stratum+ssl://pool.test:1?x",
        "stratum+ssl://pool.test:1#x","stratum+ssl://pool.test.:1","stratum+ssl://pool..test:1","stratum+ssl://-pool.test:1","stratum+ssl://pool-.test:1",
        "stratum+ssl://pool%2etest:1","stratum+ssl://pool\\test:1","stratum+ssl://pool_test:1","stratum+ssl://[::1]:1","stratum+ssl://127.0.0.1:1",
        "stratum+ssl://pool.test:1\n",std::string("stratum+ssl://pool\0.test:1",26),"stratum+ssl://"+std::string(64,'a')+".test:1"}){
        out={"unchanged",9,true};CHECK(!parsePoolSecurityUrl(bad,&out)&&out.host=="unchanged"&&out.port==9&&out.tls);}
}
void declarations(){
    PoolSecurityPreferences value;std::string error;CHECK(validPoolSecurityPreferences(value)&&!poolSecurityRequested(value));
    const auto defaults=poolSecurityPreferencesJson(value);CHECK(parsePoolSecurityPreferences(defaults,&value,&error));
    value.users[0]=rule();value.users.push_back({PoolSecurityMode::StrictTls,"stratum+ssl://other-fixture.test:1234",{},false});
    CHECK(validPoolSecurityPreferences(value)&&poolSecurityRequested(value));PoolSecurityPreferences copy;CHECK(parsePoolSecurityPreferences(poolSecurityPreferencesJson(value),&copy,&error)&&copy==value);
    CHECK(copy.donation.mode==PoolSecurityMode::Unchanged&&!copy.donation.plaintextConsent&&copy.donation.tlsUrl.empty());
    for(const auto &bad:std::vector<std::string>{"{}","[]",changed(defaults,"\"schema_version\":1","\"schema_version\":2"),changed(defaults,"\"schema_version\":1","\"schema_version\":1,\"schema_version\":1"),
        changed(defaults,"\"mode\":\"unchanged\"","\"mode\":\"strict_tls\""),changed(defaults,"\"plaintext_consent\":false","\"plaintext_consent\":true"),
        changed(defaults,"\"plaintext_consent\":false","\"plaintext_consent\":1"),changed(defaults,"\"donation\":","\"unknown\":"),
        changed(defaults,"\"tls_url\":\"\"","\"tls_url\":\"stratum+ssl://private-user@fixture.test:1234\""),std::string(8193,'x')}){
        const auto before=value;CHECK(!parsePoolSecurityPreferences(bad,&value,&error)&&value==before&&error.find("private-user")==error.npos);}
    for(unsigned mode=0;mode<6;++mode){auto bad=rule();if(mode==0)bad.mode=PoolSecurityMode::StrictTls;if(mode==1)bad.tlsUrl.clear();if(mode==2)bad.plaintextUrl.clear();
        if(mode==3)bad.tlsUrl="stratum+tcp://pool.test:1";
        if(mode==4)bad.plaintextUrl="stratum+ssl://pool.test:1";
        if(mode==5)bad.mode=static_cast<PoolSecurityMode>(7);
        CHECK(!validPoolSecurityRule(bad));}
    value={};value.users.resize(8,rule());CHECK(validPoolSecurityPreferences(value)&&parsePoolSecurityPreferences(poolSecurityPreferencesJson(value),&copy,&error)&&copy==value);
    value.users.push_back(rule());CHECK(!validPoolSecurityPreferences(value));value.users.clear();CHECK(!validPoolSecurityPreferences(value));
}
void fallback(){
    PoolSecurityPreferences preferences;preferences.users[0]=rule();PoolFallbackFailure evidence{7,PoolSecurityScope::User,0,preferences.users[0].tlsUrl,PoolTlsFailure::Connect};evidence.phase=PoolFallbackPhase::Connect;
    for(auto failure:{PoolTlsFailure::Dns,PoolTlsFailure::Connect,PoolTlsFailure::NetworkTimeout}){auto e=evidence;e.failure=failure;if(failure==PoolTlsFailure::Dns)e.phase=PoolFallbackPhase::Resolve;PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);
        const auto selected=gate.select(e);CHECK(selected&&!selected->tls&&poolSecurityUrl(*selected)==preferences.users[0].plaintextUrl&&!gate.select(e));}
    for(auto failure:{PoolTlsFailure::Verification,PoolTlsFailure::Protocol,PoolTlsFailure::Unknown,PoolTlsFailure::Cancelled,PoolTlsFailure::Clock,PoolTlsFailure::Unavailable}){
        auto e=evidence;e.failure=failure;PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);CHECK(!gate.select(e)&&!gate.select(evidence));}
    for(unsigned mode=0;mode<8;++mode){auto e=evidence;if(mode==0)e.explicitStrictUrl=true;if(mode==1)e.verified=true;if(mode==2)e.applicationAttempted=true;
        if(mode==3)e.attempt=8;
        if(mode==4)e.scope=PoolSecurityScope::Donation;
        if(mode==5)e.userSlot=1;
        if(mode==6)e.tlsUrl="stratum+ssl://changed.test:1";
        if(mode==7)e.failure=static_cast<PoolTlsFailure>(99);
        PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);CHECK(!gate.select(e)&&!gate.select(evidence));}
    {PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);gate.stop();CHECK(!gate.select(evidence));}
    for(auto phase:{PoolFallbackPhase::Unknown,PoolFallbackPhase::Handshake,PoolFallbackPhase::Authenticated}){
        auto e=evidence;e.phase=phase;e.failure=PoolTlsFailure::NetworkTimeout;PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);CHECK(!gate.select(e));}
    {auto e=evidence;e.failure=PoolTlsFailure::Dns;PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);CHECK(!gate.select(e));}
    preferences.donation=rule();auto e=evidence;e.scope=PoolSecurityScope::Donation;
    {PoolFallbackGate gate(preferences,PoolSecurityScope::Donation,0,7);CHECK(!gate.select(e));} // no qualified fixed secure donation tuple, even with independent consent
    for(unsigned slot:{1u,99u}){PoolFallbackGate gate(preferences,PoolSecurityScope::User,slot,7);CHECK(!gate.select(evidence));}
    preferences.users[0].plaintextConsent=false;{PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);CHECK(!gate.select(evidence));}
    preferences.users[0]=rule();preferences.users[0].mode=PoolSecurityMode::StrictTls;preferences.users[0].plaintextConsent=false;
    {PoolFallbackGate gate(preferences,PoolSecurityScope::User,0,7);CHECK(!gate.select(evidence));}
}
void storage(const std::filesystem::path &dir){
    const auto path=dir/std::filesystem::u8path(u8"fixture-池安全.json");PoolSecurityPreferencesStore first(path),second(path);PoolSecurityPreferences value,copy;std::string error;
    CHECK(!first.save(value,&error));CHECK(first.load(&value,&error)&&!std::filesystem::exists(path));CHECK(second.load(&copy,&error));value.users[0]=rule();
    CHECK(first.save(value,&error));const auto saved=contents(path);CHECK(!second.save(copy,&error)&&contents(path)==saved);CHECK(second.load(&copy,&error)&&copy==value);
    {RuntimePreferencesLease lease;auto lock=path;lock+=".lock";CHECK(lease.acquire(lock)&&!second.save(copy,&error));}
    CHECK(second.save(copy,&error));{std::ofstream file(path);file<<"{\"schema_version\":99}";}
    CHECK(!second.load(&copy,&error)&&copy==value&&!second.save(value,&error)&&contents(path)=="{\"schema_version\":99}");
    {std::ofstream file(path);file<<saved;}CHECK(!second.save(value,&error));CHECK(second.load(&copy,&error)&&second.save(copy,&error));
    {std::ofstream file(path);file<<std::string(8193,'x');}CHECK(!second.load(&copy,&error)&&copy==value);
    std::filesystem::create_directory(dir/"directory.json");PoolSecurityPreferencesStore directory(dir/"directory.json");CHECK(!directory.load(&copy,&error));
#if !defined(_WIN32)
    const auto link=dir/"link.json";std::filesystem::create_symlink(path.filename(),link);PoolSecurityPreferencesStore linked(link);CHECK(!linked.load(&copy,&error));
    struct stat info{};CHECK(stat(path.c_str(),&info)==0&&(info.st_mode&077)==0);
#endif
}
void type(PoolSecuritySettingsPanel &p,const std::string &text){p.key('\r');for(char ch:text)CHECK(p.key(ch)==PoolSecuritySettingsResult::None);p.key('\r');}
void panel(const std::filesystem::path &dir){
    PoolSecuritySettingsPanel p;p.open({},{true});unsigned saves=0;auto save=[&](const PoolSecurityPreferences &,std::string *){++saves;return true;};
    p.field=1;p.key(' ');CHECK(p.draft.users[0].mode==PoolSecurityMode::StrictTls&&!p.draft.users[0].plaintextConsent);p.key('v');CHECK(p.page==PoolSecuritySettingsPage::Browse&&p.field==2&&saves==0);
    type(p,"stratum+ssl://pool-fixture.test:8048");CHECK(p.draft.users[0].tlsUrl==rule().tlsUrl);p.field=1;p.key(' ');CHECK(p.draft.users[0].mode==PoolSecurityMode::PreferTls);
    p.field=3;type(p,rule().plaintextUrl);p.field=4;p.key('\r');for(char ch:std::string("ALLOW PLAINTEXT"))p.key(ch);
    const auto warning=p.render(20,12,ColorLevel::None);CHECK(warning.find("Login/proofs")!=warning.npos&&warning.find("MITM")!=warning.npos&&warning.find("ALLOW PLAINTEXT_")!=warning.npos);
    p.key('\r');CHECK(p.draft.users[0].plaintextConsent&&!p.draft.donation.plaintextConsent);p.key('v');CHECK(p.page==PoolSecuritySettingsPage::Review&&saves==0);
    p.key('\r',[](const PoolSecurityPreferences &,std::string *error){*error="fixture save refused";return false;});CHECK(p.dirty()&&p.message=="fixture save refused");
    p.key('\r',save);CHECK(saves==1&&!p.dirty()&&p.context.running);
    p.field=2;type(p,"stratum+ssl://changed-fixture.test:8048");CHECK(!p.draft.users[0].plaintextConsent&&p.active.users[0].plaintextConsent);
    p.field=5;p.key('\r');CHECK(p.draft.users.size()==2&&p.slot==1&&p.field==2&&p.draft.users[1].mode==PoolSecurityMode::StrictTls&&!p.draft.users[1].plaintextConsent);
    p.field=6;type(p,"REMOVE POOL");CHECK(p.draft.users.size()==1);p.key(27);CHECK(p.page==PoolSecuritySettingsPage::Leave);p.key(27);CHECK(p.dirty());p.key(27);CHECK(p.key('d')==PoolSecuritySettingsResult::Close&&!p.visible&&!p.dirty());
    p.open({});p.field=2;p.key('\r');for(char ch:std::string("QDLS"))CHECK(p.key(ch)==PoolSecuritySettingsResult::None);CHECK(p.input=="QDLS"&&p.key(3)==PoolSecuritySettingsResult::Quit);p.key(27);CHECK(!p.dirty());
    auto values=PoolSecurityPreferences{};values.users[0]=rule();p.open(values);
    for(int width:{20,40,80,120})for(int rows:{12,16,24}){p.field=6;p.page=PoolSecuritySettingsPage::Browse;const auto frame=p.render(width,rows,ColorLevel::None);CHECK(frame.find("Remove user")!=frame.npos&&std::count(frame.begin(),frame.end(),'\n')<rows);
        p.key('v');for(unsigned i=0;i<500;++i)p.key(kKeyPageDown);CHECK(p.render(width,rows,ColorLevel::None).find("[End of TLS review]")!=std::string::npos);p.key(27);}
    SettingsPanel root;root.open({});root.draft.color=false;root.group=6;root.groupsFocused=false;CHECK(root.key('\r',{})==SettingsResult::OpenPoolSecurity&&root.fields()==1);
    PoolSecurityPreferencesStore store(dir/"independent-pools.json");openPoolSecurityControls(&root,&store,{});CHECK(root.poolSecurity.visible&&root.dirty());
    root.poolSecurity.draft.users[0]=rule();unsigned ui=0,pool=0;const auto key=[&](int ch){return root.key(ch,[&](const UiPreferences &,std::string *){++ui;return true;},{},{},0,{},{},{},[&](const PoolSecurityPreferences &v,std::string *error){++pool;return store.save(v,error);});};
    key('v');key('\r');CHECK(pool==1&&ui==0&&root.dirty()&&!root.poolSecurity.dirty());key(27);CHECK(!root.poolSecurity.visible);openPoolSecurityControls(&root,nullptr,{});CHECK(!root.poolSecurity.visible&&!root.message.empty());
}
}
int main(){const auto dir=std::filesystem::path(makeScratchDir("pool-security"));urls();declarations();fallback();storage(dir);panel(dir);std::error_code ec;std::filesystem::remove_all(dir,ec);return finishTests("native independent pool-security declarations (no network/mining)");}
