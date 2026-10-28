#include "energy_owner.h"
#include "electricity.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <ctime>

namespace om {
namespace {
constexpr uint64_t lastUtcMs=253402300799999ULL, clockGuardMs=2000;
std::string instant(uint64_t ms) {
    const auto time=static_cast<time_t>(ms/1000); tm g{};
#if defined(_WIN32)
    if(gmtime_s(&g,&time))return "unrepresentable UTC";
#else
    if(!gmtime_r(&time,&g))return "unrepresentable UTC";
#endif
    char text[32]; if(!strftime(text,sizeof(text),"%Y-%m-%dT%H:%M:%SZ",&g))return "unrepresentable UTC"; return text;
}
ExactDecimal decimal(uint64_t value){ExactDecimal d; ExactDecimal::parse(std::to_string(value),&d); return d;}
std::string amount(const ExactDecimal &value) {
    std::string text; if(!value.formatRatio(wattMsPerKwh,6,&text))return "N/A (arithmetic capacity)";
    if(!value.zero()&&(text=="0.000000"||text=="-0.000000"))return value.negative()?">-0.000001":"<0.000001";
    return text;
}
class EnergyOwner final: public SessionEnergySink {
    struct Job {SessionPowerInput input; uint64_t epoch=0;};
    struct Channel {bool seen=false; SessionPowerInput previous; uint64_t epoch=0;};
    struct Model {
        ElectricityPreferences preferences;
        uint64_t baseMono=0, lastMono=0, lastWall=0;
        SessionEnergySnapshot snapshot;
        ExactDecimal energy, cost, rate;
        std::vector<Channel> channels;
    };
    mutable std::mutex mutex_;
    std::mutex lifecycle_;
    std::condition_variable ready_;
    std::deque<Job> queue_;
    std::thread worker_;
    std::atomic<bool> cancel_{true};
    bool accepting_=false;
    std::shared_ptr<const SessionEnergySnapshot> published_=std::make_shared<const SessionEnergySnapshot>();
    uint64_t generation_=0, epoch_=0, dropped_=0;
    unsigned channels_=0;
    static EnergyView view(const Model &m) {
        EnergyView v; v.enabled=m.preferences.showEnergy;
        if(!v.enabled)return v;
        const auto &s=m.snapshot;
        if(s.evidence.origin==PowerOrigin::SyntheticFixture)v.lines.push_back("SYNTHETIC FIXTURE - not measured hardware energy.");
        v.lines.push_back("GPU BOARD sample-and-hold ESTIMATE - SESSION ONLY / not whole-PC or incremental mining energy");
        v.lines.push_back("Power source: "+s.evidence.adapter+" | ordinal session scope: "+s.evidence.scope);
        if(s.state==SessionEnergyState::ClockFault)v.lines.push_back("STOPPED: UTC/monotonic clock fault; new explicit parent session required.");
        if(s.state==SessionEnergyState::Capacity)v.lines.push_back("STOPPED: energy arithmetic/processing capacity; no automatic retry.");
        v.lines.push_back("Known covered board energy: "+(s.coveredDeviceMs?amount(m.energy)+" kWh":"N/A (no covered power interval)"));
        v.lines.push_back("Device-time coverage: "+std::to_string(s.coveredDeviceMs)+" / "+std::to_string(s.requestedDeviceMs)+" GPU-ms across "+std::to_string(s.channels)+" selected boards");
        v.lines.push_back("Observed horizon UTC: "+instant(s.beginUtcMs)+" through "+instant(s.throughUtcMs)+" (no tail extrapolation)");
        v.lines.push_back("Includes board idle/display/game/donation power; gaps are not zero. Queue drops/discards: "+std::to_string(s.dropped));
        if(m.preferences.tariffEnabled) {
            const auto &p=m.preferences;
            v.lines.push_back("USER-DECLARED flat ENERGY-ONLY: "+p.ratePerKwh+" "+p.currency+"/kWh | revision "+p.version);
            v.lines.push_back("Country/region: "+p.country+" / "+p.region+" | supplier/product: "+p.supplier+" / "+p.product);
            v.lines.push_back("Declared effective UTC [start,end): "+instant(p.effectiveFromUtc*1000)+" to "+instant(p.effectiveThroughUtc*1000));
            v.lines.push_back("User confirmation UTC: "+instant(p.confirmedUtc*1000)+" (not supplier verification)");
            v.lines.push_back("Known priced board-energy cost: "+(s.pricedDeviceMs?amount(m.cost)+" "+p.currency:"N/A (no covered interval inside the effective version/clock guard)"));
            v.lines.push_back("Priced coverage: "+std::to_string(s.pricedDeviceMs)+" / "+std::to_string(s.coveredDeviceMs)+" covered GPU-ms; reporting clock "+(p.fixedStandardTime?"fixed STANDARD "+std::to_string(p.fixedOffsetSeconds)+" seconds":"explicit UTC"));
            v.lines.push_back("EXCLUDES tiers/TOU/dynamic rates, taxes, fees and shared-meter consequences. Not a utility bill or net profit.");
        } else v.lines.push_back("Tariff off/unconfigured; no electricity cost estimate.");
        v.lines.push_back("Energy selection "+std::to_string(m.preferences.recordIntervalMs)+" ms; max hold gap "+std::to_string(m.preferences.maximumGapMs)+" ms; mapped UTC +/-2000 ms guard.");
        return v;
    }
    static bool advance(Model &m,const Job &job) {
        const auto &p=job.input;
        if(!p.utcMs||p.utcMs>lastUtcMs||p.monotonicMs<m.baseMono||p.monotonicMs<m.lastMono||p.utcMs<m.lastWall)return false;
        const auto elapsed=p.monotonicMs-m.baseMono;
        if(elapsed>lastUtcMs-m.snapshot.beginUtcMs)return false;
        const auto mapped=m.snapshot.beginUtcMs+elapsed;
        if((p.utcMs>mapped?p.utcMs-mapped:mapped-p.utcMs)>clockGuardMs)return false;
        auto &channel=m.channels[p.channel];
        if(channel.seen&&p.monotonicMs<=channel.previous.monotonicMs)return false;
        if(channel.seen&&channel.epoch==job.epoch&&channel.previous.milliwatts) {
            const auto dt=p.monotonicMs-channel.previous.monotonicMs;
            if(dt<=m.preferences.maximumGapMs) {
                ExactDecimal wattMs, milliScale; ExactDecimal::parse("0.001",&milliScale);
                if(!decimal(*channel.previous.milliwatts).multiply(decimal(dt),&wattMs)||!wattMs.multiply(milliScale,&wattMs)||!m.energy.add(wattMs,&m.energy))throw 1;
                m.snapshot.coveredDeviceMs+=dt;
                const auto begin=m.snapshot.beginUtcMs+(channel.previous.monotonicMs-m.baseMono);
                if(m.preferences.tariffEnabled && begin>=m.preferences.effectiveFromUtc*1000+clockGuardMs &&
                    mapped<=m.preferences.effectiveThroughUtc*1000 && clockGuardMs<=m.preferences.effectiveThroughUtc*1000-mapped) {
                    ExactDecimal priced; if(!wattMs.multiply(m.rate,&priced)||!m.cost.add(priced,&m.cost))throw 1;
                    m.snapshot.pricedDeviceMs+=dt;
                }
            }
        }
        channel.seen=true;channel.previous=p;channel.epoch=job.epoch;
        m.lastMono=p.monotonicMs;m.lastWall=p.utcMs;
        m.snapshot.throughUtcMs=mapped;m.snapshot.throughMonoMs=p.monotonicMs;
        m.snapshot.requestedDeviceMs=elapsed*m.snapshot.channels;
        return true;
    }
    void run(Model model) noexcept {
        try {
            for(;;) {
                Job job;
                { std::unique_lock<std::mutex> lock(mutex_);ready_.wait(lock,[&]{return cancel_.load()||!queue_.empty();});if(cancel_)return;job=queue_.front();queue_.pop_front(); }
                if(!advance(model,job))model.snapshot.state=SessionEnergyState::ClockFault;
                model.snapshot.knownEnergyNumerator=model.energy.text();model.snapshot.knownCostNumerator=model.cost.text();
                {std::lock_guard<std::mutex> lock(mutex_);if(cancel_)return;
                    if(model.snapshot.state!=SessionEnergyState::Running){accepting_=false;dropped_+=queue_.size();queue_.clear();}
                    model.snapshot.dropped=dropped_;}
                model.snapshot.view=view(model);
                auto next=std::make_shared<SessionEnergySnapshot>(model.snapshot);
                { std::lock_guard<std::mutex> lock(mutex_); if(cancel_)return;
                  next->revision=published_->revision+1;published_=std::move(next);
                  if(model.snapshot.state!=SessionEnergyState::Running){cancel_=true;return;} }
            }
        } catch(...) {
            try { std::lock_guard<std::mutex> lock(mutex_); if(cancel_)return;
                accepting_=false;dropped_+=queue_.size();queue_.clear();
                auto fault=std::make_shared<SessionEnergySnapshot>(*published_);fault->state=SessionEnergyState::Capacity;fault->revision++;fault->dropped=dropped_;
                fault->view.lines.push_back("STOPPED: processing capacity; known prior snapshot retained, no automatic retry.");
                published_=std::move(fault);cancel_=true;
            } catch(...) {cancel_=true;}
        }
    }
   public:
    ~EnergyOwner()override{stop();}
    bool start(uint64_t generation,uint64_t utc,uint64_t mono,unsigned channels,const ElectricityPreferences &p,PowerOrigin origin)override {
        std::lock_guard<std::mutex> life(lifecycle_);std::lock_guard<std::mutex> lock(mutex_);
        if(worker_.joinable()||!generation||generation<=generation_||!utc||utc>lastUtcMs||!channels||channels>64||!p.collectEnergy||!electricityPreferencesValid(p)||
            (origin!=PowerOrigin::ObservedTelemetry&&origin!=PowerOrigin::SyntheticFixture))return false;
        if(p.tariffEnabled&&p.confirmedUtc>utc/1000)return false;
        Model m;m.preferences=p;m.baseMono=m.lastMono=mono;m.lastWall=utc;m.channels.resize(channels);
        if(p.tariffEnabled&&!ExactDecimal::parse(p.ratePerKwh,&m.rate))return false;
        m.snapshot.state=SessionEnergyState::Running;m.snapshot.generation=generation;m.snapshot.channels=channels;
        m.snapshot.evidence={PowerBasis::GpuBoard,origin,"selected-gpu-boards/session-"+std::to_string(generation),origin==PowerOrigin::SyntheticFixture?"fixture.milliwatts":"native-nvml-milliwatts"};
        m.snapshot.beginUtcMs=m.snapshot.throughUtcMs=utc;m.snapshot.throughMonoMs=mono;m.snapshot.view=view(m);
        try {generation_=generation;channels_=channels;epoch_=dropped_=0;cancel_=false;accepting_=true;published_=std::make_shared<const SessionEnergySnapshot>(m.snapshot);
            worker_=std::thread([this,m=std::move(m)]()mutable{run(std::move(m));});return true;
        } catch(...){accepting_=false;cancel_=true;return false;}
    }
    EnergyAdmission submit(uint64_t generation,SessionPowerInput input)override {
        std::lock_guard<std::mutex> lock(mutex_);
        if(generation!=generation_)return EnergyAdmission::StaleGeneration;
        if(cancel_||!accepting_)return EnergyAdmission::Stopped;
        if(input.channel>=channels_)return EnergyAdmission::Invalid;
        if(queue_.size()>=128){++dropped_;++epoch_;return EnergyAdmission::Full;}
        try {queue_.push_back({input,epoch_});ready_.notify_one();return EnergyAdmission::Queued;}catch(...){++dropped_;++epoch_;return EnergyAdmission::Full;}
    }
    void requestStop()override {
        std::lock_guard<std::mutex> lock(mutex_);cancel_=true;accepting_=false;dropped_+=queue_.size();queue_.clear();
        try {auto stopped=std::make_shared<SessionEnergySnapshot>(*published_);stopped->state=SessionEnergyState::Stopped;stopped->dropped=dropped_;stopped->revision++;published_=std::move(stopped);}catch(...){}
        ready_.notify_all();
    }
    void stop()override {std::lock_guard<std::mutex> life(lifecycle_);requestStop();if(worker_.joinable())worker_.join();}
    std::shared_ptr<const SessionEnergySnapshot> snapshot()const override{std::lock_guard<std::mutex> lock(mutex_);return published_;}
};
}
std::unique_ptr<SessionEnergySink> makeSessionEnergyOwner(){return std::make_unique<EnergyOwner>();}
} // namespace om
