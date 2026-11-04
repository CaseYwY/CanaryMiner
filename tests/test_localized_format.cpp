// Exact presentation/plural fixtures only; no asset-scale inference, financial source or machine-output mutation.
#include "product/localized_format.h"
#include "product/localization.h"
#include "product_test_util.h"
#include <clocale>
#include <set>
using namespace om;
namespace {
bool same(const PresentationNumber &a,const PresentationNumber &b){return a.text==b.text&&a.ascii==b.ascii&&a.formattingLocale==b.formattingLocale&&a.language==b.language&&
    a.pluralCategory==b.pluralCategory&&a.backendFallback==b.backendFallback&&a.localeFallback==b.localeFallback&&a.languageFallback==b.languageFallback&&a.resourceInheritance==b.resourceInheritance&&a.pluralAvailable==b.pluralAvailable&&a.rounded==b.rounded&&a.asciiRecovery==b.asciiRecovery;}
void boundaries(){
    const auto &ids=presentationFormattingLocales();CHECK(ids.size()==31&&std::set<std::string>(ids.begin(),ids.end()).size()==ids.size());
    PresentationNumberRequest request;request.decimal="1234.5";PresentationNumber value;CHECK(formatPresentationNumber(request,&value)&&value.ascii=="1234.50"&&!value.rounded);
    const bool native=!value.backendFallback;CHECK(native?(value.text=="1,234.50"&&value.pluralAvailable):(value.text==value.ascii&&!value.pluralAvailable&&value.pluralCategory.empty()));
    CHECK(native?value.resourceInheritance:!value.resourceInheritance); // pinned ICU 79.1 reports inherited number-pattern resources
    for(const auto &test:std::vector<std::pair<std::string,std::string>>{{"1.225","1.22"},{"1.235","1.24"},{"-1.225","-1.22"},{"-1.235","-1.24"},{"0.005","0.00"},{"-0.005","0.00"},{"9.995","10.00"},{"-0","0.00"}}){
        request.decimal=test.first;CHECK(formatPresentationNumber(request,&value)&&value.ascii==test.second);}
    PresentationNumber before;before.text="unchanged";before.ascii="previous";before.pluralCategory="previous";before.localeFallback=true;
    for(const std::string &bad:std::vector<std::string>{"","+1","01","1.",".1","1e3","NaN","Infinity"," 1","1,2",u8"١",std::string("1\0x",3),std::string(97,'9'),"0."+std::string(49,'1')}){
        request.decimal=bad;value=before;CHECK(!formatPresentationNumber(request,&value)&&same(value,before));}
    request.decimal="1";
    for(unsigned mode=0;mode<5;++mode){auto bad=request;if(mode==0)bad.fractionDigits=25;if(mode==1)bad.digits=static_cast<PresentationDigits>(99);
        if(mode==2)bad.formattingLocale="en@currency=USD";
        if(mode==3)bad.language="ar\n";
        if(mode==4)bad.formattingLocale=std::string(36,'x');
        value=before;CHECK(!formatPresentationNumber(bad,&value)&&same(value,before));}
    CHECK(!formatPresentationNumber(request,nullptr));
    request.formattingLocale="unsupported";request.language="unsupported";CHECK(formatPresentationNumber(request,&value)&&value.localeFallback&&value.languageFallback&&value.formattingLocale=="en"&&value.language=="en");
    request={};request.decimal=std::string(96,'9');request.fractionDigits=0;CHECK(formatPresentationNumber(request,&value)&&value.ascii==request.decimal);
    request.decimal="0.123456789012345678901234";request.fractionDigits=24;CHECK(formatPresentationNumber(request,&value)&&value.ascii==request.decimal&&!value.rounded);
    request={};request.decimal="9007199254740993.01";CHECK(formatPresentationNumber(request,&value)&&value.ascii==request.decimal&&!value.rounded);
    if(!value.backendFallback)CHECK(value.text=="9,007,199,254,740,993.01");
    request.grouping=false;CHECK(formatPresentationNumber(request,&value)&&value.text==request.decimal);
    request.decimal=std::string(96,'9')+".5";request.fractionDigits=0;value=before;CHECK(!formatPresentationNumber(request,&value)&&same(value,before));
}
void independent(){
    PresentationNumberRequest request;request.decimal="1234567.5";PresentationNumber original;CHECK(formatPresentationNumber(request,&original));
    for(const auto &pack:i18n::localePacks()){request.language=std::string(pack.id);PresentationNumber changed;CHECK(formatPresentationNumber(request,&changed)&&changed.text==original.text&&changed.ascii==original.ascii&&changed.formattingLocale==original.formattingLocale&&!changed.languageFallback);}
    request.language="ar";
    for(const auto &locale:std::vector<std::string>{"en","en-US","en-GB","de-DE","fr-FR","hi-IN","bn-BD","zh-CN","zh-TW","ar-EG","pt-BR"}){
        request.formattingLocale=locale;PresentationNumber changed;CHECK(formatPresentationNumber(request,&changed)&&changed.ascii==original.ascii&&!changed.localeFallback&&changed.language=="ar");
        if(!changed.backendFallback){if(locale=="de-DE")CHECK(changed.text=="1.234.567,50");if(locale=="fr-FR")CHECK(changed.text==u8"1\u202f234\u202f567,50");if(locale=="hi-IN")CHECK(changed.text=="12,34,567.50");}
    }
    request.formattingLocale="ar-EG";request.digits=PresentationDigits::Locale;request.decimal="1234.5";PresentationNumber arabic;CHECK(formatPresentationNumber(request,&arabic));
    if(!arabic.backendFallback)CHECK(arabic.text==u8"١٬٢٣٤٫٥٠");
    request.formattingLocale="ar";CHECK(formatPresentationNumber(request,&arabic));if(!arabic.backendFallback)CHECK(arabic.text=="1,234.50"); // ICU 79.1 default is latn, not inferred from language
    request.formattingLocale="ar-EG";
    request.decimal="-1234.5";CHECK(formatPresentationNumber(request,&arabic)&&i18n::safeMessageText(arabic.text)&&arabic.text.find(u8"\u061c")==arabic.text.npos);
    request.formattingLocale="en";request.digits=PresentationDigits::Latin;request.decimal="1";
    for(const auto &test:std::vector<std::pair<std::string,std::string>>{{"en","one"},{"ja","other"},{"ar","one"},{"ru","one"}}){request.language=test.first;request.fractionDigits=0;PresentationNumber changed;CHECK(formatPresentationNumber(request,&changed));
        if(!changed.backendFallback)CHECK(changed.pluralCategory==test.second);}
    request.language="en";request.fractionDigits=1;CHECK(formatPresentationNumber(request,&arabic));if(!arabic.backendFallback)CHECK(arabic.pluralCategory=="other");
    request.language="ar";request.fractionDigits=0;
    for(const auto &test:std::vector<std::pair<std::string,std::string>>{{"0","zero"},{"1","one"},{"2","two"},{"3","few"},{"11","many"},{"100","other"}}){request.decimal=test.first;CHECK(formatPresentationNumber(request,&arabic));if(!arabic.backendFallback)CHECK(arabic.pluralCategory==test.second);}
    for(const auto &format:presentationFormattingLocales())for(const auto &language:i18n::localePacks())for(auto digits:{PresentationDigits::Latin,PresentationDigits::Locale})for(const auto &decimal:std::vector<std::string>{"0","1","2","3","11","101","-1234.5","9007199254740993.01"}){
        request.formattingLocale=format;request.language=std::string(language.id);request.decimal=decimal;request.fractionDigits=2;request.digits=digits;PresentationNumber changed;
        CHECK(formatPresentationNumber(request,&changed)&&i18n::safeMessageText(changed.text)&&!changed.localeFallback&&!changed.languageFallback&&changed.ascii.find("PRL")==changed.ascii.npos);
    }
}
}
int main(){const std::string processLocale=std::setlocale(LC_ALL,nullptr);boundaries();independent();
    PresentationNumberRequest recovery;recovery.decimal="1234.5";recovery.formattingLocale="ar-EG";recovery.language="ar";recovery.digits=PresentationDigits::Locale;recovery.asciiOnly=true;
    PresentationNumber result;CHECK(formatPresentationNumber(recovery,&result)&&result.text=="1234.50"&&result.asciiRecovery&&!result.backendFallback&&!result.pluralAvailable&&result.pluralCategory.empty()&&!result.resourceInheritance);
    CHECK(processLocale==std::setlocale(LC_ALL,nullptr));return finishTests("exact independent presentation locale and maintained cardinal plural (not native activation)");}
