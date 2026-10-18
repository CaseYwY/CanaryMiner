// Explicit finite production Pearl attempt range. Allocation belongs to the owner.
#pragma once
#include <cstdint>
#include <string>
namespace om {
struct PearlWorkRange {
    bool enabled = false;
    uint64_t first = 0, last = 0; // inclusive; no wrapping or reset on new jobs
    bool valid() const noexcept {
        return enabled ? first <= last && last-first != UINT64_MAX : !first && !last;
    }
    bool contains(uint64_t value) const noexcept {
        return enabled && valid() && value >= first && value <= last;
    }
    uint64_t remaining(uint64_t value) const noexcept {
        return contains(value) ? last-value+1 : 0;
    }
    bool next(uint64_t value, uint64_t consumed, uint64_t *out) const noexcept {
        if (!out || !consumed || consumed >= remaining(value)) return false;
        *out = value+consumed; return true;
    }
};
inline bool pearlWorkHex(const std::string &s, uint64_t *out) noexcept {
    if (!out || s.size()!=18 || s[0]!='0' || s[1]!='x') return false;
    uint64_t value=0;
    for (size_t i=2;i<s.size();++i) {
        const char c=s[i];
        if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
        value=(value<<4)|static_cast<unsigned>(c<='9'?c-'0':c-'a'+10);
    }
    *out=value; return true;
}
inline bool parsePearlWorkRange(const std::string &s, PearlWorkRange *out) noexcept {
    PearlWorkRange value; value.enabled=true;
    if (!out || s.size()!=37 || s[18]!=':' ||
        !pearlWorkHex(s.substr(0,18),&value.first) ||
        !pearlWorkHex(s.substr(19),&value.last) || !value.valid()) return false;
    *out=value; return true;
}
inline bool pearlWorkRangePolicy(const PearlWorkRange &range, bool pearl,
                                bool benchmark, bool pool) noexcept {
    return range.valid() && (!range.enabled || (pearl && !benchmark && pool));
}
} // namespace om
