// User-declared offline reference display only. No provider, income, optimizer or miner authorization.
#pragma once
#include "runtime_preferences.h"
#include "market_view.h"
#include <functional>
namespace om {
constexpr uint64_t kManualLastUtc = 253402300799ULL;
struct ManualMarketPreferences {
    bool enabled = false, fxEnabled = false;
    std::string prlUsdt, usdtUsd;
    uint64_t prlAsOfUtc = 0, fxAsOfUtc = 0, confirmedUtc = 0;
    unsigned validitySeconds = 3600; // validity is not a refresh cadence or a one-hour average
    unsigned maxFxSkewSeconds = 30;
    bool operator==(const ManualMarketPreferences &b) const {
        return enabled == b.enabled && fxEnabled == b.fxEnabled && prlUsdt == b.prlUsdt && usdtUsd == b.usdtUsd &&
            prlAsOfUtc == b.prlAsOfUtc && fxAsOfUtc == b.fxAsOfUtc && confirmedUtc == b.confirmedUtc && validitySeconds == b.validitySeconds && maxFxSkewSeconds == b.maxFxSkewSeconds;
    }
    bool operator!=(const ManualMarketPreferences &b) const { return !(*this == b); }
};
// Lexical/canonical input validation only; arithmetic stays in the exact-decimal library.
inline bool canonicalManualPrice(const std::string &input, std::string *out) {
    if (!out || input.empty() || input.size() > 64) return false;
    const auto dot = input.find('.');
    if (dot == 0 || dot == input.size()-1 || (dot != std::string::npos && input.find('.',dot+1) != std::string::npos)) return false;
    size_t digits = 0, scale = dot == std::string::npos ? 0 : input.size()-dot-1;
    for (char c : input) { if (c == '.') continue; if (c < '0' || c > '9') return false; ++digits; }
    if (digits > 38 || scale > 18) return false;
    std::string integer = input.substr(0,dot), fraction = dot == std::string::npos ? "" : input.substr(dot+1);
    const auto first = integer.find_first_not_of('0'); integer = first == std::string::npos ? "0" : integer.substr(first);
    while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();
    if (integer == "0" && fraction.empty()) return false;
    *out = integer+(fraction.empty()?"":"."+fraction); return true;
}
inline bool manualMarketValid(const ManualMarketPreferences &p, std::string *error = nullptr) {
    auto fail = [&](const char *s){if (error) *error=s; return false;};
    std::string normalized;
    if (p.validitySeconds < 60 || p.validitySeconds > 86400) return fail("Validity must be 60..86400 seconds; it never fetches a quote.");
    if (p.maxFxSkewSeconds > 3600) return fail("Maximum FX reference skew must be 0..3600 seconds.");
    if (p.prlAsOfUtc > kManualLastUtc || p.fxAsOfUtc > kManualLastUtc || p.confirmedUtc > kManualLastUtc) return fail("Reference/confirmation UTC is outside the supported range.");
    if ((!p.prlUsdt.empty() && (!canonicalManualPrice(p.prlUsdt,&normalized) || normalized != p.prlUsdt)) ||
        (!p.usdtUsd.empty() && (!canonicalManualPrice(p.usdtUsd,&normalized) || normalized != p.usdtUsd))) return fail("Prices must be canonical positive decimals (38 digits, 18 decimals maximum).");
    if (p.prlUsdt.empty() != (p.prlAsOfUtc == 0)) return fail("PRL price and its reference UTC must be supplied together.");
    if (p.usdtUsd.empty() != (p.fxAsOfUtc == 0)) return fail("FX price and its own reference UTC must be supplied together.");
    if (p.enabled && p.prlUsdt.empty()) return fail("Enter PRL/USDT and its reference UTC before enabling the display.");
    if (p.fxEnabled && p.usdtUsd.empty()) return fail("Enter USD per USDT and its own reference UTC before enabling FX.");
    if (p.enabled && (!p.confirmedUtc || p.confirmedUtc < p.prlAsOfUtc || (p.fxEnabled && p.confirmedUtc < p.fxAsOfUtc))) return fail("Enabled references cannot be in the future; supply their own UTC instants.");
    return true;
}
inline bool confirmManualMarket(ManualMarketPreferences *p, uint64_t nowUtc, std::string *error) {
    if (!p || !nowUtc || nowUtc > kManualLastUtc) { if (error) *error="UTC clock unavailable; nothing saved."; return false; }
    auto candidate = *p; candidate.confirmedUtc = nowUtc;
    if (!manualMarketValid(candidate,error)) return false;
    *p = std::move(candidate); return true;
}
inline std::string manualMarketJson(const ManualMarketPreferences &p) {
    return "{\n  \"schema_version\": 1,\n  \"asset\": \"pearl-mainnet-native-prl\",\n  \"token_quote\": \"USDT\",\n  \"fx_base\": \"USDT\",\n  \"fx_quote\": \"USD\",\n  \"enabled\": "+std::string(p.enabled?"true":"false")+
        ",\n  \"fx_enabled\": "+(p.fxEnabled?"true":"false")+",\n  \"prl_usdt\": "+jsonQuote(p.prlUsdt)+",\n  \"prl_as_of_utc\": "+std::to_string(p.prlAsOfUtc)+
        ",\n  \"usdt_usd\": "+jsonQuote(p.usdtUsd)+",\n  \"fx_as_of_utc\": "+std::to_string(p.fxAsOfUtc)+",\n  \"confirmed_utc\": "+std::to_string(p.confirmedUtc)+",\n  \"validity_seconds\": "+std::to_string(p.validitySeconds)+",\n  \"max_fx_skew_seconds\": "+std::to_string(p.maxFxSkewSeconds)+"\n}\n";
}
inline bool parseManualMarket(const std::string &text, ManualMarketPreferences *out, std::string *error) {
    auto fail = [&](const char *s){if (error) *error=s; return false;};
    JsonValue root;
    if (!out || text.size() > 4096 || !parseJsonStrict(text,&root,nullptr) || !root.isObject()) return fail("Invalid offline-market JSON; no values applied.");
    const char *keys[] = {"schema_version","asset","token_quote","fx_base","fx_quote","enabled","fx_enabled","prl_usdt","prl_as_of_utc","usdt_usd","fx_as_of_utc","confirmed_utc","validity_seconds","max_fx_skew_seconds"};
    if (root.members.size() != 14) return fail("Missing/unknown offline-market preference; file preserved.");
    for (const auto *key : keys) if (!root.find(key)) return fail("Missing/unknown offline-market preference; file preserved.");
    uint64_t version = 0, validity = 0, skew = 0;
    if (!root.find("schema_version")->asUint64(&version) || version != 1) return fail("Unsupported offline-market schema; file preserved.");
    auto is = [&](const char *key, const char *value){const auto *v=root.find(key); return v->isString() && v->text == value;};
    if (!is("asset","pearl-mainnet-native-prl") || !is("token_quote","USDT") || !is("fx_base","USDT") || !is("fx_quote","USD")) return fail("Offline-market asset/currency identity mismatch.");
    ManualMarketPreferences p;
    const auto *enabled = root.find("enabled"), *fx = root.find("fx_enabled"), *price = root.find("prl_usdt"), *conversion = root.find("usdt_usd");
    if (!enabled->isBool() || !fx->isBool() || !price->isString() || !conversion->isString() ||
        !root.find("prl_as_of_utc")->asUint64(&p.prlAsOfUtc) || !root.find("fx_as_of_utc")->asUint64(&p.fxAsOfUtc) ||
        !root.find("confirmed_utc")->asUint64(&p.confirmedUtc) || !root.find("validity_seconds")->asUint64(&validity) || validity > 86400 || !root.find("max_fx_skew_seconds")->asUint64(&skew) || skew > 3600) return fail("Invalid offline-market preference type/range.");
    p.enabled = enabled->boolean; p.fxEnabled = fx->boolean; p.validitySeconds = static_cast<unsigned>(validity);
    p.maxFxSkewSeconds = static_cast<unsigned>(skew);
    if ((!price->text.empty() && !canonicalManualPrice(price->text,&p.prlUsdt)) || (!conversion->text.empty() && !canonicalManualPrice(conversion->text,&p.usdtUsd))) return fail("Invalid offline-market decimal; no rounding or numeric JSON allowed.");
    if (!manualMarketValid(p,error)) return false;
    *out = std::move(p); return true;
}
// Single terminal owner only: projector latches observed clock faults. Not a policy/finance input or concurrent API.
using ManualMarketProjector = std::function<PublicMarketView(uint64_t nowUtc, uint64_t nowMonoMs)>;
using ManualMarketFactory = std::function<ManualMarketProjector(const ManualMarketPreferences &,uint64_t,uint64_t)>;

// Small independent atomic JSON file. No schema sharing with UI/runtime/credentials/credit stores.
class ManualMarketStore {
   public:
    explicit ManualMarketStore(std::filesystem::path path) : path_(std::move(path)) {}
    bool load(ManualMarketPreferences *out, std::string *error) {
        if (!out) return fail(error,"Offline-market output is unavailable.");
        std::string text; bool exists = false;
        if (!read(&text,&exists,error)) return false;
        ManualMarketPreferences p; if (exists && !parseManualMarket(text,&p,error)) return false;
        original_=std::move(text); existed_=exists; loaded_=true; *out=std::move(p); return true;
    }
    bool save(const ManualMarketPreferences &p, std::string *error) {
        if (!loaded_) return fail(error,"Offline-market target not loaded; nothing saved.");
        if (!manualMarketValid(p,error)) return false;
        std::error_code ec;
        if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path(),ec);
        if (ec) return fail(error,"Cannot create offline-market settings directory.");
        auto lockPath=path_; lockPath+=".lock"; RuntimePreferencesLease lease;
        if (!lease.acquire(lockPath)) return fail(error,"Offline-market settings locked or invalid lock file.");
        std::string current; bool exists=false;
        if (!read(&current,&exists,error)) return false;
        if (exists != existed_ || current != original_) return fail(error,"Offline-market settings changed externally; reopen before saving.");
        static std::atomic<unsigned> serial{0}; auto temporary=path_;
#if defined(_WIN32)
        temporary += ".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
#else
        temporary += ".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
#endif
        int fd=-1;
#if defined(_WIN32)
        if (_wsopen_s(&fd,temporary.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE) != 0) return fail(error,"Cannot create temporary offline-market settings.");
        FILE *file=_fdopen(fd,"wb");
#else
        fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if (fd<0) return fail(error,"Cannot create temporary offline-market settings.");
        FILE *file=fdopen(fd,"wb");
#endif
        if (!file) {
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temporary,ec); return fail(error,"Cannot open temporary offline-market settings.");
        }
        const auto text=manualMarketJson(p); bool ok=fwrite(text.data(),1,text.size(),file)==text.size() && fflush(file)==0;
#if defined(_WIN32)
        ok=(_commit(fd)==0)&&ok;
#else
        ok=(fsync(fd)==0)&&ok;
#endif
        ok=(fclose(file)==0)&&ok;
        if (ok) {
#if defined(_WIN32)
            ok=MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
            std::filesystem::rename(temporary,path_,ec); ok=!ec;
#endif
        }
        if (!ok) { std::filesystem::remove(temporary,ec); return fail(error,"Cannot replace offline-market settings; active display unchanged."); }
        original_=text; existed_=true; return true;
    }
   private:
    std::filesystem::path path_; std::string original_; bool loaded_=false, existed_=false;
    static bool fail(std::string *error,const char *s) {if(error)*error=s; return false;}
    bool read(std::string *text,bool *exists,std::string *error) const {
        std::error_code ec; const auto status=std::filesystem::symlink_status(path_,ec);
        if (ec==std::errc::no_such_file_or_directory || (!ec && status.type()==std::filesystem::file_type::not_found)) {text->clear(); *exists=false; return true;}
        if (ec || !std::filesystem::is_regular_file(status)) return fail(error,"Offline-market settings must be a regular file; preserved.");
        std::ifstream file(path_,std::ios::binary); if(!file)return fail(error,"Cannot read offline-market settings; preserved.");
        char buffer[4097]; file.read(buffer,sizeof(buffer));
        if(file.gcount()>4096 || file.bad())return fail(error,"Offline-market settings too large/unreadable; preserved.");
        text->assign(buffer,static_cast<size_t>(file.gcount())); *exists=true; return true;
    }
};
}  // namespace om
