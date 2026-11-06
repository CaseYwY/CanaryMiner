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

class NativeProcessAdmission {
public:
    NativeProcessAdmission()=default;
    NativeProcessAdmission(const NativeProcessAdmission &)=delete;
    NativeProcessAdmission &operator=(const NativeProcessAdmission &)=delete;
    // Owners separately reserve bounded native/quarantine storage before creation.
    bool begin()noexcept{
        if(admitted_||blocked_)return false;
        epoch_=nativeProcessEpoch();
        if(!nativeProcessEpochCurrent(epoch_))return false;
        admitted_=true;
        if(unchanged())return true;
        admitted_=false;return false;
    }
    bool permitted()const noexcept{return admitted_&&unchanged();}
    bool unchanged()const noexcept{return nativeProcessEpochCurrent(epoch_);}
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
