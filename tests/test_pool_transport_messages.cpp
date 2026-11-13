#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
void migrationMessages(){
    using Key=i18n::PoolMigrationMessageKey;
    CHECK(i18n::poolMigrationMessageKeyCount==11&&i18n::poolMigrationLocalePacks().size()==16);
    std::set<std::string> identities;
    for(std::size_t key=0;key<i18n::poolMigrationMessageKeyCount;++key){
        const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::poolMigrationMessageKeyName(k))).second);
        for(std::size_t index=0;index<16;++index){
            const auto &pack=i18n::poolMigrationLocalePacks()[index];const auto &common=i18n::localePacks()[index];
            CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
            const auto message=i18n::poolMigrationMessage(pack.id,k);
            CHECK(!message.fallback&&message.locale==pack.id&&i18n::validTemplateTranslation(i18n::poolMigrationMessage("en",k).text,message.text));
            std::vector<std::string_view> names;CHECK(i18n::templateParameters(message.text,&names));
            TerminalPoolMigrationMessageRequest request;request.language=std::string(pack.id);request.key=k;
            for(const auto name:names)request.arguments.push_back({std::string(name),{{k==Key::Source?"stratum+tcp://prl-us.kryptex.network:7048":"stratum+ssl://prl-us.kryptex.network:8048",{},true},Tone::Amber}});
            for(const auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
                TerminalTextPolicy policy;policy.order=order;policy.columns=19;
                TerminalMessageLine line;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
                CHECK(layoutTerminalPoolMigrationMessage(request,policy,&line)&&line.line.line.cells<=19);
                if(!request.arguments.empty()){
                    // The typed renderer keeps the 41-cell endpoint atomic; the native panel has its own wrapping path.
                    CHECK(request.arguments.front().value.content.text.size()==41);
                    for(const std::size_t width:{19u,40u}){
                        policy.columns=width;CHECK(layoutTerminalPoolMigrationMessage(request,policy,&line)&&line.line.line.clipped);
                        std::string clippedEndpoint;for(const auto &run:line.line.runs)
                            if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]=="endpoint")clippedEndpoint+=run.content.text;
                        CHECK(clippedEndpoint.empty());
                        rows.rows.resize(1);rows.rows.front().line.text="retained";rows.metadata.renderedLanguage="retained";
                        painted.rows={"retained"};painted.metadata.renderedLanguage="retained";
                        CHECK(!wrapTerminalPoolMigrationMessage(request,policy,40,&rows)&&rows.rows.size()==1&&rows.rows.front().line.text=="retained"&&rows.metadata.renderedLanguage=="retained");
                        CHECK(!renderWrappedTerminalPoolMigrationMessage(request,policy,40,ColorLevel::None,&painted)&&painted.rows==std::vector<std::string>{"retained"}&&painted.metadata.renderedLanguage=="retained");
                    }
                    policy.columns=41;
                }
                CHECK(wrapTerminalPoolMigrationMessage(request,policy,40,&rows));
                CHECK(renderWrappedTerminalPoolMigrationMessage(request,policy,40,ColorLevel::None,&painted)&&painted.rows.size()==rows.rows.size());
                CHECK(rows.metadata.sources==painted.metadata.sources);
                unsigned endpointRows=0;
                for(const auto &row:rows.rows){
                    CHECK(row.line.cells<=policy.columns&&!row.line.clipped);
                    std::string endpoint;for(const auto &run:row.runs)
                        if(!run.content.control&&rows.metadata.sources[run.content.sourceSpan]=="endpoint")endpoint+=run.content.text;
                    if(!endpoint.empty()){++endpointRows;CHECK(!request.arguments.empty()&&endpoint==request.arguments.front().value.content.text);}
                }
                CHECK(endpointRows==(request.arguments.empty()?0u:1u));
            }
        }
    }
    CHECK(i18n::poolMigrationEnglish(Key::Target,{{"endpoint","stratum+ssl://prl-us.kryptex.network:8048"}})=="Proposed TLS: stratum+ssl://prl-us.kryptex.network:8048");
    CHECK(i18n::poolMigrationEnglish(Key::Source,{{"endpoint","stratum+tcp://prl-hk.kryptex.network:7048"}})=="Configured source: stratum+tcp://prl-hk.kryptex.network:7048");
    CHECK(i18n::poolMigrationEnglish(Key::Target,{{"endpoint","stratum+ssl://prl-hk.kryptex.network:8048"}})=="Proposed TLS: stratum+ssl://prl-hk.kryptex.network:8048");
    CHECK(i18n::poolMigrationEnglish(Key::Source,{{"endpoint","stratum+ssl://prl-hk.kryptex.network:8048"}}).empty());
    CHECK(i18n::poolMigrationEnglish(Key::Target,{{"endpoint","stratum+tcp://prl-hk.kryptex.network:7048"}}).empty());
    for(const auto value:{"stratum+ssl://wallet:password@prl-us.kryptex.network:8048","stratum+ssl://other.test:8048","PRIVATE-error","stratum+ssl://prl-us.kryptex.network:8048\n"})
        CHECK(i18n::poolMigrationEnglish(Key::Target,{{"endpoint",value}}).empty());
    CHECK(i18n::poolMigrationEnglish(Key::Count).empty()&&i18n::poolMigrationEnglish(Key::Target).empty());
    TerminalPoolMigrationMessageRequest bad;bad.key=Key::Target;bad.arguments={{"endpoint",{{"PRIVATE-wallet",{},true},Tone::Plain}}};
    TerminalTextPolicy policy;TerminalMessageLine line;line.line.line.text="retained";
    CHECK(!layoutTerminalPoolMigrationMessage(bad,policy,&line)&&line.line.line.text=="retained");
    bad.arguments[0].value.content.text="stratum+ssl://prl-us.kryptex.network:8048";bad.arguments[0].value.content.invariant=false;
    CHECK(!layoutTerminalPoolMigrationMessage(bad,policy,&line));
    bad.arguments[0].value.content={{"stratum+ssl://prl-hk.kryptex.network:8048"},{},true};
    policy.columns=80;
    CHECK(layoutTerminalPoolMigrationMessage(bad,policy,&line)&&line.line.line.text.find("prl-hk.kryptex.network:8048")!=std::string::npos);
    bad.key=Key::Source;
    CHECK(!layoutTerminalPoolMigrationMessage(bad,policy,&line));
}
int main(){
    using Key=i18n::PoolTransportMessageKey;
    CHECK(i18n::poolTransportMessageKeyCount==13&&i18n::poolTransportLocalePacks().size()==16);
    std::set<std::string> identities;
    for(std::size_t key=0;key<i18n::poolTransportMessageKeyCount;++key){
        const auto k=static_cast<Key>(key);CHECK(identities.insert(std::string(i18n::poolTransportMessageKeyName(k))).second);
        for(std::size_t index=0;index<16;++index){
            const auto &pack=i18n::poolTransportLocalePacks()[index];const auto &common=i18n::localePacks()[index];
            CHECK(pack.id==common.id&&pack.review==common.review&&pack.rtl==common.rtl);
            const auto message=i18n::poolTransportMessage(pack.id,k);
            CHECK(!message.fallback&&message.locale==pack.id&&i18n::validTemplateTranslation(i18n::poolTransportMessage("en",k).text,message.text));
            std::vector<std::string_view> names;CHECK(i18n::templateParameters(message.text,&names));
            TerminalPoolTransportMessageRequest request;request.language=std::string(pack.id);request.key=k;
            for(const auto name:names)request.arguments.push_back({std::string(name),{{name=="port"?"8048":name=="host"?"pool-fixture.test":"verification",{},true},Tone::Amber}});
            for(const auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
                TerminalTextPolicy policy;policy.order=order;policy.columns=19;
                TerminalMessageLine line;TerminalMessageRows rows;PaintedTerminalMessageRows painted;
                CHECK(layoutTerminalPoolTransportMessage(request,policy,&line)&&line.line.line.cells<=19);
                CHECK(wrapTerminalPoolTransportMessage(request,policy,40,&rows));
                CHECK(renderWrappedTerminalPoolTransportMessage(request,policy,40,ColorLevel::None,&painted)&&painted.rows.size()==rows.rows.size());
                for(const auto &row:rows.rows)CHECK(row.line.cells<=19&&!row.line.clipped);
            }
        }
    }
    CHECK(i18n::poolTransportEnglish(Key::VerifiedTls,{{"port","8048"}})=="First user pool transport: Verified TLS (port 8048)");
    for(const auto &port:{"0","65536","-1","8048\n","PRIVATE-wallet"})CHECK(i18n::poolTransportEnglish(Key::VerifiedTls,{{"port",port}}).empty());
    CHECK(i18n::poolTransportEnglish(Key::Error,{{"code","PRIVATE-error"}}).empty());
    CHECK(i18n::poolTransportEnglish(Key::Host,{{"host","wallet:password@pool.test"}}).empty());
    CHECK(i18n::poolTransportEnglish(Key::Count).empty());
    TerminalPoolTransportMessageRequest bad;bad.key=Key::Error;bad.arguments={ {"code",{{"PRIVATE-error",{},true},Tone::Plain}} };
    TerminalTextPolicy policy;TerminalMessageLine line;line.line.line.text="retained";
    CHECK(!layoutTerminalPoolTransportMessage(bad,policy,&line)&&line.line.line.text=="retained");
    bad.arguments[0].value.content.text="verification";bad.arguments[0].value.content.invariant=false;
    CHECK(!layoutTerminalPoolTransportMessage(bad,policy,&line));
    migrationMessages();
    return finishTests("closed transport display codes and sixteen-pack current-state and migration messages");
}
