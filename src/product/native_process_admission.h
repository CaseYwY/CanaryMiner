// Cooperative uncertainty fence shared by statically linked native process owners in this parent.
// No process discovery, force cleanup, test hook, credentials, OS calls or startup authority.
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace om {
namespace native_process_detail {
struct State {
    std::atomic<uint64_t> epoch{1},unresolved{0};
};
inline State &state(){static State value;return value;}
struct ScopeState {uint64_t epoch=0;bool active=false;};
inline ScopeState &scope(){static thread_local ScopeState value;return value;}
inline bool scopePermits(uint64_t epoch)noexcept{const auto &value=scope();return !value.active||value.epoch==epoch;}
inline void increment(std::atomic<uint64_t> &value)noexcept{
    auto current=value.load();
    while(current!=std::numeric_limits<uint64_t>::max()&&!value.compare_exchange_weak(current,current+1)){}
}
} // namespace native_process_detail

inline uint64_t nativeProcessEpoch()noexcept{return native_process_detail::state().epoch.load();}
inline bool nativeProcessUnresolved()noexcept{
    auto &state=native_process_detail::state();
    return state.unresolved.load()!=0||state.epoch.load()==std::numeric_limits<uint64_t>::max();
}
inline bool nativeProcessEpochCurrent(uint64_t epoch)noexcept{
    return epoch&&epoch==nativeProcessEpoch()&&!nativeProcessUnresolved();
}
inline uint64_t nativeProcessAdmissionEpoch()noexcept{
    const auto &value=native_process_detail::scope();return value.active?value.epoch:nativeProcessEpoch();
}

// An owned task explicitly carries its original parent epoch into its own thread. No implicit thread propagation.
class NativeProcessScope {
public:
    explicit NativeProcessScope(uint64_t epoch)noexcept:previous_(native_process_detail::scope()){
        auto &value=native_process_detail::scope();value.epoch=previous_.active&&previous_.epoch!=epoch?0:epoch;value.active=true;
    }
    NativeProcessScope(const NativeProcessScope &)=delete;
    NativeProcessScope &operator=(const NativeProcessScope &)=delete;
    ~NativeProcessScope(){native_process_detail::scope()=previous_;}
private:
    native_process_detail::ScopeState previous_;
};

// Bind once at the first explicit owner start, not when a later queued native operation finally executes.
class NativeProcessLifetime {
public:
    NativeProcessLifetime()=default;
    NativeProcessLifetime(const NativeProcessLifetime &)=delete;
    NativeProcessLifetime &operator=(const NativeProcessLifetime &)=delete;
    bool begin()noexcept{
        if(!bound_){bound_=true;const auto candidate=nativeProcessAdmissionEpoch();
            epoch_=nativeProcessEpochCurrent(candidate)&&native_process_detail::scopePermits(candidate)?candidate:0;}
        return permitted();
    }
    bool current()const noexcept{return bound_&&nativeProcessEpochCurrent(epoch_);}
    bool permitted()const noexcept{return current()&&native_process_detail::scopePermits(epoch_);}
    bool bound()const noexcept{return bound_;}
    uint64_t epoch()const noexcept{return epoch_;}
private:
    uint64_t epoch_=0;bool bound_=false;
};

class NativeProcessAdmission {
public:
    NativeProcessAdmission()=default;
    NativeProcessAdmission(const NativeProcessAdmission &)=delete;
    NativeProcessAdmission &operator=(const NativeProcessAdmission &)=delete;
    // Owners separately reserve bounded native/quarantine storage before creation.
    bool begin()noexcept{
        if(admitted_||blocked_)return false;
        epoch_=nativeProcessAdmissionEpoch();
        if(!nativeProcessEpochCurrent(epoch_))return false;
        admitted_=true;
        if(unchanged())return true;
        admitted_=false;return false;
    }
    bool permitted()const noexcept{return admitted_&&unchanged();}
    bool unchanged()const noexcept{return nativeProcessEpochCurrent(epoch_)&&native_process_detail::scopePermits(epoch_);}
    bool invalidated()const noexcept{return epoch_&&!unchanged();}
    void markUnresolved()noexcept{
        if(blocked_)return;
        blocked_=true;
        auto &state=native_process_detail::state();
        native_process_detail::increment(state.unresolved);
        native_process_detail::increment(state.epoch);
    }
    // Only actual exit proof, or proof that creation never occurred, may call this.
    void settled()noexcept{
        if(blocked_){
            auto &count=native_process_detail::state().unresolved;
            auto current=count.load();
            while(current&&current!=std::numeric_limits<uint64_t>::max()&&!count.compare_exchange_weak(current,current-1)){}
            blocked_=false;
        }
        admitted_=false;
    }
    // Intentionally no destructor decrement: destroying a facade is not exit proof.
private:
    uint64_t epoch_=0;
    bool admitted_=false,blocked_=false;
};
} // namespace om
