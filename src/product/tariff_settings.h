#pragma once
#include "dashboard.h"
#include "tariff_store.h"
#include "tariff_notice_messages.h"
#include "tariff_panel_messages.h"
#include "tariff_hint_messages.h"
#include "tariff_context_messages.h"
#include "tariff_value_messages.h"
#include "tariff_review_messages.h"
#include "tariff_diagnostic_source.h"
#include "tariff_error_messages.h"
#include "tariff_comparison_settings.h"
#include "tariff_monthly_messages.h"
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
    void open(JsonValue value){active=draft=std::move(value);route.clear();field=reviewOffset=0;visible=true;page=TariffSettingsPage::Browse;message.clear();review.clear();comparison_.close();}
    void stopComparison(){comparison_.requestStop();}
    bool dirty()const{return tariffJson(active)!=tariffJson(draft);}
    std::string path()const{
        const auto *v=&draft;std::string s;
        for(size_t i:route){if(v->isObject()){if(!s.empty())s+='.';s+=v->members[i].first;v=&v->members[i].second;}else{s+="["+std::to_string(i)+"]";v=&v->items[i];}}
        return s;
    }
    TariffSettingsResult key(int key,const Save &save,uint64_t now){
        using NoticeKey=i18n::TariffNoticeMessageKey;
        if(key==3){stopComparison();return TariffSettingsResult::Quit;}
        if(comparison_.visible) {
            if(key=='q'||key=='Q'){stopComparison();return TariffSettingsResult::Quit;}
            comparison_.key(key,now);
            return TariffSettingsResult::None;
        }
        if(page==TariffSettingsPage::Edit){
            if(key==27){page=TariffSettingsPage::Browse;message.clear();}
            else if(key=='\r'||key=='\n'){
                if(inputRejected){message=i18n::tariffNoticeEnglish(NoticeKey::InputRejected);return TariffSettingsResult::None;}
                auto *v=selected();if(!v)return TariffSettingsResult::None;
                const auto name=selectedName();
                const auto location=path();
                const bool changed=v->text!=input;
                const bool seasonalOpening=seasonal()&&location=="opening"&&changed;
                const bool changedOpeningPoint=seasonalOpening&&(name=="through_utc_ms"||name=="cycle_begin_utc_ms");
                const bool changedAllowance=changed&&(seasonRow()||(seasonalOpening&&name=="cycle_end_utc_ms"));
                const bool changedRateClock=changed&&location=="plan.clock"&&
                    (name=="zone"||(seasonal()&&(name=="kind"||name=="fixed_offset_seconds")));
                if(input.empty()&&(name=="minimum_cycle_subtotal"||name=="through_kwh"||name=="date"))*v={};
                else if(v->isNumber()){
                    JsonValue number;if(!parseJsonStrict(input,&number,nullptr)||!number.isNumber()||input.find_first_not_of("-0123456789")!=std::string::npos){message=i18n::tariffNoticeEnglish(NoticeKey::IntegerOnly);return TariffSettingsResult::None;}
                    *v=std::move(number);
                }else{v->type=JsonValue::Type::String;v->text=input;}
                page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::DraftOnly);
                if(changedOpeningPoint){
                    clearDraftOpeningAmounts();
                    message=i18n::tariffMonthlyEnglish(i18n::TariffMonthlyMessageKey::SeasonalOpeningPoint);
                }else if(changedRateClock&&clearMidcycleSubtotal())
                    message="Rate clock changed. Re-enter the opening subtotal using the new clock.";
                else if(changedAllowance)reviewChangedSeasonalAllowance();
            }else if(key==8||key==127){if(replaceInput)input.clear();else if(!input.empty())input.pop_back();replaceInput=false;inputRejected=false;}
            else if(key>=32&&key<=126){if(replaceInput){input.clear();inputRejected=false;}replaceInput=false;if(input.size()<128)input+=static_cast<char>(key);else{inputRejected=true;message=i18n::tariffNoticeEnglish(NoticeKey::TooLong);}}
            else if(key>=128&&key<=255){inputRejected=true;message=i18n::tariffNoticeEnglish(NoticeKey::AsciiOnly);}
            return TariffSettingsResult::None;
        }
        if(key=='q'||key=='Q')return TariffSettingsResult::Quit;
        if(page==TariffSettingsPage::Remove){
            if(key==27)page=TariffSettingsPage::Browse;
            else if(key=='\r'||key=='\n'){
                auto *v=node();
                if(v->isArray()&&field<v->items.size()){
                    v->items.erase(v->items.begin()+field);
                    if(field&&field==v->items.size())--field;
                }
                page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::Removed);
                if(collectionName()=="seasons")reviewChangedSeasonalAllowance();
            }
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
                if(!decodeTariffDeclaration(candidate,&checked,&error)){
                    showProblem(error);page=TariffSettingsPage::Browse;
                }
                else if(save&&save(candidate,&message)){active=draft=std::move(candidate);page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::SavedOffline);}
                else if(message.empty())message=i18n::tariffNoticeEnglish(NoticeKey::SaveUnavailable);
                else message=i18n::tariffErrorEnglish(i18n::TariffErrorMessageKey::Raw,{{"reason",message}});
            }
        }else if(key==27){
            if(!route.empty()){field=route.back();route.pop_back();}
            else if(dirty())page=TariffSettingsPage::Leave;
            else{visible=false;return TariffSettingsResult::Close;}
        }else if(key=='v'||key=='V'||key==kKeyF2)beginReview();
        else if(key=='p'||key=='P')comparison_.open(active);
        else if((key=='m'||key=='M')&&onCyclePolicy())toggleCyclePolicy(TariffResetCadence::Monthly);
        else if((key=='y'||key=='Y')&&onCyclePolicy())toggleCyclePolicy(TariffResetCadence::Annual);
        else if((key=='b'||key=='B')&&onCyclePolicy())toggleCyclePolicy(TariffResetCadence::Bimonthly);
        else if((key=='a'||key=='A')&&onSeasonalToggle())toggleSeasonalAllowance();
        else if((key=='f'||key=='F')&&onCyclePolicy()&&recurring()&&!bimonthly())toggleDailyFeeAllocation();
        else if((key=='z'||key=='Z')&&onTariffClock())toggleNamedClock();
        else if((key=='g'||key=='G')&&recurring())deriveCycle();
        else if(key==kKeyDown||key=='\t'){const auto n=count();if(n)field=(field+1)%n;}
        else if(key==kKeyUp||key==kKeyBackTab){const auto n=count();if(n)field=(field+n-1)%n;}
        else if((key=='a'||key=='A')&&node()->isArray()){
            const auto collection=collectionName();const size_t bound=collection=="revisions"||collection=="seasons"?16:collection=="dynamic"?256:32;
            if(count()>=bound||tariffJson(draft).size()>tariffDeclarationMaxBytes-1024)message=i18n::tariffNoticeEnglish(NoticeKey::Capacity);
            else{auto value=tariffRowTemplate(collection);if(value.isObject()){node()->items.push_back(std::move(value));field=count()-1;message=i18n::tariffNoticeEnglish(NoticeKey::AddedBlank);if(collection=="seasons")reviewChangedSeasonalAllowance();}}
        }else if((key=='x'||key=='X')&&node()->isArray()&&count())page=TariffSettingsPage::Remove;
        else if(key=='\r'||key=='\n'||key==' '){
            auto *v=selected();if(!v)return TariffSettingsResult::None;
            if(v->isObject()||v->isArray()){route.push_back(field);field=0;message.clear();}
            else if(path()=="plan"&&selectedName()=="rounding")toggleMeterRounding(*v);
            else if(readOnly(selectedName()))message=i18n::tariffNoticeEnglish(NoticeKey::FixedContract);
            else if(v->isBool())v->boolean=!v->boolean;
            else{input=v->type==JsonValue::Type::Null?"":v->text;replaceInput=true;inputRejected=false;page=TariffSettingsPage::Edit;message.clear();}
        }
        return TariffSettingsResult::None;
    }
    std::string render(int columns,int rows,ColorLevel color)const{
        if(comparison_.visible)return comparison_.render(columns,rows,color);
        using namespace dash_detail;const size_t width=static_cast<size_t>(std::max(20,columns)-1);
        using PanelKey=i18n::TariffPanelMessageKey;
        using ContextKey=i18n::TariffContextMessageKey;
        if(rows>0&&rows<12)return i18n::tariffPanelEnglish(PanelKey::ShortTitle)+"\n"+i18n::tariffPanelEnglish(PanelKey::ShortResize)+"\n"+i18n::tariffPanelEnglish(PanelKey::ShortKeys)+"\n";
        std::string shortcuts;
        if(page==TariffSettingsPage::Browse){
            using Key=i18n::TariffMonthlyMessageKey;
            if(onCyclePolicy()){
                shortcuts+=i18n::tariffMonthlyEnglish(Key::Toggle)+" "+i18n::tariffMonthlyEnglish(Key::AnnualToggle)+" "+
                    i18n::tariffMonthlyEnglish(Key::BimonthlyToggle)+" ";
                if(recurring()&&!bimonthly())shortcuts+=i18n::tariffMonthlyEnglish(Key::FeeToggle)+" ";
            }
            if(onSeasonalToggle())shortcuts+=i18n::tariffMonthlyEnglish(Key::SeasonalToggle)+" ";
            if(recurring())shortcuts+=i18n::tariffMonthlyEnglish(Key::Derive)+" ";
            if(onTariffClock()&&civilNamedTimeAvailable())shortcuts+="[Z] Named/UTC clock ";
            shortcuts+=i18n::tariffComparisonEnglish(i18n::TariffComparisonMessageKey::Hint)+" ";
        }
        const auto footer=wrapText(shortcuts+i18n::tariffPanelEnglish(page==TariffSettingsPage::Edit?PanelKey::FooterEdit:page==TariffSettingsPage::Review?PanelKey::FooterReview:PanelKey::FooterBrowse),width);
        auto notice=wrapText(message.empty()?i18n::tariffPanelEnglish(PanelKey::OfflineDefault):message,width);
        // Reserve focus/editor space even for multi-line validation messages on a 60x12 terminal.
        if(notice.size()>3)notice.resize(3);
        const size_t budget=rows>0?static_cast<size_t>(std::max(1,rows-static_cast<int>(footer.size()+notice.size())-1)):10000;
        std::vector<std::string> body;
        if(page==TariffSettingsPage::Edit){const auto heading=field>=count()?i18n::tariffContextEnglish(ContextKey::EditBlank):node()->isObject()?
            i18n::tariffContextEnglish(ContextKey::EditField,{{"field",selectedName()}}):i18n::tariffContextEnglish(ContextKey::EditRow,{{"position",std::to_string(field)}});
            body={heading,i18n::tariffContextEnglish(ContextKey::EditInput,{{"input",input}}),hint(selectedName()),i18n::tariffPanelEnglish(PanelKey::EditorKeys)};}
        else if(page==TariffSettingsPage::Review)body=review;
        else if(page==TariffSettingsPage::Leave)body={i18n::tariffPanelEnglish(PanelKey::LeaveTitle),i18n::tariffPanelEnglish(PanelKey::LeaveKeys)};
        else if(page==TariffSettingsPage::Remove)body={i18n::tariffContextEnglish(ContextKey::RemoveRow,{{"position",std::to_string(field)}}),i18n::tariffPanelEnglish(PanelKey::RemoveKeys),i18n::tariffPanelEnglish(PanelKey::RemovePreserve)};
        else{
            // Focus first: path is secondary so deep nesting cannot push the selected control off screen.
            const auto *v=node();const auto n=count();
            if(!n)body.push_back(i18n::tariffPanelEnglish(PanelKey::EmptyTable));
            for(size_t i=field;i<n&&i<field+6;++i){using ValueKey=i18n::TariffValueMessageKey;const bool object=v->isObject();const auto &value=object?v->members[i].second:v->items[i];
                i18n::MessageArguments arguments=object?i18n::MessageArguments{{"field",v->members[i].first}}:i18n::MessageArguments{{"position",std::to_string(i)}};ValueKey key;
                if(value.isObject())key=object?ValueKey::FieldFields:ValueKey::RowFields;
                else if(value.isArray()){key=object?ValueKey::FieldRows:ValueKey::RowRows;arguments.emplace_back("count",std::to_string(value.items.size()));}
                else{key=object?(readOnly(v->members[i].first)?ValueKey::FieldJsonFixed:ValueKey::FieldJson):ValueKey::RowJson;arguments.emplace_back("value",tariffJson(value));}
                body.push_back(std::string(i==field?"> ":"  ")+i18n::tariffValueEnglish(key,arguments));}
            const auto routePath=path();body.push_back(routePath.empty()?i18n::tariffContextEnglish(ContextKey::RootPath):i18n::tariffContextEnglish(ContextKey::Path,{{"path",routePath}}));
            body.push_back(i18n::tariffContextEnglish(ContextKey::FieldPosition,{{"position",std::to_string(n?field+1:0)},{"count",std::to_string(n)}}));
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
    TariffComparisonSettings comparison_;
    JsonValue *node(){auto *v=&draft;for(size_t i:route)v=v->isObject()?&v->members[i].second:&v->items[i];return v;}
    const JsonValue *node()const{return const_cast<TariffSettingsPanel *>(this)->node();}
    size_t count()const{const auto *v=node();return v->isObject()?v->members.size():v->items.size();}
    JsonValue *selected(){if(field>=count())return nullptr;auto *v=node();return v->isObject()?&v->members[field].second:&v->items[field];}
    std::string selectedName()const{if(field>=count())return "";const auto *v=node();return v->isObject()?v->members[field].first:i18n::tariffContextEnglish(i18n::TariffContextMessageKey::RowName,{{"position",std::to_string(field)}});}
    std::string collectionName()const{const auto s=path();const auto at=s.rfind('.');return at==std::string::npos?s:s.substr(at+1);}
    bool recurring()const{const auto *policy=draft.find("reset_policy");return policy&&policy->isObject();}
    TariffResetCadence cadence()const{
        const auto *policy=draft.find("reset_policy");
        if(policy&&policy->find("bimonthly_start_month"))return TariffResetCadence::Bimonthly;
        if(policy&&policy->find("annual_month"))return TariffResetCadence::Annual;
        return TariffResetCadence::Monthly;
    }
    bool annual()const{return cadence()==TariffResetCadence::Annual;}
    bool bimonthly()const{return cadence()==TariffResetCadence::Bimonthly;}
    bool onCyclePolicy()const{return route.empty()&&selectedName()=="reset_policy";}
    bool seasonal()const{return draft.find("seasonal_allowance")!=nullptr;}
    bool onSeasonalToggle()const{return onCyclePolicy()||(route.empty()&&selectedName()=="seasonal_allowance");}
    bool seasonRow()const{return path().rfind("seasonal_allowance.seasons[",0)==0;}
    bool onTariffClock()const{
        const auto at=path();
        return selectedName()=="clock"&&(at=="plan"||at=="reset_policy");
    }
    static bool named(const JsonValue *clock){
        const auto *kind=clock?clock->find("kind"):nullptr;
        return kind&&kind->text=="named_iana";
    }
    bool readOnly(const std::string &s)const{
        if(s=="kind"&&named(node()))return true;
        if(s=="rounding")return path()!="plan";
        if(s=="cycle_begin_utc_ms"||s=="cycle_end_utc_ms")return recurring();
        return s=="schema_version"||s=="confirmed_utc"||s=="reset_policy"||s=="daily_fee_allocation"||s=="basis"||s=="origin"||s=="time_database_version";
    }
    static JsonValue *member(JsonValue &object,const std::string &name){
        for(auto &m:object.members)if(m.first==name)return &m.second;
        return nullptr;
    }
    void refreshClockContract(){
        auto *schema=member(draft,"schema_version");
        auto *opening=member(draft,"opening");
        const auto *plan=draft.find("plan");
        const auto *policy=draft.find("reset_policy");
        if(!schema||!opening||!plan||!policy)return;
        const bool namedRate=named(plan->find("clock"));
        const bool namedReset=named(policy->find("clock"));
        schema->text=seasonal()?"7":bimonthly()?"6":annual()?"5":namedRate||namedReset?"4":!recurring()?"1":
            policy->find("daily_fee_allocation")?"3":"2";
        if(auto *version=member(*opening,"time_database_version"))
            version->text=namedRate?tariffTzdbVersion:"explicit-fixed-clock";
    }
    void toggleNamedClock(){
        if(!civilNamedTimeAvailable()){
            message="Reviewed tariff time data unavailable.";
            return;
        }
        auto *clock=selected();
        if(!clock||!clock->isObject())return;
        const bool rate=path()=="plan";
        if(named(clock)){
            parseJsonStrict(R"({"kind":"utc","fixed_offset_seconds":0})",clock,nullptr);
        }else{
            parseJsonStrict(R"({"kind":"named_iana","zone":"","time_database_version":""})",clock,nullptr);
            member(*clock,"time_database_version")->text=tariffTzdbVersion;
        }
        refreshClockContract();
        if(rate&&clearMidcycleSubtotal()){
            message="Rate clock changed. Re-enter the opening subtotal using the new clock.";
        }else{
            message=rate?"Draft rate clock changed. Review the zone and opening.":"Draft reset clock changed. Derive and review the billing cycle.";
        }
    }
    void toggleCyclePolicy(TariffResetCadence selectedCadence){
        using Key=i18n::TariffMonthlyMessageKey;
        auto *policy=selected();
        const auto *plan=draft.find("plan");
        const auto *clock=plan?plan->find("clock"):nullptr;
        if(!policy||!clock)return;
        const auto previousCadence=cadence();
        const bool useAnnual=selectedCadence==TariffResetCadence::Annual;
        const bool useBimonthly=selectedCadence==TariffResetCadence::Bimonthly;
        if(useBimonthly&&seasonal()){
            message=i18n::tariffMonthlyEnglish(Key::SeasonalConflict);
            return;
        }
        const bool wasBimonthly=previousCadence==TariffResetCadence::Bimonthly;
        const bool wasAnnual=previousCadence==TariffResetCadence::Annual;
        if(recurring()&&previousCadence==selectedCadence){
            const bool hadAllocation=policy->find("daily_fee_allocation")!=nullptr;
            *policy=JsonValue{};
            policy->type=JsonValue::Type::String;
            policy->text="explicit_cycle_only";
            refreshClockContract();
            if(wasBimonthly)reviewChangedPeriod(Key::BimonthlyChanged);
            else if(wasAnnual)reviewChangedPeriod(Key::AnnualChanged);
            else if(hadAllocation)reviewChangedDailyFeeRule();
            else message=i18n::tariffNoticeEnglish(i18n::TariffNoticeMessageKey::DraftOnly);
            return;
        }
        JsonValue day;
        day.type=JsonValue::Type::Number;
        day.text="0";
        const auto *oldDay=policy->find(wasAnnual?"annual_day":wasBimonthly?"bimonthly_day":"monthly_day");
        if(oldDay)day=*oldDay;
        const auto *oldClock=policy->find("clock");
        JsonValue replacement;
        replacement.type=JsonValue::Type::Object;
        if(useAnnual||useBimonthly){
            JsonValue month;
            month.type=JsonValue::Type::Number;
            month.text="0";
            replacement.members.emplace_back(useAnnual?"annual_month":"bimonthly_start_month",std::move(month));
        }
        replacement.members.emplace_back(useAnnual?"annual_day":useBimonthly?"bimonthly_day":"monthly_day",std::move(day));
        replacement.members.emplace_back("clock",oldClock?*oldClock:*clock);
        const auto *allocation=policy->find("daily_fee_allocation");
        if(!useBimonthly&&allocation)replacement.members.emplace_back("daily_fee_allocation",*allocation);
        *policy=std::move(replacement);
        refreshClockContract();
        if(useBimonthly)reviewChangedPeriod(Key::BimonthlyEnabled);
        else if(wasBimonthly)reviewChangedPeriod(Key::BimonthlyChanged);
        else if(wasAnnual||useAnnual)reviewChangedPeriod(useAnnual?Key::AnnualEnabled:Key::AnnualChanged);
        else message=i18n::tariffMonthlyEnglish(Key::Enabled);
    }
    void toggleSeasonalAllowance(){
        using Key=i18n::TariffMonthlyMessageKey;
        if(bimonthly()){
            message=i18n::tariffMonthlyEnglish(Key::SeasonalConflict);
            return;
        }
        if(seasonal()){
            const auto existing=std::find_if(draft.members.begin(),draft.members.end(),[](const auto &entry){
                return entry.first=="seasonal_allowance";
            });
            draft.members.erase(existing);
            focus("reset_policy");
            message=i18n::tariffNoticeEnglish(i18n::TariffNoticeMessageKey::DraftOnly);
        }else{
            JsonValue allowance;
            parseJsonStrict(R"({"published_utc_ms":0,"seasons":[]})",&allowance,nullptr);
            draft.members.emplace_back("seasonal_allowance",std::move(allowance));
            focus("seasonal_allowance");
            message=i18n::tariffMonthlyEnglish(Key::SeasonalEnabled);
        }
        refreshClockContract();
        reviewChangedSeasonalAllowance();
    }
    void reviewChangedSeasonalAllowance(){
        if(clearMidcycleSubtotal())message=i18n::tariffMonthlyEnglish(i18n::TariffMonthlyMessageKey::SeasonalOpening);
    }
    void clearDraftOpeningAmounts(){
        if(auto *opening=member(draft,"opening")){
            for(const auto *amount:{"imported_kwh","accrued_subtotal"})
                if(auto *value=member(*opening,amount))value->text.clear();
        }
    }
    void reviewChangedPeriod(i18n::TariffMonthlyMessageKey notice){
        clearDraftOpeningAmounts();
        message=i18n::tariffMonthlyEnglish(notice);
    }
    void toggleDailyFeeAllocation(){
        auto *policy=selected();
        if(!policy||!policy->isObject())return;
        const auto allocation=std::find_if(policy->members.begin(),policy->members.end(),[](const auto &entry){
            return entry.first=="daily_fee_allocation";
        });
        if(allocation==policy->members.end()){
            JsonValue value;
            value.type=JsonValue::Type::String;
            value.text="prorate_by_cycle_minutes";
            policy->members.emplace_back("daily_fee_allocation",std::move(value));
        }else{
            policy->members.erase(allocation);
        }
        refreshClockContract();
        reviewChangedDailyFeeRule();
    }
    bool clearMidcycleSubtotal(){
        auto *opening=member(draft,"opening");
        const auto *begin=opening?opening->find("cycle_begin_utc_ms"):nullptr;
        const auto *through=opening?opening->find("through_utc_ms"):nullptr;
        auto *subtotal=opening?member(*opening,"accrued_subtotal"):nullptr;
        if(!begin||!through||!subtotal||begin->text==through->text)return false;
        subtotal->text.clear();
        return true;
    }
    void toggleMeterRounding(JsonValue &rounding){
        rounding.text=rounding.text==meterHalfHourRounding?finalDisplayRounding:meterHalfHourRounding;
        if(clearMidcycleSubtotal()){
            focus("opening.accrued_subtotal");
            message="Rounding changed. Re-enter the opening subtotal.";
        }else{
            message=rounding.text==meterHalfHourRounding
                ?"Half-hour rounding requires complete cumulative meter readings."
                :"Draft uses display-only rounding.";
        }
    }
    void reviewChangedDailyFeeRule(){
        if(clearMidcycleSubtotal())message=i18n::tariffMonthlyEnglish(i18n::TariffMonthlyMessageKey::FeeOpeningRequired);
        else message=i18n::tariffNoticeEnglish(i18n::TariffNoticeMessageKey::DraftOnly);
    }
    void deriveCycle(){
        using Key=i18n::TariffMonthlyMessageKey;
        auto *opening=member(draft,"opening");
        if(!opening)return;
        auto *begin=member(*opening,"cycle_begin_utc_ms");
        auto *end=member(*opening,"cycle_end_utc_ms");
        const auto *through=opening->find("through_utc_ms");
        const auto *quantity=opening->find("imported_kwh");
        const auto *subtotal=opening->find("accrued_subtotal");
        if(!begin||!end||!through||!quantity||!subtotal)return;
        TariffCycleBounds bounds;
        TariffProblem error;
        if(!deriveTariffRecurringCycle(*draft.find("reset_policy"),*through,bounds,&error)){
            showProblem(error);
            return;
        }
        auto beginText=std::to_string(bounds.beginUtcMs);
        if(begin->text!=beginText&&(!quantity->text.empty()||!subtotal->text.empty())){
            focus(quantity->text.empty()?"opening.accrued_subtotal":"opening.imported_kwh");
            message=i18n::tariffMonthlyEnglish(Key::RetainAmounts);
            return;
        }
        auto endText=std::to_string(bounds.endUtcMs);
        const auto *policy=draft.find("reset_policy");
        const bool proratedEndChanged=end->text!=endText&&policy->find("daily_fee_allocation");
        const bool seasonalEndChanged=end->text!=endText&&seasonal();
        begin->text=std::move(beginText);
        end->text=std::move(endText);
        if(seasonalEndChanged&&clearMidcycleSubtotal()){
            focus("opening.accrued_subtotal");
            message=i18n::tariffMonthlyEnglish(Key::SeasonalOpening);
            return;
        }
        if(proratedEndChanged&&clearMidcycleSubtotal()){
            focus("opening.accrued_subtotal");
            message="Cycle end changed. Re-enter the subtotal with the new daily-fee allocation.";
            return;
        }
        focus("opening.cycle_begin_utc_ms");
        message=i18n::tariffMonthlyEnglish(Key::Derived);
    }
    std::string hint(const std::string &s)const{
        using HintKey=i18n::TariffHintMessageKey;
        using Key=i18n::TariffMonthlyMessageKey;
        if(s=="monthly_day"||s=="annual_day"||s=="bimonthly_day")return i18n::tariffMonthlyEnglish(Key::Day);
        if(s=="annual_month")return i18n::tariffMonthlyEnglish(Key::AnnualMonth);
        if(s=="bimonthly_start_month")return i18n::tariffMonthlyEnglish(Key::BimonthlyMonth);
        if(seasonal()&&s=="through_kwh")return i18n::tariffMonthlyEnglish(Key::SeasonalBand);
        if(s=="minimum_cycle_subtotal"||s=="through_kwh"){
            const auto optional=i18n::tariffHintEnglish(HintKey::OptionalDecimal);
            return bimonthly()?i18n::tariffMonthlyEnglish(Key::MonthlyAmount)+" "+optional:optional;
        }
        if(bimonthly()&&s=="cycle_charge")return i18n::tariffMonthlyEnglish(Key::MonthlyAmount);
        if(s=="tiers")return i18n::tariffHintEnglish(HintKey::Tiers);
        if(s=="time_mode")return i18n::tariffHintEnglish(HintKey::TimeMode);
        if(s=="kind")return i18n::tariffHintEnglish(HintKey::ClockKind);
        if(s=="zone")return "Enter an IANA zone from the reviewed time database, for example America/New_York.";
        if(s=="date")return i18n::tariffHintEnglish(HintKey::Date);
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
    void showProblem(const TariffProblem &error){
        using Key=i18n::TariffErrorMessageKey;
        i18n::MessageArguments arguments={{"reason",tariffDiagnosticReasonEnglish(error)}};
        if(!error.path.empty())arguments.emplace_back("path",error.path);
        message=i18n::tariffErrorEnglish(error.path.empty()?Key::OriginalRoot:Key::OriginalPath,arguments);
        focus(error.path);
    }
    static std::vector<std::string> reviewLines(const JsonValue &before,const JsonValue &after){
        using Key=i18n::TariffReviewMessageKey;std::vector<std::string> lines;
        for(const auto &record:tariffDeclarationChangeRecords(before,after)){const bool rows=record.kind==TariffDeclarationChange::Kind::RowCount;i18n::MessageArguments arguments;
            if(!record.path.empty())arguments.emplace_back("path",record.path);
            arguments.emplace_back("before",rows?std::to_string(record.beforeRows):record.beforeJson);arguments.emplace_back("after",rows?std::to_string(record.afterRows):record.afterJson);
            lines.push_back(i18n::tariffReviewEnglish(rows?(record.path.empty()?Key::RowsRoot:Key::Rows):(record.path.empty()?Key::ValueRoot:Key::Value),arguments));
        }
        return lines;
    }
    void beginReview(){
        using PanelKey=i18n::TariffPanelMessageKey;
        using MonthlyKey=i18n::TariffMonthlyMessageKey;
        const bool prorated=recurring()&&draft.find("reset_policy")->find("daily_fee_allocation");
        const auto cycleReview=bimonthly()?MonthlyKey::BimonthlyReview:annual()?MonthlyKey::AnnualReview:MonthlyKey::Review;
        review={i18n::tariffPanelEnglish(PanelKey::ReviewHeading),i18n::tariffPanelEnglish(PanelKey::NoActivation),
            i18n::tariffPanelEnglish(PanelKey::DeclaredForecast),recurring()?i18n::tariffMonthlyEnglish(cycleReview):i18n::tariffPanelEnglish(PanelKey::ResetLimit),
            prorated?i18n::tariffMonthlyEnglish(MonthlyKey::ProratedExclusions):i18n::tariffPanelEnglish(PanelKey::Excludes),
            i18n::tariffPanelEnglish(PanelKey::NoZeroFill),
            i18n::tariffPanelEnglish(PanelKey::FullCycle),i18n::tariffPanelEnglish(PanelKey::ChangesHeading)};
        if(seasonal())review.insert(review.begin()+4,i18n::tariffMonthlyEnglish(MonthlyKey::SeasonalReview));
        if(prorated)review.insert(review.begin()+4,i18n::tariffMonthlyEnglish(MonthlyKey::FeeReview));
        const auto *plan=draft.find("plan");
        const auto *rounding=plan?plan->find("rounding"):nullptr;
        if(rounding&&rounding->text==meterHalfHourRounding)
            review.insert(review.begin()+4,"Meter register only. Rate each complete UTC half hour after 0.01 kWh half-even rounding.");
        const auto diff=reviewLines(active,draft);review.insert(review.end(),diff.begin(),diff.end());
        if(diff.empty())review.push_back(i18n::tariffPanelEnglish(PanelKey::NoChanges));
        const auto complete=reviewLines({},draft);review.insert(review.end(),complete.begin(),complete.end());
        review.push_back(i18n::tariffPanelEnglish(PanelKey::ConfirmationOnly));
        page=TariffSettingsPage::Review;reviewOffset=0;message.clear();
    }
};
} // namespace om
