// Synthetic native requests/store/CLI state. No ICU call, helper, detector, financial source, network or GPU.
#include "cpu_telemetry_fixture.h"
#include "product/settings.h"
#include "product/product_cli.h"
#include "product_test_util.h"
using namespace om;
namespace {
std::string changed(std::string text,const std::string &a,const std::string &b){const auto at=text.find(a);if(at==text.npos)std::abort();text.replace(at,a.size(),b);return text;}
std::string contents(const std::filesystem::path &p){std::ifstream file(p,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void parsing(){
    LanguagePreferences value,copy;std::string error;CHECK(validLanguagePreferences(value));const auto defaults=languagePreferencesJson(value);
    CHECK(parseLanguagePreferences(defaults,&copy,&error)&&copy==value);
    for(const auto &language:i18n::localePacks())for(const auto &format:presentation_detail::formattingLocales())for(auto digits:{PresentationDigits::Latin,PresentationDigits::Locale})for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual}){
        value.language=std::string(language.id);value.formattingLocale=format.id;value.digits=digits;value.textOrder=order;
        CHECK(validLanguagePreferences(value)&&parseLanguagePreferences(languagePreferencesJson(value),&copy,&error)&&copy==value);
        CHECK(std::string(effectiveNativeMessageLanguage())=="en"&&std::string(effectiveNativeNumberFormat())=="legacy per-field formatting");
    }
    for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft})for(unsigned wide:{1u,2u})for(unsigned emoji:{1u,2u})for(bool grouping:{false,true}){
        value.direction=direction;value.ambiguousCells=wide;value.emojiCells=emoji;value.grouping=grouping;CHECK(parseLanguagePreferences(languagePreferencesJson(value),&copy,&error)&&copy==value);}
    for(const auto &bad:std::vector<std::string>{"{}","[]",changed(defaults,"\"schema_version\":1","\"schema_version\":2"),changed(defaults,"\"schema_version\":1","\"schema_version\":1,\"schema_version\":1"),
        changed(defaults,"\"language\":\"en\"","\"language\":\"private-invalid\""),changed(defaults,"\"formatting_locale\":\"en\"","\"formatting_locale\":\"ar@currency=USD\""),
        changed(defaults,"\"digits\":\"latin\"","\"digits\":\"arabic\""),changed(defaults,"\"text_order\":\"ascii\"","\"text_order\":\"unknown\""),
        changed(defaults,"\"direction\":\"auto\"","\"direction\":\"unknown\""),changed(defaults,"\"emoji_cells\":2","\"emoji_cells\":3"),
        changed(defaults,"\"ambiguous_cells\":1","\"ambiguous_cells\":1.0"),changed(defaults,"\"grouping\":true","\"grouping\":1"),
        changed(defaults,"\"digits\":","\"currency\":"),std::string(4097,'x')}){
        const auto before=value;CHECK(!parseLanguagePreferences(bad,&value,&error)&&value==before&&error.find("private-invalid")==error.npos);}
    LanguageOverrides overrides;overrides.language="zh-Hans";LanguagePreferences saved;saved.formattingLocale="de-DE";saved.textOrder=TerminalTextOrder::Visual;
    CHECK(applyLanguageOverrides(saved,overrides,&copy,&error)&&copy.language=="zh-Hans"&&copy.formattingLocale=="de-DE"&&copy.textOrder==TerminalTextOrder::Visual&&saved.language=="en");
    overrides.language.reset();overrides.formattingLocale="en-US";overrides.ascii=true;CHECK(applyLanguageOverrides(saved,overrides,&copy,&error)&&copy.language=="en"&&copy.formattingLocale=="en-US"&&copy.textOrder==TerminalTextOrder::Ascii);
    overrides.language="private-invalid";const auto before=copy;CHECK(!applyLanguageOverrides(saved,overrides,&copy,&error)&&copy==before);
}
void storage(const std::filesystem::path &dir){
    const auto path=dir/std::filesystem::u8path(u8"fixture-语言.json");LanguagePreferencesStore first(path),second(path);LanguagePreferences p,copy;std::string error;
    CHECK(!first.save(p,&error)&&first.load(&p,&error)&&second.load(&copy,&error)&&!std::filesystem::exists(path));p.language="fr";p.formattingLocale="en-US";CHECK(first.save(p,&error));
    const auto saved=contents(path);CHECK(!second.save(copy,&error)&&contents(path)==saved);CHECK(second.load(&copy,&error)&&copy==p);
    {RuntimePreferencesLease lease;auto lock=path;lock+=".lock";CHECK(lease.acquire(lock)&&!second.save(copy,&error));}
    CHECK(second.save(copy,&error));{std::ofstream file(path);file<<"{\"schema_version\":99}";}
    CHECK(!second.load(&copy,&error)&&copy==p&&!second.save(p,&error)&&contents(path)=="{\"schema_version\":99}");
    {std::ofstream file(path);file<<saved;}CHECK(second.load(&copy,&error)&&second.save(copy,&error));
#if !defined(_WIN32)
    const auto link=dir/"language-link.json";std::filesystem::create_symlink(path.filename(),link);LanguagePreferencesStore linked(link);CHECK(!linked.load(&copy,&error));
    struct stat info{};CHECK(stat(path.c_str(),&info)==0&&(info.st_mode&077)==0);
#endif
}
void panel(const std::filesystem::path &dir){
    LanguageSettingsPanel p;p.open({});const auto original=p.draft;p.key(' ');CHECK(p.draft.language=="zh-Hans"&&p.draft.formattingLocale=="en"&&p.dirty());
    p.field=1;p.key(kKeyLeft);CHECK(p.draft.formattingLocale=="vi-VN"&&p.draft.language=="zh-Hans");
    for(unsigned i=2;i<8;++i){p.field=i;p.key(' ');}CHECK(validLanguagePreferences(p.draft)&&p.draft.language=="zh-Hans"&&p.draft.formattingLocale=="vi-VN");
    unsigned saves=0;auto save=[&](const LanguagePreferences &,std::string *){++saves;return true;};p.key('v');CHECK(p.page==LanguageSettingsPage::Review&&saves==0);
    const auto review=p.render(120,100,ColorLevel::None);CHECK(review.find("Before:")!=review.npos&&review.find("machine-assisted draft")!=review.npos&&review.find("legacy English")!=review.npos);
    p.key('\r',[](const LanguagePreferences &,std::string *error){*error="fixture save refused";return false;});CHECK(p.dirty()&&p.active==original&&p.message=="fixture save refused");
    p.key('\r',save);CHECK(saves==1&&!p.dirty()&&p.active.language=="zh-Hans"&&std::string(effectiveNativeMessageLanguage())=="en");
    p.field=0;p.key(' ');p.key(27);CHECK(p.page==LanguageSettingsPage::Leave);p.key(27);CHECK(p.dirty());p.key(27);CHECK(p.key('d')==LanguageSettingsResult::Close&&!p.visible&&!p.dirty());
    p.open({});CHECK(p.key(3)==LanguageSettingsResult::Quit&&p.key('q')==LanguageSettingsResult::Quit);
    for(int width:{20,40,80,120})for(int rows:{12,16,24}){p.open({});for(unsigned field=0;field<8;++field){p.field=field;const auto frame=p.render(width,rows,ColorLevel::None);CHECK(frame.find("> ")!=frame.npos&&std::count(frame.begin(),frame.end(),'\n')<rows);}
        p.key('v');for(unsigned i=0;i<100;++i)p.key(kKeyPageDown);const auto tail=p.render(width,rows,ColorLevel::None);CHECK(tail.find("[End language]")!=tail.npos&&std::count(tail.begin(),tail.end(),'\n')<rows);}
    SettingsPanel root;root.open({});root.draft.color=false;root.group=7;root.groupsFocused=false;CHECK(root.fields()==1&&root.key('\r',{})==SettingsResult::OpenLanguage);
    LanguagePreferencesStore store(dir/"language-independent.json");LanguageSettingsContext context;context.running=true;context.cliOverrides=true;context.requested.language="ar";
    openLanguageControls(&root,&store,context);CHECK(root.language.visible&&root.dirty()&&root.language.context.requested.language=="ar");root.language.key(' ');
    unsigned ui=0,language=0;const auto key=[&](int ch){return root.key(ch,[&](const UiPreferences &,std::string *){++ui;return true;},{},{},0,{},{},{},{},[&](const LanguagePreferences &value,std::string *e){++language;return store.save(value,e);});};
    key('v');key('\r');CHECK(language==1&&ui==0&&root.dirty()&&!root.language.dirty()&&root.language.context.requested.language=="ar");key(27);CHECK(!root.language.visible);openLanguageControls(&root,nullptr,{});CHECK(!root.language.visible&&!root.message.empty());
}
void cli(){
    const char *args[]={"fixture","--language=zh-Hans","--format-locale","en-US","--ascii-ui"};CliParse result;std::string error;
    CHECK(parseProductCli(5,args,&result,&error)&&result.rest.empty()&&result.ov.presentation.language=="zh-Hans"&&result.ov.presentation.formattingLocale=="en-US"&&result.ov.presentation.ascii);
    ProductConfig original,applied=original;CHECK(applyOverrides(result.ov,&applied,&error)&&applied.pools.size()==original.pools.size()&&applied.wallet==original.wallet&&applied.stateDir==original.stateDir);
    for(const auto &values:std::vector<std::vector<const char *>>{{"fixture","--language"},{"fixture","--language","private-invalid"},{"fixture","--language=en","--language=fr"},
        {"fixture","--format-locale="},{"fixture","--format-locale","private-invalid"},{"fixture","--format-locale=en","--format-locale=de"},
        {"fixture","--ascii-ui","--ascii-ui"},{"fixture","--ascii-ui=private-invalid"}}){const auto before=result.command;CHECK(!parseProductCli(static_cast<int>(values.size()),values.data(),&result,&error)&&result.command==before&&error.find("private-invalid")==error.npos);}
}
}
int main(){const auto dir=std::filesystem::path(makeScratchDir("language-settings"));parsing();storage(dir);panel(dir);cli();std::error_code ec;std::filesystem::remove_all(dir,ec);return finishTests("native independent language requests and explicit effective fallback (no ICU/mining)");}
