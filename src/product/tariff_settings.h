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
                const bool openingChanged=changed&&location=="opening";
                const bool components=requiresPricedOpeningReview();
                const bool changedOpeningPoint=openingChanged&&(seasonal()||components)&&
                    (name=="through_utc_ms"||name=="cycle_begin_utc_ms");
                const bool changedAllowance=changed&&(seasonRow()||(openingChanged&&seasonal()&&name=="cycle_end_utc_ms"));
                const bool changedMeterBinding=openingChanged&&components&&(name=="scope"||name=="adapter");
                const bool changedDemandWindow=changed&&location=="demand_charge"&&(name=="window_minutes"||name=="offset_minutes");
                const bool changedHistoryBasis=changed&&ratchet()&&
                    (changedMeterBinding||changedDemandWindow||location=="reset_policy.clock"||
                     (location=="reset_policy"&&name=="monthly_day")||
                     (openingChanged&&name=="cycle_begin_utc_ms"));
                const bool changedPlanSubtotal=location=="plan"&&(name=="tiers"||name=="cycle_charge"||name=="currency");
                const bool changedExportCurrency=changed&&location=="plan"&&name=="currency"&&exports(draft);
                const bool changedCreditBinding=changed&&restrictedCredit(draft)&&
                    ((location=="plan"&&name=="currency")||changedMeterBinding||changedOpeningPoint);
                const bool changedComponents=changed&&components&&(revision()||changedPlanSubtotal);
                const bool changedRateClock=changed&&location=="plan.clock"&&
                    (name=="zone"||((seasonal()||components)&&(name=="kind"||name=="fixed_offset_seconds")));
                if(input.empty()&&(name=="minimum_cycle_subtotal"||name=="through_kwh"||name=="date"||name=="power_factor_va_places"))*v={};
                else if(v->isNumber()||name=="power_factor_va_places"){
                    JsonValue number;if(!parseJsonStrict(input,&number,nullptr)||!number.isNumber()||input.find_first_not_of("-0123456789")!=std::string::npos){message=i18n::tariffNoticeEnglish(NoticeKey::IntegerOnly);return TariffSettingsResult::None;}
                    *v=std::move(number);
                }else{v->type=JsonValue::Type::String;v->text=input;}
                page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::DraftOnly);
                if(changedOpeningPoint){
                    clearDraftOpeningAmounts();
                    message=exports(draft)?"Opening time changed. Re-enter the import, export and other opening amounts.":
                        demand()?"Opening time changed. Re-enter the import, subtotal and demand peak.":
                        seasonal()?i18n::tariffMonthlyEnglish(i18n::TariffMonthlyMessageKey::SeasonalOpeningPoint):
                        "Opening time changed. Re-enter the imported quantity and subtotal.";
                }else if(changedMeterBinding){
                    clearDraftOpeningAmounts();
                    message=exports(draft)?"Meter binding changed. Re-enter the import, export and other opening amounts.":
                        demand()?"Meter binding changed. Re-enter the import, subtotal and demand peak.":
                        "Meter binding changed. Re-enter the imported quantity and subtotal.";
                }else if(changedRateClock&&clearMidcycleSubtotal())
                    message="Rate clock changed. Re-enter the opening subtotal using the new clock.";
                else if(changedAllowance)reviewChangedSeasonalAllowance();
                else if(changedDemandWindow)reviewChangedDemandWindow();
                else if(changedComponents)reviewChangedComponents();
                if(changedHistoryBasis&&clearDemandHistory())
                    message+=" Re-enter the prior monthly peaks for the new basis.";
                if(openingChanged&&name=="imported_kwh"&&restrictedCredit(draft)){
                    member(*member(draft,"restricted_credit"),"accrued_import_subtotal")->text.clear();
                    message+=" Re-enter the opening import energy subtotal.";
                }
                if(changedCreditBinding){
                    auto &credit=*member(draft,"restricted_credit");
                    for(auto &lot:member(credit,"lots")->items)member(lot,"amount")->text.clear();
                    message+=" Review the credit balance for the new opening or currency.";
                }
                if(changedExportCurrency){
                    auto &opening=*member(draft,"opening");
                    if(opening.find("through_utc_ms")->text!=opening.find("cycle_begin_utc_ms")->text){
                        member(opening,"accrued_export_payment")->text.clear();
                        message+=" Re-enter the accrued export payment in the new currency.";
                    }
                }
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
                const bool rateRows=requiresPricedOpeningReview()&&(revision()||path()=="plan.revisions");
                if(v->isArray()&&field<v->items.size()){
                    v->items.erase(v->items.begin()+field);
                    if(field&&field==v->items.size())--field;
                }
                page=TariffSettingsPage::Browse;message=i18n::tariffNoticeEnglish(NoticeKey::Removed);
                if(collectionName()=="seasons")reviewChangedSeasonalAllowance();
                if(rateRows){
                    refreshClockContract();
                    reviewChangedComponents();
                }
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
        else if((key=='c'||key=='C')&&onComponentToggle())toggleComponentMultiplier();
        else if((key=='d'||key=='D')&&onDemandToggle())toggleDemandCharge();
        else if((key=='r'||key=='R')&&onRatchetToggle())toggleRatchet();
        else if((key=='e'||key=='E')&&path()=="plan.export_payment")toggleExportSchedule();
        else if((key=='e'||key=='E')&&onExportToggle())toggleExportPayment();
        else if((key=='u'||key=='U')&&onRestrictedCreditToggle())toggleRestrictedCredit();
        else if((key=='t'||key=='T')&&onTaxToggle())toggleTaxPeriods();
        else if((key=='k'||key=='K')&&onRatchetToggle())toggleDemandUnit();
        else if((key=='f'||key=='F')&&onCyclePolicy()&&recurring()&&!bimonthly())toggleDailyFeeAllocation();
        else if((key=='z'||key=='Z')&&onTariffClock())toggleNamedClock();
        else if((key=='g'||key=='G')&&recurring())deriveCycle();
        else if(key==kKeyDown||key=='\t'){const auto n=count();if(n)field=(field+1)%n;}
        else if(key==kKeyUp||key==kKeyBackTab){const auto n=count();if(n)field=(field+n-1)%n;}
        else if((key=='a'||key=='A')&&node()->isArray()){
            const auto collection=collectionName();
            const size_t bound=collection=="lots"?maxRestrictedCreditLots:collection=="demand_history"?12:
                collection=="tax_periods"?maxTaxPeriods:collection=="rules"||collection=="prior_taxes"?maxTaxRules:
                collection=="components"||collection=="included_in"?taxComponentCount:
                collection=="revisions"||collection=="seasons"?16:collection=="dynamic"?256:32;
            if(count()>=bound||tariffJson(draft).size()>tariffDeclarationMaxBytes-1024)message=i18n::tariffNoticeEnglish(NoticeKey::Capacity);
            else{
                auto value=tariffRowTemplate(collection,componentRevision(),apparentDemand());
                if(value.isObject()||value.isString()){
                    node()->items.push_back(std::move(value));
                    field=count()-1;
                    message=i18n::tariffNoticeEnglish(NoticeKey::AddedBlank);
                    if(collection=="seasons")reviewChangedSeasonalAllowance();
                    if(requiresPricedOpeningReview()&&(revision()||collection=="revisions"))reviewChangedComponents();
                }
            }
        }else if((key=='x'||key=='X')&&node()->isArray()&&count())page=TariffSettingsPage::Remove;
        else if(key=='\r'||key=='\n'||key==' '){
            auto *v=selected();if(!v)return TariffSettingsResult::None;
            if(v->isObject()||v->isArray()){route.push_back(field);field=0;message.clear();}
            else if(path()=="plan"&&selectedName()=="rounding")toggleMeterRounding(*v);
            else if(taxRuleRow()&&selectedName()=="kind")toggleTaxKind();
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
            if(onComponentToggle())shortcuts+="[C] Component multiplier ";
            if(onDemandToggle())shortcuts+="[D] Demand charge ";
            if(onExportToggle())shortcuts+="[E] Export payment ";
            if(path()=="plan.export_payment")shortcuts+="[E] Fixed/scheduled export ";
            if(onRestrictedCreditToggle())shortcuts+="[U] Restricted credit ";
            if(onTaxToggle())shortcuts+="[T] Ordered taxes ";
            if(onRatchetToggle())shortcuts+="[R] Historical demand floor [K] kW/kVA ";
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
    static bool hasComponents(const JsonValue &value){
        const auto *plan=value.find("plan");
        const auto *revisions=plan?plan->find("revisions"):nullptr;
        return revisions&&std::any_of(revisions->items.begin(),revisions->items.end(),[](const JsonValue &row){
            return row.find("component_multiplier")!=nullptr;
        });
    }
    bool demand()const{return draft.find("demand_charge")!=nullptr;}
    bool ratchet()const{
        const auto *rule=draft.find("demand_charge");
        return rule&&rule->find("ratchet");
    }
    bool apparentDemand()const{
        const auto *rule=draft.find("demand_charge");
        return rule&&rule->find("power_factor_va_places");
    }
    const char *openingPeakField()const{return apparentDemand()?"peak_demand_kva":"peak_demand_kw";}
    static const JsonValue *exportRule(const JsonValue &value){
        const auto *plan=value.find("plan");
        return plan?plan->find("export_payment"):nullptr;
    }
    static bool exports(const JsonValue &value){return exportRule(value)!=nullptr;}
    bool onExportToggle()const{
        return (route.empty()&&selectedName()=="plan")||(path()=="plan"&&selectedName()=="export_payment");
    }
    static bool restrictedCredit(const JsonValue &value){return value.find("restricted_credit")!=nullptr;}
    static bool hasTaxes(const JsonValue &value){
        const auto *plan=value.find("plan");
        return plan&&plan->find("tax_periods");
    }
    bool onTaxToggle()const{
        return (route.empty()&&selectedName()=="plan")||(path()=="plan"&&selectedName()=="tax_periods");
    }
    bool taxRuleRow()const{return route.size()==5&&path().rfind("plan.tax_periods[",0)==0&&path().find(".rules[")!=std::string::npos;}
    bool onRestrictedCreditToggle()const{
        return route.empty()&&(selectedName()=="plan"||selectedName()=="restricted_credit");
    }
    bool onRatchetToggle()const{return route.empty()&&selectedName()=="demand_charge";}
    bool onDemandToggle()const{return onCyclePolicy()||(route.empty()&&selectedName()=="demand_charge");}
    bool requiresPricedOpeningReview()const{
        return demand()||active.find("demand_charge")||hasComponents(active)||hasComponents(draft)||exports(active)||exports(draft)||
            restrictedCredit(active)||restrictedCredit(draft)||hasTaxes(active)||hasTaxes(draft);
    }
    const JsonValue *revision()const{
        if(route.size()<3||draft.members[route[0]].first!="plan")return nullptr;
        const auto &collection=draft.members[route[0]].second.members[route[1]];
        return collection.first=="revisions"?&collection.second.items[route[2]]:nullptr;
    }
    bool componentRevision()const{
        const auto *row=revision();
        return row&&row->find("component_multiplier");
    }
    bool onComponentToggle()const{
        return route.size()==3&&selectedName()=="base_rate_per_kwh"&&revision();
    }
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
        if(s=="cycle_begin_utc_ms"||s=="cycle_end_utc_ms")return path()=="opening"&&recurring();
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
        const auto *payment=exportRule(draft);
        schema->text=hasTaxes(draft)?"15":restrictedCredit(draft)?"13":payment?(payment->find("dynamic")?"14":"12"):apparentDemand()?"11":ratchet()?"10":demand()?"9":hasComponents(draft)?"8":seasonal()?"7":bimonthly()?"6":annual()?"5":namedRate||namedReset?"4":!recurring()?"1":
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
        if(!rate&&clearDemandHistory()){
            message="Draft reset clock changed. Derive the cycle and re-enter the prior monthly peaks.";
        }else if(rate&&clearMidcycleSubtotal()){
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
        if(ratchet()){
            message="Remove the historical demand floor before changing the monthly reset type.";
            return;
        }
        const bool useAnnual=selectedCadence==TariffResetCadence::Annual;
        const bool useBimonthly=selectedCadence==TariffResetCadence::Bimonthly;
        if(useBimonthly&&demand()){
            message="Demand charge requires a cycle without two-month averaging.";
            return;
        }
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
    void toggleComponentMultiplier(){
        auto &row=*node();
        const bool remove=componentRevision();
        const auto removeField=[](JsonValue &object,const std::string &name){
            object.members.erase(std::remove_if(object.members.begin(),object.members.end(),
                [&](const auto &entry){return entry.first==name;}),object.members.end());
        };
        JsonValue blank;
        blank.type=JsonValue::Type::String;
        if(remove)removeField(row,"component_multiplier");
        else{
            JsonValue rule;
            parseJsonStrict(R"({"rate_per_kwh":"","base_multiplier":""})",&rule,nullptr);
            if(row.find("time_mode")->text=="dynamic")member(rule,"base_multiplier")->text="0";
            row.members.emplace_back("component_multiplier",std::move(rule));
        }
        for(const auto *collection:{"calendar","dynamic"}){
            for(auto &entry:member(row,collection)->items){
                if(remove)removeField(entry,"multiplier");
                else entry.members.emplace_back("multiplier",blank);
            }
        }
        refreshClockContract();
        message=remove?"Draft component multiplier removed. Review the remaining unit rates.":
            "Enter the multiplied part and each factor. Review existing unit rates as the unscaled part.";
        reviewChangedComponents();
    }
    void reviewChangedComponents(){
        if(clearMidcycleSubtotal())message="Tariff changed. Re-enter the opening subtotal using the revised rules.";
    }
    void toggleDemandCharge(){
        if(bimonthly()){
            message="Demand charge requires a cycle without two-month averaging.";
            return;
        }
        auto &opening=*member(draft,"opening");
        if(demand()){
            opening.members.erase(std::remove_if(opening.members.begin(),opening.members.end(),[](const auto &entry){
                return entry.first=="peak_demand_kw"||entry.first=="peak_demand_kva"||entry.first=="demand_history";
            }),opening.members.end());
            draft.members.erase(std::remove_if(draft.members.begin(),draft.members.end(),[](const auto &entry){
                return entry.first=="demand_charge";
            }),draft.members.end());
            focus("reset_policy");
            message="Demand charge removed from the draft. Review the opening before saving.";
        }else{
            JsonValue rule, peak;
            parseJsonStrict(R"({"window_minutes":0,"offset_minutes":0,"published_utc_ms":0,"rate_per_kw":""})",&rule,nullptr);
            peak.type=JsonValue::Type::String;
            opening.members.emplace_back("peak_demand_kw",std::move(peak));
            draft.members.emplace_back("demand_charge",std::move(rule));
            focus("demand_charge");
            message="Enter the aligned window, price and established peak. The opening subtotal excludes demand.";
        }
        refreshClockContract();
    }
    void toggleRestrictedCredit(){
        if(hasTaxes(draft)){
            message="Remove ordered taxes to add restricted credit.";
            return;
        }
        if(exports(draft)||apparentDemand()){
            message="Restricted credit requires a plan without separate export payments or kVA demand.";
            return;
        }
        if(restrictedCredit(draft)){
            draft.members.erase(std::remove_if(draft.members.begin(),draft.members.end(),[](const auto &entry){
                return entry.first=="restricted_credit";
            }),draft.members.end());
            focus("plan");
            message="Restricted credit removed from the draft.";
        }else{
            JsonValue credit;
            parseJsonStrict(R"({"accrued_import_subtotal":"","lots":[]})",&credit,nullptr);
            draft.members.emplace_back("restricted_credit",std::move(credit));
            focus("restricted_credit");
            message="Enter the opening import subtotal and unused credit lots.";
        }
        refreshClockContract();
    }
    static void clearTaxBases(JsonValue &opening){
        if(auto *bases=member(opening,"tax_bases"))
            for(auto &field:bases->members)field.second.text.clear();
    }
    void toggleTaxPeriods(){
        if(restrictedCredit(draft)){
            message="Remove restricted credit to add ordered taxes.";
            return;
        }
        auto &plan=*member(draft,"plan");
        auto &opening=*member(draft,"opening");
        if(hasTaxes(draft)){
            plan.members.erase(std::remove_if(plan.members.begin(),plan.members.end(),[](const auto &entry){
                return entry.first=="tax_periods";
            }),plan.members.end());
            opening.members.erase(std::remove_if(opening.members.begin(),opening.members.end(),[](const auto &entry){
                return entry.first=="tax_bases";
            }),opening.members.end());
            focus("plan");
            message="Ordered taxes removed from the draft.";
        }else{
            JsonValue periods,bases;
            parseJsonStrict("[]",&periods,nullptr);
            parseJsonStrict(R"({"energy":"0","daily_fee":"0","cycle_fee":"0","demand":"0","billed_kwh":"0"})",&bases,nullptr);
            plan.members.emplace_back("tax_periods",std::move(periods));
            opening.members.emplace_back("tax_bases",std::move(bases));
            const bool midcycle=opening.find("cycle_begin_utc_ms")->text!=opening.find("through_utc_ms")->text;
            if(midcycle)clearTaxBases(opening);
            focus("plan.tax_periods");
            message=midcycle?"Enter the tax periods and the reviewed opening tax bases. Use a subtotal multiplier of 1.":
                "Enter the tax periods. Use a subtotal multiplier of 1 with ordered taxes.";
        }
        refreshClockContract();
    }
    void toggleTaxKind(){
        auto &row=*node();
        auto &kind=*member(row,"kind");
        if(kind.text=="imported_kwh"){
            kind.text="billed_kwh";
        }else{
            JsonValue replacement;
            if(kind.text=="fraction")
                parseJsonStrict(R"({"key":"","kind":"imported_kwh","rate":"","included":false})",&replacement,nullptr);
            else replacement=tariffRowTemplate("rules");
            *member(replacement,"key")=*row.find("key");
            *member(replacement,"rate")=*row.find("rate");
            row=std::move(replacement);
        }
        message="Review the tax rate and bases for this tax kind.";
    }
    void toggleExportPayment(){
        if(restrictedCredit(draft)){
            message="Remove restricted credit to add a separate export payment.";
            return;
        }
        if(apparentDemand()){
            message="Use kW demand to add a separate export payment. Apparent demand requires its own evidence.";
            return;
        }
        auto &plan=*member(draft,"plan");
        auto &opening=*member(draft,"opening");
        if(exports(draft)){
            plan.members.erase(std::remove_if(plan.members.begin(),plan.members.end(),[](const auto &entry){
                return entry.first=="export_payment";
            }),plan.members.end());
            opening.members.erase(std::remove_if(opening.members.begin(),opening.members.end(),[](const auto &entry){
                return entry.first=="exported_kwh"||entry.first=="accrued_export_payment";
            }),opening.members.end());
            focus("plan");
            message="Export payment removed from the draft.";
        }else{
            JsonValue payment, quantity;
            parseJsonStrict(R"({"rate_per_kwh":"","published_utc_ms":0})",&payment,nullptr);
            quantity.type=JsonValue::Type::String;
            plan.members.emplace_back("export_payment",std::move(payment));
            opening.members.emplace_back("exported_kwh",quantity);
            opening.members.emplace_back("accrued_export_payment",std::move(quantity));
            focus("plan.export_payment");
            message="Enter the export rate, opening export quantity and payment already earned.";
        }
        refreshClockContract();
    }
    void toggleExportSchedule(){
        auto *payment=node();
        const bool wasScheduled=payment->find("dynamic")!=nullptr;
        JsonValue replacement;
        parseJsonStrict(wasScheduled?R"({"rate_per_kwh":"","published_utc_ms":0})":R"({"dynamic":[]})",
                        &replacement,nullptr);
        *payment=std::move(replacement);
        field=0;
        refreshClockContract();
        message=wasScheduled?"Enter a new fixed export rate and publication time.":
            "Add export rate rows in UTC order and enter their publication times.";
    }
    void toggleDemandUnit(){
        if(restrictedCredit(draft)){
            message="Remove restricted credit to use apparent-power demand.";
            return;
        }
        if(exports(draft)){
            message="Remove the separate export payment to use apparent-power demand.";
            return;
        }
        auto &rule=*member(draft,"demand_charge");
        auto &opening=*member(draft,"opening");
        const bool useApparent=!apparentDemand();
        const auto replaceEmptyField=[](JsonValue &object,const char *oldField,const char *newField){
            for(auto &entry:object.members){
                if(entry.first!=oldField)continue;
                entry.first=newField;
                entry.second.text.clear();
                break;
            }
        };
        replaceEmptyField(rule,useApparent?"rate_per_kw":"rate_per_kva",useApparent?"rate_per_kva":"rate_per_kw");
        replaceEmptyField(opening,openingPeakField(),useApparent?"peak_demand_kva":"peak_demand_kw");
        if(useApparent)rule.members.emplace_back("power_factor_va_places",JsonValue{});
        else rule.members.erase(std::remove_if(rule.members.begin(),rule.members.end(),[](const auto &entry){
            return entry.first=="power_factor_va_places";
        }),rule.members.end());
        const bool clearedHistory=clearDemandHistory();
        refreshClockContract();
        message="Demand unit changed. Enter the price and opening peak in the new unit.";
        if(clearedHistory)message+=" Re-enter the prior monthly peaks.";
    }
    void toggleRatchet(){
        if(!recurring()||annual()||bimonthly()){
            message="Historical demand floor requires a monthly reset policy.";
            return;
        }
        auto &rule=*member(draft,"demand_charge");
        auto &opening=*member(draft,"opening");
        if(ratchet()){
            rule.members.erase(std::remove_if(rule.members.begin(),rule.members.end(),[](const auto &entry){
                return entry.first=="ratchet";
            }),rule.members.end());
            opening.members.erase(std::remove_if(opening.members.begin(),opening.members.end(),[](const auto &entry){
                return entry.first=="demand_history";
            }),opening.members.end());
            message="Historical demand floor removed from the draft.";
        }else{
            JsonValue setting,history;
            parseJsonStrict(R"({"lookback_months":0,"fraction":""})",&setting,nullptr);
            history.type=JsonValue::Type::Array;
            rule.members.emplace_back("ratchet",std::move(setting));
            opening.members.emplace_back("demand_history",std::move(history));
            focus("demand_charge.ratchet");
            message="Enter the fraction and lookback length. Enter one actual peak for each prior month.";
        }
        refreshClockContract();
    }
    bool clearDemandHistory(){
        auto *opening=member(draft,"opening");
        auto *history=opening?member(*opening,"demand_history"):nullptr;
        if(!history||history->items.empty())return false;
        history->items.clear();
        return true;
    }
    void reviewChangedDemandWindow(){
        auto &opening=*member(draft,"opening");
        if(opening.find("through_utc_ms")->text!=opening.find("cycle_begin_utc_ms")->text){
            member(opening,openingPeakField())->text.clear();
            message="Demand window changed. Re-enter the opening peak using the new window.";
        }
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
            for(const auto *amount:{"imported_kwh","accrued_subtotal","exported_kwh","accrued_export_payment",openingPeakField()})
                if(auto *value=member(*opening,amount))value->text.clear();
            clearTaxBases(*opening);
        }
        if(auto *credit=member(draft,"restricted_credit"))member(*credit,"accrued_import_subtotal")->text.clear();
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
        clearTaxBases(*opening);
        if(auto *credit=member(draft,"restricted_credit"))member(*credit,"accrued_import_subtotal")->text.clear();
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
        const auto *peak=opening->find(openingPeakField());
        const auto *exported=opening->find("exported_kwh");
        const auto *exportPayment=opening->find("accrued_export_payment");
        const auto *credit=draft.find("restricted_credit");
        const auto *creditSubtotal=credit?credit->find("accrued_import_subtotal"):nullptr;
        const auto *tax=opening->find("tax_bases");
        const bool retainedTax=tax&&std::any_of(tax->members.begin(),tax->members.end(),[](const auto &entry){
            return !entry.second.text.empty();
        });
        if(!begin||!end||!through||!quantity||!subtotal)return;
        TariffCycleBounds bounds;
        TariffProblem error;
        if(!deriveTariffRecurringCycle(*draft.find("reset_policy"),*through,bounds,&error)){
            showProblem(error);
            return;
        }
        auto beginText=std::to_string(bounds.beginUtcMs);
        if(begin->text!=beginText&&(!quantity->text.empty()||!subtotal->text.empty()||(peak&&!peak->text.empty())||
                                  (exported&&!exported->text.empty())||
                                  (exportPayment&&!exportPayment->text.empty())||
                                  (creditSubtotal&&!creditSubtotal->text.empty())||retainedTax)){
            if(!quantity->text.empty())focus("opening.imported_kwh");
            else if(!subtotal->text.empty())focus("opening.accrued_subtotal");
            else if(exported&&!exported->text.empty())focus("opening.exported_kwh");
            else if(exportPayment&&!exportPayment->text.empty())focus("opening.accrued_export_payment");
            else if(creditSubtotal&&!creditSubtotal->text.empty())focus("restricted_credit.accrued_import_subtotal");
            else if(retainedTax)focus("opening.tax_bases");
            else focus(std::string("opening.")+openingPeakField());
            message=tax?"Clear the opening amounts and tax bases before changing the cycle start.":
                credit?"Clear the opening amounts, including the import energy subtotal, before changing the cycle start.":
                exported?"Clear the opening amounts, including accrued export, before changing the cycle start.":
                peak?"Clear the opening import, subtotal and demand peak before changing the cycle start.":
                i18n::tariffMonthlyEnglish(Key::RetainAmounts);
            return;
        }
        auto endText=std::to_string(bounds.endUtcMs);
        const auto *policy=draft.find("reset_policy");
        const bool clearedHistory=begin->text!=beginText&&clearDemandHistory();
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
        message=clearedHistory?"Cycle derived. Re-enter the prior monthly peaks.":
            i18n::tariffMonthlyEnglish(Key::Derived);
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
        if(s=="window_minutes")return "Choose 5 to 60 minutes. The window must divide one hour.";
        if(s=="offset_minutes")return "Enter the UTC grid offset in minutes, smaller than the window.";
        if(s=="accrued_import_subtotal")return "Enter signed import energy charges only, before the multiplier. Enter zero at cycle start.";
        if(s=="expires_utc_ms")return "Enter the absolute UTC ms expiry. List lots in increasing expiry order.";
        if(s=="amount"&&path().rfind("restricted_credit.lots[",0)==0)
            return "Enter the positive unused balance at the opening, in the plan currency.";
        if(s=="exported_kwh")return "Enter nonnegative exported energy since the cycle start. Enter zero at the start.";
        if(s=="accrued_export_payment")return "Enter the accrued export payment in the plan currency. Use a negative amount for a net export charge under scheduled rates. Enter zero without prior exports.";
        if(s=="rate_per_kwh"&&path().rfind("plan.export_payment.dynamic[",0)==0)
            return "Enter the amount per exported kWh in the plan currency. A negative rate charges for exports.";
        if(path()=="plan.export_payment"&&s=="rate_per_kwh")return "Enter the separate payment per exported kWh in the plan currency.";
        if(s=="rate_per_kw")return "Enter the price per kW of the billable demand for this cycle.";
        if(s=="rate_per_kva")return "Enter the price per kVA of the billable demand for this cycle.";
        if(s=="power_factor_va_places")return "Leave blank for measured VA only. Enter 0 to 18 decimals for power-factor conversion.";
        if(s=="lookback_months")return "Enter one to twelve completed monthly cycles.";
        if(s=="fraction")return "Enter a positive fraction up to one. For 80 percent, use 0.8.";
        if((s=="peak_kw"||s=="peak_kva")&&collectionName().rfind("demand_history[",0)==0)
            return "Enter the actual measured peak, before applying any historical floor.";
        if(s=="peak_demand_kw"||s=="peak_demand_kva")return "Enter the established average-window peak. Use zero at cycle start.";
        if(s=="accrued_subtotal"&&demand())return "Enter accrued energy and fees before demand or the subtotal multiplier.";
        if(s=="base_multiplier")return "Enter the fallback factor as a nonnegative decimal. Use 0 in dynamic mode.";
        if(s=="multiplier")return "Enter the nonnegative factor for the multiplied part of the unit rate.";
        if(componentRevision()&&(s=="base_rate_per_kwh"||s=="rate_per_kwh")){
            const auto location=path();
            if(collectionName()=="component_multiplier")return "Enter the sum per kWh of the charges that use the factor.";
            if(location.find(".bands[")==std::string::npos)
                return "Enter the sum per kWh of the charges excluded from the factor.";
        }
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
            exports(draft)?"Restricted export credits, eligibility and rollover are outside this separate-payment rule.":
                demand()?"Demand uses complete UTC-grid windows. Capacity, export, prepaid and independent demand resets are excluded.":
                hasTaxes(draft)?"Each supply cycle needs one published tax period. Restricted closing credits use a separate declaration.":
                prorated?i18n::tariffMonthlyEnglish(MonthlyKey::ProratedExclusions):i18n::tariffPanelEnglish(PanelKey::Excludes),
            i18n::tariffPanelEnglish(PanelKey::NoZeroFill),
            i18n::tariffPanelEnglish(PanelKey::FullCycle),i18n::tariffPanelEnglish(PanelKey::ChangesHeading)};
        if(seasonal())review.insert(review.begin()+4,i18n::tariffMonthlyEnglish(MonthlyKey::SeasonalReview));
        if(ratchet())review.insert(review.begin()+4,
            "Billable demand is the larger of this month's actual peak and the fraction of the prior maximum.");
        if(restrictedCredit(draft))review.insert(review.begin()+4,
            "Restricted credit offsets import energy at closing. Balances expire at their absolute UTC instants.");
        if(const auto *payment=exportRule(draft))review.insert(review.begin()+4,payment->find("dynamic")?
            "Export uses declared UTC rates. A negative rate charges for exports. Supply charges use imported energy.":
            "Export is paid separately. The declared rate applies after the opening. Supply charges use imported energy.");
        if(apparentDemand())review.insert(review.begin()+4,
            "Demand uses kVA and requires whole-meter apparent-power evidence. Energy uses active import in kWh.");
        if(demand())review.insert(review.begin()+4,
            "Peak demand is charged once per cycle. The opening subtotal excludes it. Review the plan and opening keys.");
        if(hasComponents(draft))review.insert(review.begin()+4,
            "Unit rate = unscaled part + multiplied part x factor. Use new plan and opening keys after a rate change.");
        if(hasTaxes(draft))review.insert(review.begin()+4,
            "Ordered taxes use the declared charge bases and prior added taxes. Included components keep their entered amounts.");
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
