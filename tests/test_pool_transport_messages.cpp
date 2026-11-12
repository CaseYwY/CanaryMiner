#include "product/terminal_settings_message.h"
#include "product_test_util.h"
#include <set>
using namespace om;
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
    return finishTests("closed transport display codes and sixteen-pack current-state messages");
}
