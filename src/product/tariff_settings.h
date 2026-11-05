#pragma once
#include "dashboard.h"
#include "tariff_store.h"
#include "tariff_notice_messages.h"
#include "tariff_panel_messages.h"
#include "tariff_hint_messages.h"
#include <functional>
namespace om {
enum class TariffSettingsResult { None, Close, Quit };
enum class TariffSettingsPage { Browse, Edit, Review, Leave, Remove };
class TariffSettingsPanel {
   public:
    using Save=std::function<bool(const JsonValue &,std::string *)>;
    bool visible=false,replaceInput=true,inputRejected=false;
    JsonValue active,draft;
    TariffSettingsPage page=TariffSettingsPage::Browse;
    std::vector<size_t> route;size_t field=0,reviewOffset=0;
    std::string input,message;std::vector<std::string> review;
    void open(JsonValue value){active=draft=std::move(value);route.clear();field=reviewOffset=0;visible=true;page=TariffSettingsPage::Browse;message.clear();review.clear();}
    bool dirty()const{return tariffJson(active)!=tariffJson(draft);}
    std::string path()const{
        const auto *v=&draft;std::string s;
        for(size_t i:route){if(v->isObject()){if(!s.empty())s+='.';s+=v->members[i].first;v=&v->members[i].second;}else{s+="["+std::to_string(i)+"]";v=&v->items[i];}}
        return s;
    }
    TariffSettingsResult key(int key,const Save &save,uint64_t now){
        using NoticeKey=i18n::TariffNoticeMessageKey;
        if(key==3)return TariffSettingsResult::Quit;
        if(page==TariffSettingsPage::Edit){
            if(key==27){page=TariffSettingsPage::Browse;message.clear();}
            else if(key=='\r'||key=='\n'){
                if(inputRejected){message=i18n::tariffNoticeEnglish(NoticeKey::InputRejected);return TariffSettingsResult::None;}
                auto *v=selected();if(!v)return TariffSettingsResult::None;
                const auto name=selectedName();
                if(input.empty()&&(name=="minimum_cycle_subtotal"||name=="through_kwh"||name=="date"))*v={};
                else if(v->isNumber()){
                    JsonValue number;if(!parseJsonStrict(input,&number,nullptr)||!number.isNumber()||input.find_first_not_of("-0123456789")!=std::string::npos){message=i18n::tariffNoticeEnglish(NoticeKey::IntegerOnly);return TariffSettingsResult::None;}
                    *v=std::move(number);
                }else{v->type=JsonValue::Type::String;v->text=input;}
                page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::DraftOnly);
            }else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;inputRejected=false;}
            else if(key>=32&&key<=126){if(replaceInput){input.clear();inputRejected=false;}replaceInput=false;if(input.size()<128)input+=static_cast<char>(key);else{inputRejected=true;message=i18n::tariffNoticeEnglish(NoticeKey::TooLong);}}
            else if(key>=128&&key<=255){inputRejected=true;message=i18n::tariffNoticeEnglish(NoticeKey::AsciiOnly);}
            return TariffSettingsResult::None;
        }
        if(key=='q'||key=='Q')return TariffSettingsResult::Quit;
        if(page==TariffSettingsPage::Remove){
            if(key==27)page=TariffSettingsPage::Browse;
            else if(key=='\r'||key=='\n'){auto *v=node();if(v->isArray()&&field<v->items.size()){v->items.erase(v->items.begin()+field);if(field&&field==v->items.size())--field;}page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::Removed);}
        }else if(page==TariffSettingsPage::Leave){
            if(key==27)page=TariffSettingsPage::Browse;
            else if(key=='d'||key=='D'){draft=active;visible=false;return TariffSettingsResult::Close;}
            else if(key=='\r'||key=='\n'||key==kKeyF2)beginReview();
        }else if(page==TariffSettingsPage::Review){
            if(key==27)page=TariffSettingsPage::Browse;
            else if(key==kKeyUp||key==kKeyPageUp)reviewOffset=reviewOffset?reviewOffset-1:0;
            else if(key==kKeyDown||key==kKeyPageDown)++reviewOffset;
            else if(key=='\r'||key=='\n'||key==kKeyF2){
                auto candidate=draft;for(auto &m:candidate.members)if(m.first=="confirmed_utc"){m.second.type=JsonValue::Type::Number;m.second.text=std::to_string(now);}
                TariffDeclaration checked;TariffProblem error;
                if(!decodeTariffDeclaration(candidate,&checked,&error)){message=error.path+": "+error.message;focus(error.path);page=TariffSettingsPage::Browse;}
                else if(save&&save(candidate,&message)){active=draft=std::move(candidate);page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::SavedOffline);}
                else if(message.empty())message=i18n::tariffNoticeEnglish(NoticeKey::SaveUnavailable);
            }
        }else if(key==27){
            if(!route.empty()){field=route.back();route.pop_back();}
            else if(dirty())page=TariffSettingsPage::Leave;
            else{visible=false;return TariffSettingsResult::Close;}
        }else if(key=='v'||key=='V'||key==kKeyF2)beginReview();
        else if(key==kKeyDown||key=='\t'){const auto n=count();if(n)field=(field+1)%n;}
        else if(key==kKeyUp||key==kKeyBackTab){const auto n=count();if(n)field=(field+n-1)%n;}
        else if((key=='a'||key=='A')&&node()->isArray()){
            const auto collection=collectionName();const size_t bound=collection=="revisions"?16:collection=="dynamic"?256:32;
            if(count()>=bound||tariffJson(draft).size()>tariffDeclarationMaxBytes-1024)message=i18n::tariffNoticeEnglish(NoticeKey::Capacity);
            else{auto value=tariffRowTemplate(collection);if(value.isObject()){node()->items.push_back(std::move(value));field=count()-1;message=i18n::tariffNoticeEnglish(NoticeKey::AddedBlank);}}
        }else if((key=='x'||key=='X')&&node()->isArray()&&count())page=TariffSettingsPage::Remove;
        else if(key=='\r'||key=='\n'||key==' '){
            auto *v=selected();if(!v)return TariffSettingsResult::None;
            if(v->isObject()||v->isArray()){route.push_back(field);field=0;message.clear();}
            else if(readOnly(selectedName()))message=i18n::tariffNoticeEnglish(NoticeKey::FixedContract);
            else if(v->isBool())v->boolean=!v->boolean;
            else{input=v->type==JsonValue::Type::Null?"":v->text;replaceInput=true;inputRejected=false;page=TariffSettingsPage::Edit;message.clear();}
        }
        return TariffSettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        using PanelKey=i18n::TariffPanelMessageKey;
        if(rows>0&&rows<12)return i18n::tariffPanelEnglish(PanelKey::ShortTitle)+"\n"+i18n::tariffPanelEnglish(PanelKey::ShortResize)+"\n"+i18n::tariffPanelEnglish(PanelKey::ShortKeys)+"\n";
        const auto footer=wrapText(i18n::tariffPanelEnglish(page==TariffSettingsPage::Edit?PanelKey::FooterEdit:page==TariffSettingsPage::Review?PanelKey::FooterReview:PanelKey::FooterBrowse),width);
        auto notice=wrapText(message.empty()?i18n::tariffPanelEnglish(PanelKey::OfflineDefault):message,width);
        // Reserve focus/editor space even for multi-line validation messages on a 60x12 terminal.
        if(notice.size()>3)notice.resize(3);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footer.size()+notice.size())-1)):10000;
        std::vector<std::string> body;
        if(page==TariffSettingsPage::Edit){body={"Edit "+selectedName(),"> "+input+"_",hint(selectedName()),i18n::tariffPanelEnglish(PanelKey::EditorKeys)};}
        else if(page==TariffSettingsPage::Review)body=review;
        else if(page==TariffSettingsPage::Leave)body={i18n::tariffPanelEnglish(PanelKey::LeaveTitle),i18n::tariffPanelEnglish(PanelKey::LeaveKeys)};
        else if(page==TariffSettingsPage::Remove)body={"Remove draft row "+std::to_string(field)+"?",i18n::tariffPanelEnglish(PanelKey::RemoveKeys),i18n::tariffPanelEnglish(PanelKey::RemovePreserve)};
        else{
            // Focus first: path is secondary so deep nesting cannot push the selected control off screen.
            const auto *v=node();const auto n=count();
            if(!n)body.push_back(i18n::tariffPanelEnglish(PanelKey::EmptyTable));
            for(size_t i=field;i<n&&i<field+6;++i){const auto &value=v->isObject()?v->members[i].second:v->items[i];const auto name=v->isObject()?v->members[i].first:"row "+std::to_string(i);
                body.push_back(std::string(i==field?"> ":"  ")+name+": "+(value.isObject()?i18n::tariffPanelEnglish(PanelKey::FieldsPrompt):value.isArray()?"[Enter] "+std::to_string(value.items.size())+" rows":tariffJson(value)+(readOnly(name)?i18n::tariffPanelEnglish(PanelKey::FixedSuffix):std::string{})));}
            body.push_back("Path: "+(path().empty()?"declaration":path()));
            body.push_back("Field "+std::to_string(n?field+1:0)+"/"+std::to_string(n));
        }
        std::vector<std::string> lines;for(const auto &s:body)for(const auto &part:wrapText(s,width))lines.push_back(part);
        if(page==TariffSettingsPage::Review&&lines.size()>budget){const auto offset=std::min(reviewOffset,lines.size()-budget);lines.erase(lines.begin(),lines.begin()+offset);}
        if(lines.size()>budget)lines.resize(budget);
        std::string text=paint(i18n::tariffPanelEnglish(PanelKey::Title)+(dirty()?i18n::tariffPanelEnglish(PanelKey::UnsavedSuffix):std::string{}),Tone::Canary,color)+"\n";
        for(const auto &s:lines)text+=s+"\n";
        for(const auto &s:notice)text+=s+"\n";
        for(const auto &s:footer)text+=s+"\n";
        return text;
    }
   private:
    JsonValue *node(){auto *v=&draft;for(size_t i:route)v=v->isObject()?&v->members[i].second:&v->items[i];return v;}
    const JsonValue *node()const{return const_cast<TariffSettingsPanel *>(this)->node();}
    size_t count()const{const auto *v=node();return v->isObject()?v->members.size():v->items.size();}
    JsonValue *selected(){if(field>=count())return nullptr;auto *v=node();return v->isObject()?&v->members[field].second:&v->items[field];}
    std::string selectedName()const{if(field>=count())return "";const auto *v=node();return v->isObject()?v->members[field].first:"row "+std::to_string(field);}
    std::string collectionName()const{const auto s=path();const auto at=s.rfind('.');return at==std::string::npos?s:s.substr(at+1);}
    static bool readOnly(const std::string &s){return s=="schema_version"||s=="confirmed_utc"||s=="reset_policy"||s=="rounding"||s=="basis"||s=="origin"||s=="time_database_version";}
    static std::string hint(const std::string &s){
        using HintKey=i18n::TariffHintMessageKey;
        if(s=="tiers")return i18n::tariffHintEnglish(HintKey::Tiers);
        if(s=="time_mode")return i18n::tariffHintEnglish(HintKey::TimeMode);
        if(s=="kind")return i18n::tariffHintEnglish(HintKey::ClockKind);
        if(s=="date")return i18n::tariffHintEnglish(HintKey::Date);
        if(s=="minimum_cycle_subtotal"||s=="through_kwh")return i18n::tariffHintEnglish(HintKey::OptionalDecimal);
        if(s.find("utc_ms")!=std::string::npos)return i18n::tariffHintEnglish(HintKey::UtcMs);
        if(s=="imported_kwh"||s=="accrued_subtotal")return i18n::tariffHintEnglish(HintKey::Opening);
        if(s.find("mask")!=std::string::npos)return i18n::tariffHintEnglish(HintKey::Mask);
        if(s=="fixed_offset_seconds")return i18n::tariffHintEnglish(HintKey::FixedOffset);
        if(s.find("minute")!=std::string::npos)return i18n::tariffHintEnglish(HintKey::Minute);
        return i18n::tariffHintEnglish(HintKey::Default);
    }
    bool findPath(JsonValue &v,const std::string &current,const std::string &wanted,std::vector<size_t> &found,size_t &selectedField){
        if(!v.isObject()&&!v.isArray())return false;
        const size_t n=v.isObject()?v.members.size():v.items.size();
        for(size_t i=0;i<n;++i){const auto next=v.isObject()?(current.empty()?"":current+".")+v.members[i].first:current+"["+std::to_string(i)+"]";
            if(next==wanted){selectedField=i;return true;}
            found.push_back(i);auto &child=v.isObject()?v.members[i].second:v.items[i];if(findPath(child,next,wanted,found,selectedField))return true;found.pop_back();}
        return false;
    }
    void focus(const std::string &wanted){std::vector<size_t> found;size_t f=0;if(findPath(draft,"",wanted,found,f)){route=std::move(found);field=f;}}
    void beginReview(){
        using PanelKey=i18n::TariffPanelMessageKey;
        review={i18n::tariffPanelEnglish(PanelKey::ReviewHeading),i18n::tariffPanelEnglish(PanelKey::NoActivation),
            i18n::tariffPanelEnglish(PanelKey::DeclaredForecast),i18n::tariffPanelEnglish(PanelKey::ResetLimit),
            i18n::tariffPanelEnglish(PanelKey::Excludes),i18n::tariffPanelEnglish(PanelKey::NoZeroFill),
            i18n::tariffPanelEnglish(PanelKey::FullCycle),i18n::tariffPanelEnglish(PanelKey::ChangesHeading)};
        const auto diff=tariffDeclarationChanges(active,draft);review.insert(review.end(),diff.begin(),diff.end());
        if(diff.empty())review.push_back(i18n::tariffPanelEnglish(PanelKey::NoChanges));
        const auto complete=tariffDeclarationChanges({},draft);review.insert(review.end(),complete.begin(),complete.end());
        review.push_back(i18n::tariffPanelEnglish(PanelKey::ConfirmationOnly));
        page=TariffSettingsPage::Review;reviewOffset=0;message.clear();
    }
};
} // namespace om
