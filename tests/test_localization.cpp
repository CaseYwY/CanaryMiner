// Pure bounded catalog/UTF-8 scalar/template contracts, not terminal grapheme/cell/bidi/shaping qualification.
#include "product/localization.h"
#include "product_test_util.h"
#include <set>
using namespace om::i18n;
namespace {
std::string encode(uint32_t c){std::string s;if(c<0x80)s+=static_cast<char>(c);
    else if(c<0x800){s+=static_cast<char>(0xc0|(c>>6));s+=static_cast<char>(0x80|(c&63));}
    else if(c<0x10000){s+=static_cast<char>(0xe0|(c>>12));s+=static_cast<char>(0x80|((c>>6)&63));s+=static_cast<char>(0x80|(c&63));}
    else{s+=static_cast<char>(0xf0|(c>>18));s+=static_cast<char>(0x80|((c>>12)&63));s+=static_cast<char>(0x80|((c>>6)&63));s+=static_cast<char>(0x80|(c&63));}return s;}
void packs(){
    const std::vector<std::string_view> required={"en","zh-Hans","zh-Hant","es","pt-BR","fr","de","ru","ja","ko","ar","hi","bn","id","tr","vi"};
    std::set<std::string_view> seen;std::string output;CHECK(localePacks().size()==16&&messageKeyCount==13);
    for(std::size_t p=0;p<localePacks().size();++p){const auto &pack=localePacks()[p];CHECK(pack.id==required[p]&&seen.insert(pack.id).second&&safeMessageText(pack.autonym)&&!pack.autonym.empty());
        CHECK(pack.rtl==(pack.id=="ar")&&pack.review==(p?ReviewStatus::MachineAssisted:ReviewStatus::Source));
        for(std::size_t k=0;k<messageKeyCount;++k){const auto key=static_cast<MessageKey>(k);const auto text=message(pack.id,key);
            CHECK(!messageKeyName(key).empty()&&text.locale==pack.id&&text.text==pack.messages[k]&&!text.fallback&&text.review==pack.review&&text.rtl==pack.rtl);
            CHECK(validTemplateTranslation(localePacks()[0].messages[k],text.text));
            const MessageArguments args=key==MessageKey::SourceValue?MessageArguments{{"value","1.23 PRL / USDT / USD / UTC"},{"source","stratum+ssl://fixture.test:8048"}}:MessageArguments{};
            CHECK(formatMessage(pack.id,key,args,&output));
            if(key==MessageKey::SourceValue)CHECK(output.find(args[0].second)!=output.npos&&output.find(args[1].second)!=output.npos);
        }
    }
    for(auto unknown:{"","en-US","zh-hans","auto","unsupported","en\n"}){LocalizedMessage metadata;CHECK(formatMessage(unknown,MessageKey::Save,{},&output,&metadata)&&output=="Save"&&metadata.fallback&&metadata.locale=="en"&&metadata.review==ReviewStatus::Source&&!metadata.rtl);}
    CHECK(messageKeyName(MessageKey::Count).empty()&&messageKeyName(static_cast<MessageKey>(-1)).empty());
    output="unchanged";CHECK(!formatMessage("en",MessageKey::Count,{},&output)&&output=="unchanged");
}
void templates(){
    CHECK(validTemplateTranslation("{source}: {value}","{value} from {source}"));
    CHECK(!validTemplateTranslation("{source}: {value}","{value}")&&!validTemplateTranslation("{source}","{other}")&&!validTemplateTranslation("{source}","{source} {source}"));
    CHECK(validTemplateTranslation("plain","%s is literal, never a printf format"));
    std::vector<std::string_view> out={"unchanged"};
    for(const auto &bad:std::vector<std::string>{"{","}","{}","{{source}}","{Source}","{source-name}","{source name}","{source\n}","{"+std::string(33,'s')+"}","{a}{a}{a}{a}{a}{a}{a}{a}{a}"}){
        CHECK(!templateParameters(bad,&out)&&out==std::vector<std::string_view>{"unchanged"});}
    const MessageArguments good={{"source","fixture"},{"value","{literal} 100%"}};std::string text;CHECK(formatMessage("ar",MessageKey::SourceValue,good,&text)&&text.find("{literal} 100%")!=text.npos);
    for(auto bad:std::vector<MessageArguments>{{},{{"source","fixture"}},{{"source","fixture"},{"source","duplicate"},{"value","x"}},{{"source","x"},{"value","x"},{"extra","x"}},
        {{"source","x"},{"value",std::string("x\0y",3)}},{{"source","x"},{"value","\x1b[31m"}},{{"source",std::string(4096,'s')},{"value","x"}}}){
        text="unchanged";LocalizedMessage meta{"previous","previous",ReviewStatus::HumanReviewed,true,true};
        CHECK(!formatMessage("en",MessageKey::SourceValue,bad,&text,&meta)&&text=="unchanged"&&meta.text=="previous"&&meta.review==ReviewStatus::HumanReviewed);
    }
    std::string aliased="en";CHECK(formatMessage(aliased,MessageKey::Save,{},&aliased)&&aliased=="Save");
    auto argumentAlias=good;CHECK(formatMessage("en",MessageKey::SourceValue,argumentAlias,&argumentAlias[1].second)&&argumentAlias[1].second=="fixture: {literal} 100%");
    for(auto locale:{"en","zh-Hans"}){const std::size_t separator=std::string_view(locale)=="en"?2:3;MessageArguments maximum={{"source","s"},{"value",std::string(4095-separator,'x')}};
        CHECK(formatMessage(locale,MessageKey::SourceValue,maximum,&text)&&text.size()==4096);
        maximum[1].second+='x';text="unchanged";CHECK(!formatMessage(locale,MessageKey::SourceValue,maximum,&text)&&text=="unchanged");}
}
void unicode(){
    CHECK(safeMessageText(u8"e\u0301 中 👩‍💻 العربية हिन्दी বাংলা")); // validation is not width or grapheme qualification
    for(const std::string &bad:std::vector<std::string>{std::string("\x80",1),std::string("\xc0\x80",2),std::string("\xc2",1),std::string("\xe0\x80\x80",3),
        std::string("\xed\xa0\x80",3),std::string("\xf0\x80\x80\x80",4),std::string("\xf4\x90\x80\x80",4),std::string("\xf5\x80\x80\x80",4),std::string(4097,'x')})CHECK(!safeMessageText(bad));
    unsigned mismatches=0;
    for(uint32_t c=0;c<=0x10ffff;++c){const bool expected=c>=0x20&&!(c>=0x7f&&c<=0x9f)&&!(c>=0xd800&&c<=0xdfff)&&!(c>=0x202a&&c<=0x202e)&&!(c>=0x2066&&c<=0x2069)&&c!=0x2028&&c!=0x2029;
        if(safeMessageText(encode(c))!=expected)++mismatches;}
    CHECK(mismatches==0);
    for(uint32_t c:{0x202au,0x202eu,0x2066u,0x2069u})CHECK(!safeMessageText(encode(c)));
}
}
int main(){packs();templates();unicode();return finishTests("16-locale message-key foundation (no terminal/runtime activation)");}
