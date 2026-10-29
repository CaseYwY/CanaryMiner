#pragma once
#include "dashboard.h"
#include "tariff_store.h"
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
        if(key==3)return TariffSettingsResult::Quit;
        if(page==TariffSettingsPage::Edit){
            if(key==27){page=TariffSettingsPage::Browse;message.clear();}
            else if(key=='\r'||key=='\n'){
                if(inputRejected){message="Input rejected; shorten/use ASCII or cancel. Nothing applied.";return TariffSettingsResult::None;}
                auto *v=selected();if(!v)return TariffSettingsResult::None;
                const auto name=selectedName();
                if(input.empty()&&(name=="minimum_cycle_subtotal"||name=="through_kwh"||name=="date"))*v={};
                else if(v->isNumber()){
                    JsonValue number;if(!parseJsonStrict(input,&number,nullptr)||!number.isNumber()||input.find_first_not_of("-0123456789")!=std::string::npos){message="Integer only, in the field's stated units.";return TariffSettingsResult::None;}
                    *v=std::move(number);
                }else{v->type=JsonValue::Type::String;v->text=input;}
                page=TariffSettingsPage::Browse;message="Draft only; V/F2 reviews and validates before Save.";
            }else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;inputRejected=false;}
            else if(key>=32&&key<=126){if(replaceInput){input.clear();inputRejected=false;}replaceInput=false;if(input.size()<128)input+=static_cast<char>(key);else{inputRejected=true;message="Maximum 128 characters; input was not truncated into a saved value.";}}
            else if(key>=128&&key<=255){inputRejected=true;message="ASCII editor only; non-ASCII input rejected.";}
            return TariffSettingsResult::None;
        }
        if(key=='q'||key=='Q')return TariffSettingsResult::Quit;
        if(page==TariffSettingsPage::Remove){
            if(key==27)page=TariffSettingsPage::Browse;
            else if(key=='\r'||key=='\n'){auto *v=node();if(v->isArray()&&field<v->items.size()){v->items.erase(v->items.begin()+field);if(field&&field==v->items.size())--field;}page=TariffSettingsPage::Browse;message="Removed from draft only; review before saving.";}
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
                else if(save&&save(candidate,&message)){active=draft=std::move(candidate);page=TariffSettingsPage::Browse;message="Saved offline declaration + opening ONLY. Runtime collectors and policies do not use it yet.";}
                else if(message.empty())message="Save unavailable; draft retained.";
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
            if(count()>=bound||tariffJson(draft).size()>tariffDeclarationMaxBytes-1024)message="Collection/document capacity reached.";
            else{auto value=tariffRowTemplate(collection);if(value.isObject()){node()->items.push_back(std::move(value));field=count()-1;message="Added blank row; unknown rates stay empty. Enter to edit.";}}
        }else if((key=='x'||key=='X')&&node()->isArray()&&count())page=TariffSettingsPage::Remove;
        else if(key=='\r'||key=='\n'||key==' '){
            auto *v=selected();if(!v)return TariffSettingsResult::None;
            if(v->isObject()||v->isArray()){route.push_back(field);field=0;message.clear();}
            else if(readOnly(selectedName()))message="Fixed schema/model contract. This field is not user-editable.";
            else if(v->isBool())v->boolean=!v->boolean;
            else{input=v->type==JsonValue::Type::Null?"":v->text;replaceInput=true;inputRejected=false;page=TariffSettingsPage::Edit;message.clear();}
        }
        return TariffSettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        using namespace dash_detail;const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        if(rows>0&&rows<12)return "Terminal too short.\nResize to 12+ rows.\n[Ctrl+C] Stop [Esc] Back\n";
        const auto footer=wrapText(page==TariffSettingsPage::Edit?"[Enter] Apply draft [Esc] Cancel edit [Ctrl+C] Stop":page==TariffSettingsPage::Review?"[Enter/F2] Save declaration [Arrows] Scroll [Esc] Edit [Q] Stop":"[Arrows/Tab] Field [Enter] Open/edit [A] Add row [X] Remove row [V/F2] Review [Esc] Back [Q] Stop",width);
        auto notice=wrapText(message.empty()?"OFFLINE DECLARATION / whole-meter scenario; no automatic reset or runtime activation.":message,width);
        // Reserve focus/editor space even for multi-line validation messages on a 60x12 terminal.
        if(notice.size()>3)notice.resize(3);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footer.size()+notice.size())-1)):10000;
        std::vector<std::string> body;
        if(page==TariffSettingsPage::Edit){body={"Edit "+selectedName(),"> "+input+"_",hint(selectedName()),"Q/D/L/S are text here; Ctrl+C stops."};}
        else if(page==TariffSettingsPage::Review)body=review;
        else if(page==TariffSettingsPage::Leave)body={"Unsaved tariff declaration","[Enter] Review [D] Discard [Esc] Continue editing"};
        else if(page==TariffSettingsPage::Remove)body={"Remove draft row "+std::to_string(field)+"?","[Enter] Remove from draft [Esc] Cancel","Saved declaration is preserved until reviewed Save."};
        else{
            // Focus first: path is secondary so deep nesting cannot push the selected control off screen.
            const auto *v=node();const auto n=count();
            if(!n)body.push_back("Empty table: [A] Add a blank row");
            for(size_t i=field;i<n&&i<field+6;++i){const auto &value=v->isObject()?v->members[i].second:v->items[i];const auto name=v->isObject()?v->members[i].first:"row "+std::to_string(i);
                body.push_back(std::string(i==field?"> ":"  ")+name+": "+(value.isObject()?"[Enter] fields":value.isArray()?"[Enter] "+std::to_string(value.items.size())+" rows":tariffJson(value)+(readOnly(name)?" [fixed]":"")));}
            body.push_back("Path: "+(path().empty()?"declaration":path()));
            body.push_back("Field "+std::to_string(n?field+1:0)+"/"+std::to_string(n));
        }
        std::vector<std::string> lines;for(const auto &s:body)for(const auto &part:wrapText(s,width))lines.push_back(part);
        if(page==TariffSettingsPage::Review&&lines.size()>budget){const auto offset=std::min(reviewOffset,lines.size()-budget);lines.erase(lines.begin(),lines.begin()+offset);}
        if(lines.size()>budget)lines.resize(budget);
        std::string text=paint("Combined tariff / declared opening"+std::string(dirty()?" *":""),Tone::Canary,color)+"\n";
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
        if(s=="tiers")return "none / progressive / all_units; additive shared-register bands.";
        if(s=="time_mode")return "constant / calendar / dynamic. Clear incompatible tables explicitly.";
        if(s=="kind")return "utc / fixed_standard (no DST or OS timezone inference).";
        if(s=="date")return "YYYY-MM-DD; clear for null (no exact-date condition).";
        if(s=="minimum_cycle_subtotal"||s=="through_kwh")return "Canonical decimal; clear for null. Final band MUST be unbounded/null.";
        if(s.find("utc_ms")!=std::string::npos)return "UTC Unix MILLISECONDS. Cycle/revision end is exclusive; no automatic now/reset.";
        if(s=="imported_kwh"||s=="accrued_subtotal")return "Explicit whole-meter opening; subtotal BEFORE multiplier. Missing history is not zero.";
        if(s.find("mask")!=std::string::npos)return "Bit mask: weekdays Monday bit 0 (1..127); months January bit 0 (1..4095).";
        if(s=="fixed_offset_seconds")return "Whole-minute STANDARD offset in seconds, +/-64800. UTC requires 0.";
        if(s.find("minute")!=std::string::npos)return "Local minute 0..1440, begin < end. Split overnight windows.";
        return "Explicit local declaration. Decimal strings: no exponent; zero must be entered. No account/credentials.";
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
        review={"Review tariff-declaration.json ONLY","Saved declaration does NOT activate the runtime energy owner or an optimizer.",
            "Whole-meter DECLARED FORECAST opening; source reference is an unverified local claim.",
            "Reset: explicit single cycle only, <=370 elapsed days. Rates do not reset shared tiers.",
            "EXCLUDES demand/export/prepaid/independent resets/proration/ordered taxes/provider rounding.",
            "No missing household usage, future prices or unobserved load is filled with zero.",
            "Minimum/all-units need full remaining cycle. USD conversion is not inferred.",
            "Changes (old -> draft); unchanged fields follow:"};
        const auto diff=tariffDeclarationChanges(active,draft);review.insert(review.end(),diff.begin(),diff.end());
        if(diff.empty())review.push_back("No value changes.");
        const auto complete=tariffDeclarationChanges({},draft);review.insert(review.end(),complete.begin(),complete.end());
        review.push_back("Save stamps local confirmation time only; not supplier approval or freshness renewal.");
        page=TariffSettingsPage::Review;reviewOffset=0;message.clear();
    }
};
} // namespace om
