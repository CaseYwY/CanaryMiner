#include "energy_capture.h"
#include "energy_history_model.h"
#include "hash.h"
#include <algorithm>
#include <mutex>

namespace om {
namespace {
using namespace energy_history_detail;
bool hexKey(const std::string &s){return s.size()==32&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool canonicalDevice(std::string *s,PowerOrigin origin){
    if(origin==PowerOrigin::SyntheticFixture)return identifier(*s)&&s->rfind("fixture.",0)==0;
    if(origin!=PowerOrigin::ObservedTelemetry||s->size()!=40||s->rfind("GPU-",0)!=0)return false;
    for(size_t n=4;n<s->size();++n){auto &c=(*s)[n];if(n==12||n==17||n==22||n==27){if(c!='-')return false;}
        else{if(c>='A'&&c<='F')c=static_cast<char>(c-'A'+'a');if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;}}
    return true;
}
bool mappedKeys(const std::string &space,const std::vector<std::string> &devices,PowerOrigin origin,std::vector<std::string> *keys){
    if(!keys||!hexKey(space)||devices.empty()||devices.size()>64)return false;
    std::vector<std::string> result;std::set<std::string> unique;
    for(const auto &device:devices){if(device.size()>128)return false;auto id=device;if(!canonicalDevice(&id,origin))return false;
        const auto key="channel."+sha256Hex("energy-channel-v1|"+space+"|"+id);
        if(!unique.insert(key).second)return false;
        result.push_back(key);}
    *keys=std::move(result);return true;
}
bool validPolicy(const EnergyCapturePolicy &p){
    return p.persistenceAuthorized&&p.selectionMs>=500&&p.selectionMs<=60000&&p.maximumGapMs>=p.selectionMs&&p.maximumGapMs<=60000&&
        p.flushMs>=50&&p.flushMs<=60000&&p.bufferedReadings>=1&&p.bufferedReadings<=128&&p.batchReadings>=1&&p.batchReadings<=p.bufferedReadings;
}
bool sameBinding(const EnergyHistoryBinding &a,const EnergyHistoryBinding &b){return bindingText(a)==bindingText(b);}
class Capture final:public EnergyCaptureSink {
    EnergyCapturePlan plan_;
    EnergyHistoryOwner owner_;
    mutable std::mutex mutex_;
    std::mutex lifecycle_;
    EnergyCaptureSnapshot current_;
    std::shared_ptr<const EnergyCaptureSnapshot> published_=std::make_shared<const EnergyCaptureSnapshot>();
    std::vector<unsigned> mapping_;
    std::vector<std::optional<uint64_t>> selectedAt_,seenAt_;
    std::vector<EnergyHistoryReading> queue_;
    std::optional<EnergyHistorySession> session_;
    bool attempted_=false,stopped_=true,submittedSession_=false,clockSeen_=false;
    uint64_t lastMono_=0;
    uint64_t retryTicket_=0;
    int64_t lastWall_=0;
    void publish(){current_.buffered=queue_.size();++current_.revision;published_=std::make_shared<const EnergyCaptureSnapshot>(current_);}
    void gap(size_t count=1){
        current_.dropped+=count;
        if(current_.continuity==INT64_MAX){fault(EnergyCaptureState::Faulted);return;}
        ++current_.continuity;
    }
    void discard(){if(!queue_.empty()){const auto count=queue_.size();queue_.clear();gap(count);}}
    void fault(EnergyCaptureState state){
        current_.dropped+=queue_.size();queue_.clear();stopped_=true;current_.state=state;owner_.requestStop();current_.owner=owner_.snapshot();
    }
    bool sync(){
        const auto s=owner_.snapshot();const bool changed=!current_.owner||s->revision!=current_.owner->revision;
        current_.owner=s;
        if(stopped_)return changed;
        if(retryTicket_&&s->state==EnergyHistoryOwnerState::Ready&&!s->uncertainBatch&&s->reply&&s->reply->ticket==retryTicket_&&s->reply->recovery&&
            s->reply->recovery->state==EnergyRecoveryState::Confirmed&&s->reply->recovery->status==EnergyHistoryStatus::Ok&&s->reply->applied&&
            (*s->reply->applied==EnergyHistoryStatus::Ok||*s->reply->applied==EnergyHistoryStatus::Replay)){
            current_.reconciled=s;retryTicket_=0;
        }
        if(s->state==EnergyHistoryOwnerState::NeedsReview){current_.state=EnergyCaptureState::NeedsReview;discard();}
        else if(s->state==EnergyHistoryOwnerState::Faulted||s->state==EnergyHistoryOwnerState::Stopped){
            current_.state=EnergyCaptureState::Faulted;discard();stopped_=true;
        }else if(s->state==EnergyHistoryOwnerState::Ready){
            // A reused identity is never quietly continued, even after an inspected Confirmed receipt.
            if(!submittedSession_&&s->reply&&s->reply->checkpoint.sessionId==plan_.sessionId){fault(EnergyCaptureState::Faulted);}
            else current_.state=EnergyCaptureState::Collecting;
        }else if(s->state==EnergyHistoryOwnerState::Busy&&current_.state==EnergyCaptureState::Collecting){}
        else current_.state=EnergyCaptureState::Starting;
        return changed;
    }
    bool clock(int64_t wall,uint64_t mono){
        if(!instant(wall)||mono>INT64_MAX||(clockSeen_&&(wall<lastWall_||mono<lastMono_)))return false;
        if(session_){const auto &s=*session_;
            if(mono<s.anchorMonoMs||mono-s.anchorMonoMs>static_cast<uint64_t>(lastUtcMs-s.anchorUtcMs))return false;
            const auto mapped=s.anchorUtcMs+static_cast<int64_t>(mono-s.anchorMonoMs);
            if(static_cast<uint64_t>(mapped>wall?mapped-wall:wall-mapped)>clockGuardMs)return false;
        }
        clockSeen_=true;lastMono_=mono;lastWall_=wall;return true;
    }
   public:
    Capture(std::filesystem::path helper,std::filesystem::path file,EnergyCapturePlan plan,unsigned timeout,EnergyHistoryClock clock)
        :plan_(std::move(plan)),owner_(std::move(helper),std::move(file),plan_.binding,plan_.limits,timeout,std::move(clock),true,plan_.fence){}
    ~Capture()override{stop();}
    bool start(uint64_t generation,const std::vector<std::string> &ids)override{
        std::lock_guard<std::mutex> life(lifecycle_);std::lock_guard<std::mutex> lock(mutex_);
        if(attempted_)return false;
        attempted_=true;
        EnergyHistoryBinding derived;std::vector<std::string> keys;
        const bool sessionKey=plan_.binding.evidence.origin==PowerOrigin::SyntheticFixture?
            identifier(plan_.sessionId)&&plan_.sessionId.rfind("fixture.",0)==0:plan_.sessionId.rfind("session.",0)==0&&hexKey(plan_.sessionId.substr(8));
        if(!generation||!sessionKey||!validPolicy(plan_.policy)||!validEnergyHistoryLimits(plan_.limits)||plan_.policy.batchReadings>plan_.limits.maxReadings||
            !makeEnergyCaptureBinding(plan_.privateNamespace,plan_.binding.historyBeginUtcMs,ids,plan_.binding.evidence.origin,&derived)||
            !sameBinding(derived,plan_.binding)||!mappedKeys(plan_.privateNamespace,ids,plan_.binding.evidence.origin,&keys))return false;
        for(const auto &key:keys)mapping_.push_back(static_cast<unsigned>(std::lower_bound(derived.channels.begin(),derived.channels.end(),key)-derived.channels.begin()));
        selectedAt_.resize(keys.size());seenAt_.resize(keys.size());queue_.reserve(plan_.policy.bufferedReadings);
        plan_.privateNamespace.clear(); // no raw device IDs retained; binding keys remain private correlation identifiers
        current_.generation=generation;current_.binding=plan_.binding;current_.state=EnergyCaptureState::Starting;
        if(!owner_.start(generation,plan_.mode)){current_.state=EnergyCaptureState::Faulted;current_.owner=owner_.snapshot();publish();return false;}
        stopped_=false;current_.owner=owner_.snapshot();publish();return true;
    }
    EnergyCaptureAdmission submit(uint64_t generation,const SessionPowerInput &in)override{
        std::lock_guard<std::mutex> lock(mutex_);
        if(generation!=current_.generation)return EnergyCaptureAdmission::StaleGeneration;
        if(stopped_)return EnergyCaptureAdmission::Stopped;
        sync();
        if(stopped_){publish();return EnergyCaptureAdmission::Stopped;}
        if(in.channel>=mapping_.size()){gap();publish();return EnergyCaptureAdmission::Invalid;}
        if(in.utcMs>static_cast<uint64_t>(lastUtcMs)||!clock(static_cast<int64_t>(in.utcMs),in.monotonicMs)){
            ++current_.dropped;fault(EnergyCaptureState::ClockFault);publish();return EnergyCaptureAdmission::Invalid;}
        auto &seen=seenAt_[in.channel];
        if(seen&&in.monotonicMs<=*seen){gap();publish();return EnergyCaptureAdmission::Invalid;}seen=in.monotonicMs;
        if(current_.state!=EnergyCaptureState::Collecting){gap();publish();return EnergyCaptureAdmission::NotReady;}
        auto &selected=selectedAt_[in.channel];
        if(selected&&in.monotonicMs-*selected<plan_.policy.selectionMs){
            // Decimation is intentional; a known failed sensor read must still break the held-power projection.
            if(!in.milliwatts)gap(0);
            ++current_.selectedOut;publish();return EnergyCaptureAdmission::Skipped;
        }
        selected=in.monotonicMs;
        if(queue_.size()>=plan_.policy.bufferedReadings){gap();publish();return EnergyCaptureAdmission::Full;}
        if(!session_){
            const auto &head=current_.owner->reply->checkpoint;
            if(static_cast<int64_t>(in.utcMs)<plan_.binding.historyBeginUtcMs||static_cast<int64_t>(in.utcMs)<head.observedAtMs){
                ++current_.dropped;fault(EnergyCaptureState::ClockFault);publish();return EnergyCaptureAdmission::Invalid;}
            session_=EnergyHistorySession{plan_.sessionId,static_cast<int64_t>(in.utcMs),in.monotonicMs,plan_.policy.maximumGapMs};
        }
        queue_.push_back({mapping_[in.channel],static_cast<int64_t>(in.utcMs),in.monotonicMs,current_.continuity,in.milliwatts});
        publish();return EnergyCaptureAdmission::Buffered;
    }
    void markGap(uint64_t generation)override{
        std::lock_guard<std::mutex> lock(mutex_);if(stopped_||generation!=current_.generation)return;
        gap(0);publish();
    }
    void tick(int64_t wall,uint64_t mono)override{
        std::lock_guard<std::mutex> lock(mutex_);if(stopped_)return;
        const bool changed=sync();if(stopped_){publish();return;}
        if(!clock(wall,mono)){fault(EnergyCaptureState::ClockFault);publish();return;}
        if(current_.state!=EnergyCaptureState::Collecting||current_.owner->state!=EnergyHistoryOwnerState::Ready||queue_.empty()||
            (queue_.size()<plan_.policy.batchReadings&&mono-queue_.front().monotonicMs<plan_.policy.flushMs)){
            if(changed)publish();
            return;
        }
        const auto count=(std::min)(queue_.size(),plan_.policy.batchReadings);const auto &last=queue_[count-1];
        const auto mapped=session_->anchorUtcMs+static_cast<int64_t>(last.monotonicMs-session_->anchorMonoMs);
        // A permitted small negative wall skew cannot turn a mapped reading into a future observation.
        if(wall<mapped){if(changed)publish();return;}
        EnergyHistoryBatch batch;batch.sequence=current_.owner->reply->checkpoint.sequence+1;batch.id="energy."+std::to_string(batch.sequence);
        batch.sessionId=plan_.sessionId;batch.observedAtMs=wall;if(!submittedSession_)batch.start=session_;
        batch.readings.assign(queue_.begin(),queue_.begin()+count);
        const auto status=owner_.submit(current_.generation,current_.owner->revision,batch);
        if(status==EnergyHistoryAdmission::Queued){queue_.erase(queue_.begin(),queue_.begin()+count);submittedSession_=true;}
        else if(status==EnergyHistoryAdmission::Invalid){fault(EnergyCaptureState::Faulted);}
        // NotReady/ReviewChanged retain the exact unsent queue. No sequence is consumed by admission failure.
        sync();publish();
    }
    EnergyHistoryAdmission inspect(uint64_t generation,uint64_t revision)override{
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopped_)return EnergyHistoryAdmission::NotReady;
        const bool changed=sync();if(!queue_.empty()){if(changed)publish();return EnergyHistoryAdmission::NotReady;}
        const auto result=owner_.refresh(generation,revision);sync();publish();return result;
    }
    EnergyHistoryAdmission retryReviewed(uint64_t generation,uint64_t revision)override{
        std::lock_guard<std::mutex> lock(mutex_);if(stopped_)return EnergyHistoryAdmission::NotReady;
        const bool changed=sync();if(current_.state!=EnergyCaptureState::NeedsReview){if(changed)publish();return EnergyHistoryAdmission::NotReady;}
        const auto ticket=current_.owner->lastTicket+1;const auto result=owner_.retryReviewed(generation,revision);
        if(result==EnergyHistoryAdmission::Queued)retryTicket_=ticket;
        sync();publish();return result;
    }
    void requestStop()override{
        std::lock_guard<std::mutex> lock(mutex_);attempted_=true;if(current_.state==EnergyCaptureState::Stopped)return;
        stopped_=true;discard();owner_.requestStop();current_.owner=owner_.snapshot();current_.state=EnergyCaptureState::Stopped;publish();
    }
    void stop()override{std::lock_guard<std::mutex> life(lifecycle_);requestStop();owner_.stop();}
    std::shared_ptr<const EnergyCaptureSnapshot> snapshot()const override{std::lock_guard<std::mutex> lock(mutex_);return published_;}
};
}
bool makeEnergyCaptureBinding(const std::string &space,int64_t begin,const std::vector<std::string> &ids,PowerOrigin origin,EnergyHistoryBinding *out){
    if(!out||!instant(begin))return false;
    EnergyHistoryBinding binding;if(!mappedKeys(space,ids,origin,&binding.channels))return false;
    std::sort(binding.channels.begin(),binding.channels.end());std::string selection="energy-selection-v1|"+space;
    for(const auto &key:binding.channels)selection+="|"+key;
    binding.historyBeginUtcMs=begin;binding.evidence={PowerBasis::GpuBoard,origin,"selection."+sha256Hex(selection),
        origin==PowerOrigin::SyntheticFixture?"fixture.capture":"native-nvml-milliwatts"};
    if(!validEnergyHistoryBinding(binding))return false;
    *out=std::move(binding);return true;
}
std::unique_ptr<EnergyCaptureSink> makeEnergyCapture(std::filesystem::path helper,std::filesystem::path file,EnergyCapturePlan plan,unsigned timeout,EnergyHistoryClock clock){
    return std::make_unique<Capture>(std::move(helper),std::move(file),std::move(plan),timeout,std::move(clock));
}
} // namespace om
