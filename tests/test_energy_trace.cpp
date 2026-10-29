#include "energy_history_fixture.h"
#include "electricity_fixture.h"
#include "product/hash.h"
#include "product_test_util.h"
#include <algorithm>
#include <fstream>
using namespace om;
using namespace energy_history_fixture;
namespace {
using S=EnergyHistoryStatus;
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
EnergyHistoryLimits policy(bool compact){EnergyHistoryLimits l;if(compact)l.compaction=EnergyCompactionPolicy{2,32768};return l;}
EnergyHistoryBatch batch(unsigned seq,bool compact){EnergyHistoryBatch b;b.sequence=seq;b.id=compact?"energy."+std::to_string(seq):"fixture.trace."+std::to_string(seq);b.sessionId="fixture.trace-parent";return b;}
void checkInterval(const PowerInterval &i,int64_t a,int64_t z,const char *watts){CHECK(i.beginUtcMs==a&&i.endUtcMs==z&&i.watts.text()==watts);}
}
int main(){
    const auto root=std::filesystem::path(makeScratchDir("energy-trace"));auto binding=energy_history_fixture::binding();const auto now=T+100000;
    {EnergyHistoryLedger ledger;CHECK(ledger.open(root/"missing"/"source.db",binding,now,EnergyHistoryOpen::ExistingQuery)==S::Storage&&!std::filesystem::exists(root/"missing"));}
    for(const auto *suffix:{"-wal","-shm","-journal"}){const auto file=root/(std::string("orphan")+suffix+".db");auto companion=file;companion+=suffix;
        {std::ofstream output(companion);output<<"fixture retained companion";}EnergyHistoryLedger ledger;
        CHECK(ledger.open(file,binding,now,EnergyHistoryOpen::CreateNew)==S::Storage&&!std::filesystem::exists(file)&&bytes(companion)=="fixture retained companion");}
    for(bool compact:{false,true}){const auto path=root/(compact?"compacted.db":"originals.db");const auto limits=policy(compact);
        auto first=batch(1,compact);first.start=EnergyHistorySession{first.sessionId,T,100,5000};first.observedAtMs=T+1000;
        first.readings={reading(0,0,100000),reading(1,0,200000),reading(0,1000,200000)};
        auto second=batch(2,compact);second.observedAtMs=T+3000;
        second.readings={reading(1,1500,0),reading(0,2000,0),reading(0,3000,0),reading(1,3000,0)};EnergyHistoryTrace frozen;
        {EnergyHistoryLedger ledger;CHECK(ledger.open(path,binding,now,EnergyHistoryOpen::CreateNew,limits)==S::Ok);
            const auto empty=ledger.queryTrace(T,T+1000,now);CHECK(empty.status==S::Ok&&empty.checkpoint.sequence==0&&!empty.completeTrace&&!empty.totals.completeKwh);
            CHECK(ledger.apply(first,now)==S::Ok);const auto partial=ledger.queryTrace(T,T+1000,now);
            CHECK(partial.status==S::Ok&&partial.totals.coverage==EnergyHistoryCoverage::Partial&&!partial.completeTrace&&partial.totals.coveredDeviceMs==1000);
            CHECK(ledger.apply(second,now)==S::Ok);frozen=ledger.queryTrace(T,T+3000,now);
            CHECK(frozen.status==S::Ok&&frozen.completeTrace&&frozen.checkpoint.sequence==2&&frozen.checkpoint.compactedSequence==(compact?2u:0u));
            CHECK(frozen.checkpoint.digest==ledger.checkpoint(now).digest&&frozen.totals.observedAtMs==frozen.checkpoint.observedAtMs&&frozen.binding.channels==binding.channels);
            CHECK(frozen.completeTrace->evidence.origin==PowerOrigin::SyntheticFixture&&frozen.completeTrace->intervals.size()==4&&frozen.totals.knownKwh.numerator.text()=="600000");
            const auto &rows=frozen.completeTrace->intervals;checkInterval(rows[0],T,T+1000,"300");checkInterval(rows[1],T+1000,T+1500,"400");
            checkInterval(rows[2],T+1500,T+2000,"200");checkInterval(rows[3],T+2000,T+3000,"0");
            const auto clipped=ledger.queryTrace(T+999,T+2001,now);CHECK(clipped.completeTrace&&clipped.completeTrace->intervals.size()==4&&clipped.totals.knownKwh.numerator.text()=="300300");
            const auto capacity=ledger.queryTrace(T,T+3000,now,3);CHECK(capacity.status==S::Capacity&&!capacity.completeTrace&&capacity.checkpoint.digest==frozen.checkpoint.digest);
            CHECK(ledger.queryTrace(T,T+3000,now,0).status==S::Invalid&&ledger.queryTrace(T,T+3000,now,513).status==S::Invalid);
            CHECK(ledger.queryTrace(T,T+3000,T+2000).status==S::Invalid&&ledger.queryTrace(T,T+1000,T+2000).status==S::Clock);
            CHECK(!ledger.queryTrace(T-1,T+3000,now).completeTrace&&!ledger.queryTrace(T,T+3001,now).completeTrace);
            auto third=batch(3,compact);third.observedAtMs=T+4000;third.readings={reading(0,4000,0),reading(1,4000,0)};
            CHECK(ledger.apply(third,now)==S::Ok);const auto zeros=ledger.queryTrace(T+2000,T+4000,now,1);
            CHECK(zeros.completeTrace&&zeros.completeTrace->intervals.size()==1&&zeros.totals.completeKwh->numerator.zero());
            CHECK(frozen.checkpoint.sequence==2&&frozen.completeTrace->intervals.back().endUtcMs==T+3000);}
        const auto before=bytes(path);
        {EnergyHistoryLedger reader;CHECK(reader.open(path,binding,now,EnergyHistoryOpen::ExistingQuery,limits)==S::Ok);
            CHECK(reader.apply(second,now)==S::Invalid&&reader.verifyCommitted(second,now)==S::Replay);
            const auto trace=reader.queryTrace(T,T+3000,now);CHECK(trace.status==S::Ok&&trace.completeTrace&&trace.checkpoint.sequence==3&&trace.totals.knownKwh.numerator.text()=="600000");
            EnergyHistoryLedger writer;CHECK(writer.open(path,binding,now,EnergyHistoryOpen::Existing,limits)==S::Storage);
            // A rate boundary must see exact chronology, not a flattened average of the same kWh.
            auto plan=electricity_fixture::plan(T,T+3000);plan.revisions[0].timeMode=TimeRateMode::Dynamic;plan.revisions[0].baseRatePerKwh={};
            plan.revisions[0].dynamic={{T,T+1000,T,electricity_fixture::d("1")},{T+1000,T+3000,T,electricity_fixture::d("3")}};
            auto opening=electricity_fixture::opening(T,T+3000);opening.evidence=trace.binding.evidence;
            const auto exact=accrueBill(plan,opening,*trace.completeTrace,T+3000,now);CHECK(exact.settledTotal&&exact.settledTotal->numerator.text()=="1200000");
            const PowerTrace averaged{trace.binding.evidence,{{T,T+3000,electricity_fixture::d("200")}}};
            const auto flat=accrueBill(plan,opening,averaged,T+3000,now);CHECK(flat.settledTotal&&flat.settledTotal->numerator.text()=="1400000");}
        CHECK(bytes(path)==before);
        {auto other=binding;other.channels[0]="fixture.wrong-channel";EnergyHistoryLedger reader;CHECK(reader.open(path,other,now,EnergyHistoryOpen::ExistingQuery,limits)==S::Attribution&&bytes(path)==before);}
        auto request=energy_history_fixture::request(path);request.mode=EnergyHistoryOpen::ExistingQuery;std::string wire;CHECK(!encodeEnergyHistoryRequest(request,&wire)); // old write IPC gains no mode implicitly
    }
    // Independent frame oracle: exact pointwise sums/coverage survive compaction, partial channels, zero, drop and session gaps.
    struct Frame{uint64_t mw=0;unsigned covered=0;};std::vector<Frame> expected;uint32_t random=987654321;
    const auto next=[&](){random=random*1664525u+1013904223u;return random;};
    for(bool compact:{false,true}){const auto path=root/(compact?"random-compacted.db":"random-raw.db");auto limits=policy(compact);if(compact)limits.compaction->afterBatches=7;
        auto source=binding;source.channels.push_back("fixture.channel-C");std::optional<uint32_t> previous[3];std::string session;uint64_t anchor=0,continuity=0;expected.clear();random=987654321;
        {EnergyHistoryLedger ledger;CHECK(ledger.open(path,source,now,EnergyHistoryOpen::CreateNew,limits)==S::Ok);
            for(unsigned frame=0;frame<=400;++frame){auto input=batch(frame+1,compact);const bool fresh=frame%97==0,drop=frame%31==0;
                if(fresh){session="fixture.parent-"+std::to_string(frame);anchor=frame*100;continuity=0;input.start=EnergyHistorySession{session,T+static_cast<int64_t>(anchor),0,5000};}
                if(drop)++continuity;
                Frame held;if(frame&&!fresh&&!drop)for(const auto &power:previous)if(power){held.mw+=*power;++held.covered;}
                if(frame)expected.push_back(held);
                input.sessionId=session;input.observedAtMs=T+frame*100;
                for(unsigned channel=0;channel<3;++channel){std::optional<uint32_t> power;if(next()%11)power=next()%5?next()%1000000:0;
                    input.readings.push_back({channel,input.observedAtMs,frame*100-anchor,continuity,power});previous[channel]=power;}
                CHECK(ledger.apply(input,now)==S::Ok);}}
        {EnergyHistoryLedger ledger;CHECK(ledger.open(path,source,now,EnergyHistoryOpen::ExistingQuery,limits)==S::Ok);
            for(unsigned trial=0;trial<300;++trial){const int64_t begin=next()%39000,end=begin+1+next()%1000;uint64_t mwms=0,covered=0;std::vector<PowerInterval> exact;
                for(size_t frame=0;frame<expected.size();++frame){const auto a=(std::max)(begin,static_cast<int64_t>(frame*100)),z=(std::min)(end,static_cast<int64_t>((frame+1)*100));if(z<=a)continue;
                    const auto &held=expected[frame];mwms+=held.mw*static_cast<uint64_t>(z-a);covered+=held.covered*static_cast<uint64_t>(z-a);
                    auto watts=electricity_fixture::d(std::to_string(held.mw));CHECK(watts.multiply(electricity_fixture::d("0.001"),&watts));
                    if(!exact.empty()&&exact.back().endUtcMs==T+a&&exact.back().watts.compare(watts)==0)exact.back().endUtcMs=T+z;else exact.push_back({T+a,T+z,watts});}
                const auto result=ledger.queryTrace(T+begin,T+end,now);auto energy=electricity_fixture::d(std::to_string(mwms));CHECK(energy.multiply(electricity_fixture::d("0.001"),&energy));
                const bool complete=covered==static_cast<uint64_t>(end-begin)*3;
                CHECK(result.status==S::Ok&&result.totals.coveredDeviceMs==covered&&result.totals.knownKwh.numerator.compare(energy)==0&&result.completeTrace.has_value()==complete);
                if(complete){CHECK(result.completeTrace->intervals.size()==exact.size());for(size_t i=0;i<exact.size();++i)CHECK(result.completeTrace->intervals[i].beginUtcMs==exact[i].beginUtcMs&&result.completeTrace->intervals[i].endUtcMs==exact[i].endUtcMs&&result.completeTrace->intervals[i].watts.compare(exact[i].watts)==0);}}}
    }
    for(bool compact:{false,true}){const auto limits=policy(compact);auto source=binding;source.channels.resize(1);const auto path=root/(compact?"cap-compacted.db":"cap-raw.db");
        {EnergyHistoryLedger ledger;CHECK(ledger.open(path,source,now,EnergyHistoryOpen::CreateNew,limits)==S::Ok);
            for(unsigned begin=0,seq=1;begin<514;begin+=128,++seq){auto input=batch(seq,compact);if(begin==0)input.start=EnergyHistorySession{input.sessionId,T,100,5000};
                for(unsigned i=begin;i<(std::min)(begin+128,514u);++i)input.readings.push_back(reading(0,i,i%2?1000u:0u));
                input.observedAtMs=input.readings.back().utcMs;CHECK(ledger.apply(input,now)==S::Ok);}}
        EnergyHistoryLedger reader;CHECK(reader.open(path,source,now,EnergyHistoryOpen::ExistingQuery,limits)==S::Ok);
        const auto exact=reader.queryTrace(T,T+512,now),tooMany=reader.queryTrace(T,T+513,now);
        CHECK(exact.completeTrace&&exact.completeTrace->intervals.size()==512&&tooMany.status==S::Capacity&&!tooMany.completeTrace&&tooMany.checkpoint.digest==exact.checkpoint.digest);}
    {auto source=binding;source.channels.clear();for(unsigned i=0;i<64;++i)source.channels.push_back("fixture.channel-"+std::to_string(i));
        source.evidence.origin=PowerOrigin::ObservedTelemetry;source.evidence.adapter="native-nvml-milliwatts";auto input=batch(1,false);
        input.start=EnergyHistorySession{input.sessionId,T,100,5000};input.observedAtMs=T+1;
        for(unsigned elapsed=0;elapsed<2;++elapsed)for(unsigned i=0;i<64;++i)input.readings.push_back(reading(i,elapsed,UINT32_MAX));
        EnergyHistoryLedger ledger;CHECK(ledger.open(root/"native-contract-fixture.db",source,now,EnergyHistoryOpen::CreateNew)==S::Ok&&ledger.apply(input,now)==S::Ok);
        const auto exact=ledger.queryTrace(T,T+1,now,1);CHECK(exact.completeTrace&&exact.completeTrace->intervals.size()==1&&exact.totals.coveredDeviceMs==64&&exact.totals.knownKwh.numerator.text()=="274877906.88");
        CHECK(exact.completeTrace->evidence.origin==PowerOrigin::ObservedTelemetry&&exact.completeTrace->evidence.basis==PowerBasis::GpuBoard&&exact.completeTrace->evidence.adapter=="native-nvml-milliwatts");}
    std::filesystem::remove_all(root);return finishTests("energy trace");
}
