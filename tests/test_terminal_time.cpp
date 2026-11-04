// Explicit-clock display fixtures, never system 'now', a billing reset or physical terminal qualification.
#include "product/terminal_time.h"
#include "product/terminal_message.h"
#include "product_test_util.h"
#include <clocale>
#include <limits>
using namespace om;
namespace {
constexpr int64_t hour=3600000,last=253402300799999LL;
int64_t utc(CivilDate date,uint32_t ms=0){const auto result=civilResolve({},date,ms);CHECK(result.status==CivilStatus::Ok&&result.utcMs);return result.utcMs.value_or(0);}
std::string unisolate(std::string text){for(const auto *mark:{u8"\u2066",u8"\u2067",u8"\u2068",u8"\u2069"})for(auto at=text.find(mark);at!=text.npos;at=text.find(mark))text.erase(at,3);return text;}
void matrix(){
    for(const auto &locale:presentationFormattingLocales())for(auto digits:{PresentationDigits::Latin,PresentationDigits::Locale})
        for(auto fields:{PresentationTimeFields::DateTime,PresentationTimeFields::Date,PresentationTimeFields::Time})for(auto style:{PresentationDateStyle::Numeric,PresentationDateStyle::AbbreviatedMonth})
            for(auto hours:{PresentationHourCycle::Hour24,PresentationHourCycle::Hour12})for(bool millis:{false,true}){
                TerminalTimeRequest request;request.utcMs=utc({2026,11,5},23*hour+45*60000+6789);request.clock={CivilClockKind::FixedStandardOffset,"",19845};
                request.formattingLocale=locale;request.digits=digits;request.fields=fields;request.dateStyle=style;request.hours=hours;request.milliseconds=millis;request.tone=Tone::Canary;
                TerminalTime result;CHECK(formatTerminalTime(request,&result));
                CHECK(result.utcMs==*request.utcMs&&sameCivilDate(result.civil.date,{2026,11,6})&&result.civil.millisecondOfDay==18951789&&result.civil.offsetSeconds==19845);
                CHECK(result.civil.databaseVersion=="explicit-fixed-clock"&&result.clock.kind==request.clock.kind&&result.clock.fixedOffsetSeconds==19845);
                CHECK(result.formattingLocale==locale&&!result.localeFallback&&!result.asciiRecovery&&result.backendFallback==!terminalTextBackend().unicode);
                CHECK(result.zoneLabel=="UTC+05:30:45"&&!result.segment.content.invariant&&result.segment.content.atom==TerminalTextAtom::LeftToRight&&result.segment.tone==Tone::Canary);
                const std::string date="2026-11-06",time=millis?"05:15:51.789":"05:15:51";
                const auto expected=fields==PresentationTimeFields::Date?date:fields==PresentationTimeFields::Time?time:date+" "+time;
                CHECK(result.ascii==expected&&result.segment.content.ascii==expected+" [UTC+05:30:45]");
                if(result.backendFallback)CHECK(result.text==result.ascii&&!result.resourceInheritance);
                request.asciiOnly=true;TerminalTime recovery;CHECK(formatTerminalTime(request,&recovery)&&recovery.asciiRecovery&&!recovery.backendFallback&&!recovery.resourceInheritance&&recovery.text==expected);
                CHECK(recovery.utcMs==result.utcMs&&sameCivilDate(recovery.civil.date,result.civil.date)&&recovery.civil.offsetSeconds==result.civil.offsetSeconds&&recovery.ascii==result.ascii);
            }
}
void exactBoundaries(){
    TerminalTimeRequest request;request.asciiOnly=true;request.utcMs=0;TerminalTime result;
    CHECK(formatTerminalTime(request,&result)&&result.ascii=="1970-01-01 00:00:00.000"&&result.zoneLabel=="UTC");
    request.utcMs=last;CHECK(formatTerminalTime(request,&result)&&result.ascii=="9999-12-31 23:59:59.999");
    request.utcMs=utc({2000,2,28},23*hour+59*60000+59999);request.clock={CivilClockKind::FixedStandardOffset,"",1};
    CHECK(formatTerminalTime(request,&result)&&result.ascii=="2000-02-29 00:00:00.999"&&result.zoneLabel=="UTC+00:00:01");
    request.utcMs=utc({2100,2,28},23*hour);request.clock.fixedOffsetSeconds=7200;
    CHECK(formatTerminalTime(request,&result)&&result.ascii=="2100-03-01 01:00:00.000");
    request.utcMs=utc({2026,1,1},hour);request.clock.fixedOffsetSeconds=-7200;
    CHECK(formatTerminalTime(request,&result)&&result.ascii=="2025-12-31 23:00:00.000"&&result.zoneLabel=="UTC-02:00:00");
    for(int32_t offset:{-86400,-1,0,1,86400}){request.utcMs=utc({2026,1,2});request.clock.fixedOffsetSeconds=offset;
        CHECK(formatTerminalTime(request,&result)&&result.civil.offsetSeconds==offset&&civilResolve(request.clock,result.civil.date,result.civil.millisecondOfDay).utcMs==request.utcMs);}
    request.clock={};request.utcMs=utc({2026,1,1},12345);request.milliseconds=false;
    CHECK(formatTerminalTime(request,&result)&&result.ascii=="2026-01-01 00:00:12"&&result.utcMs==*request.utcMs&&result.civil.millisecondOfDay==12345);
    request.formattingLocale="zz-ZZ";CHECK(formatTerminalTime(request,&result)&&result.localeFallback&&result.formattingLocale=="en");
}
void nativeFields(){
    if(!terminalTextBackend().unicode)return;
    TerminalTimeRequest request;request.utcMs=utc({2026,11,5},13*hour+45678);request.fields=PresentationTimeFields::Time;TerminalTime result;
    CHECK(formatTerminalTime(request,&result)&&result.text=="13:00:45.678");
    request.hours=PresentationHourCycle::Hour12;
    CHECK(formatTerminalTime(request,&result)&&result.text.find("01:00:45.678")==0&&result.text.find("PM")!=result.text.npos&&result.ascii=="13:00:45.678");
    request.fields=PresentationTimeFields::Date;request.dateStyle=PresentationDateStyle::AbbreviatedMonth;
    CHECK(formatTerminalTime(request,&result)&&result.text=="Nov 5, 2026"&&result.ascii=="2026-11-05");
    request.fields=PresentationTimeFields::Time;request.hours=PresentationHourCycle::Hour24;request.utcMs=last;
    CHECK(formatTerminalTime(request,&result)&&result.text=="23:59:59.999");
    request.utcMs=utc({2026,11,5},13*hour+45678);request.clock={CivilClockKind::FixedStandardOffset,"",7200};
    CHECK(formatTerminalTime(request,&result)&&result.text=="15:00:45.678"&&result.zoneLabel=="UTC+02:00:00");
    request.formattingLocale="ar-EG";request.digits=PresentationDigits::Locale;
    CHECK(formatTerminalTime(request,&result)&&result.text.find(u8"١٥")!=result.text.npos&&result.text.find(u8"٦٧٨")!=result.text.npos);
    request.digits=PresentationDigits::Latin;
    CHECK(formatTerminalTime(request,&result)&&result.text=="15:00:45.678");
}
void layout(){
    for(const auto &locale:presentationFormattingLocales())for(auto digits:{PresentationDigits::Latin,PresentationDigits::Locale})for(auto style:{PresentationDateStyle::Numeric,PresentationDateStyle::AbbreviatedMonth}){
        TerminalTimeRequest request;request.utcMs=utc({2026,11,5},13*hour+45678);request.formattingLocale=locale;request.digits=digits;request.dateStyle=style;request.tone=Tone::Donation;
        TerminalTime value;CHECK(formatTerminalTime(request,&value));
        for(auto order:{TerminalTextOrder::Ascii,TerminalTextOrder::Logical,TerminalTextOrder::Visual})for(auto direction:{TerminalTextDirection::Auto,TerminalTextDirection::LeftToRight,TerminalTextDirection::RightToLeft}){
            TerminalTextPolicy policy;policy.order=order;policy.direction=direction;policy.columns=1024;ColoredTerminalLine line;
            CHECK(layoutTerminalSegments({value.segment},policy,&line)&&!line.line.clipped&&line.line.cells>0);
            if(order!=TerminalTextOrder::Visual||!terminalTextBackend().unicode)CHECK(unisolate(line.line.text)==(line.line.fallback==TerminalTextFallback::None?value.segment.content.text:value.segment.content.ascii));
            const auto width=line.line.cells;policy.columns=width-1;
            CHECK(layoutTerminalSegments({value.segment},policy,&line)&&line.line.clipped&&line.line.text.empty()&&!line.line.cells);
            std::vector<ColoredTerminalLine> rows(1);rows[0].line.text="previous";
            CHECK(!wrapTerminalSegments({value.segment},policy,64,&rows)&&rows[0].line.text=="previous");
            policy.columns=1024;CHECK(wrapTerminalSegments({value.segment},policy,1,&rows)&&rows.size()==1);
            for(const auto &run:rows[0].runs)if(!run.content.control)CHECK(run.tone==Tone::Donation&&run.content.sourceSpan==0&&!run.content.padding);
        }
        TerminalMessageRequest message;message.key=i18n::MessageKey::SourceValue;message.language="ar";
        message.arguments={{"source",{{"as-of",{},true},Tone::Green}},{"value",value.segment}};
        TerminalTextPolicy policy;policy.order=TerminalTextOrder::Logical;policy.columns=1024;TerminalMessageLine line;
        CHECK(layoutTerminalMessage(message,policy,&line));std::string timestamp;
        for(const auto &run:line.line.runs)if(!run.content.control&&line.metadata.sources[run.content.sourceSpan]=="value"){CHECK(run.tone==Tone::Donation);timestamp+=run.content.text;}
        CHECK(timestamp==(terminalTextBackend().unicode?value.segment.content.text:value.segment.content.ascii));
    }
}
void named(const char *directory){
    CHECK(configureCivilTimeData(std::filesystem::u8path(directory),"2026e")==CivilStatus::Ok&&civilNamedTimeAvailable());
    TerminalTimeRequest request;request.clock={CivilClockKind::NamedIana,"America/New_York",0};request.utcMs=utc({2026,11,1},5*hour+30*60000);request.asciiOnly=true;TerminalTime early,late;
    CHECK(formatTerminalTime(request,&early)&&early.ascii=="2026-11-01 01:30:00.000"&&early.zoneLabel=="America/New_York UTC-04:00:00"&&early.civil.databaseVersion=="2026e");
    *request.utcMs+=hour;CHECK(formatTerminalTime(request,&late)&&late.ascii==early.ascii&&late.zoneLabel=="America/New_York UTC-05:00:00"&&late.utcMs-early.utcMs==hour);
    CHECK(early.segment.content.ascii!=late.segment.content.ascii);
    request.asciiOnly=false;request.formattingLocale="en-GB";TerminalTime native;
    CHECK(formatTerminalTime(request,&native)&&native.civil.databaseVersion==late.civil.databaseVersion&&native.civil.offsetSeconds==late.civil.offsetSeconds&&native.ascii==late.ascii);
    request.clock={CivilClockKind::NamedIana,"America/Winnipeg",0};request.utcMs=utc({2026,11,1},7*hour+30*60000);
    CHECK(formatTerminalTime(request,&native)&&native.civil.offsetSeconds==-18000&&native.ascii=="2026-11-01 02:30:00.000"&&native.civil.databaseVersion=="2026e");
    // The reviewed civil data's Winnipeg no-fallback rule must not be replaced by ICU's bundled rules.
    request.clock={CivilClockKind::NamedIana,"Pacific/Apia",0};request.utcMs=utc({2011,12,30},12*hour);
    CHECK(formatTerminalTime(request,&native)&&native.ascii=="2011-12-31 02:00:00.000");
    request.clock={CivilClockKind::NamedIana,"Etc/Not_A_Zone",0};native.text="previous";
    CHECK(!formatTerminalTime(request,&native)&&native.text=="previous");
}
void refusals(){
    TerminalTimeRequest valid;valid.utcMs=utc({2026,11,5});TerminalTime before;before.text="unchanged";before.utcMs=17;before.civil.databaseVersion="original";before.segment.content.text="previous";
    for(unsigned mode=0;mode<16;++mode){auto bad=valid;
        if(mode==0)bad.utcMs.reset();
        if(mode==1)bad.utcMs=-1;
        if(mode==2)bad.utcMs=last+1;
        if(mode==3)bad.utcMs=(std::numeric_limits<int64_t>::max)();
        if(mode==4)bad.formattingLocale="en@calendar=islamic";
        if(mode==5)bad.digits=static_cast<PresentationDigits>(99);
        if(mode==6)bad.fields=static_cast<PresentationTimeFields>(99);
        if(mode==7)bad.dateStyle=static_cast<PresentationDateStyle>(99);
        if(mode==8)bad.hours=static_cast<PresentationHourCycle>(99);
        if(mode==9)bad.tone=static_cast<Tone>(99);
        if(mode==10)bad.clock={CivilClockKind::Utc,"UTC",0};
        if(mode==11)bad.clock={CivilClockKind::FixedStandardOffset,"",86401};
        if(mode==12)bad.clock={static_cast<CivilClockKind>(99),"",0};
        if(mode==13)bad.clock={CivilClockKind::NamedIana,"../private",0};
        if(mode==14){bad.utcMs=0;bad.clock={CivilClockKind::FixedStandardOffset,"",-1};}
        if(mode==15){bad.utcMs=last;bad.clock={CivilClockKind::FixedStandardOffset,"",1};}
        for(bool recovery:{false,true}){bad.asciiOnly=recovery;auto result=before;CHECK(!formatTerminalTime(bad,&result)&&result.text=="unchanged"&&result.utcMs==17&&result.civil.databaseVersion=="original"&&result.segment.content.text=="previous");}
    }
    CHECK(!formatTerminalTime(valid,nullptr));
    if(!civilNamedTimeAvailable()){valid.clock={CivilClockKind::NamedIana,"America/New_York",0};for(bool recovery:{false,true}){valid.asciiOnly=recovery;auto result=before;CHECK(!formatTerminalTime(valid,&result)&&result.text=="unchanged");}}
}
}
int main(int argc,char **argv){const std::string locale=std::setlocale(LC_ALL,nullptr);matrix();exactBoundaries();nativeFields();layout();refusals();if(argc==2)named(argv[1]);CHECK(locale==std::setlocale(LC_ALL,nullptr));return finishTests("explicit civil-clock Gregorian timestamp presentation (not native activation)");}
