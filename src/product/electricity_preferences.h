// User-declared flat energy-only proxy and optional session observation. No utility certification, income or policy permission.
#pragma once
#include "runtime_preferences.h"
#include <vector>
namespace om {
struct ElectricityPreferences {
    bool collectEnergy=false, showEnergy=true, tariffEnabled=false, exclusionsAcknowledged=false;
    unsigned recordIntervalMs=1000, maximumGapMs=5000;
    std::string country, region, supplier, product, version, currency, ratePerKwh;
    bool fixedStandardTime=false;
    int fixedOffsetSeconds=0;
    uint64_t effectiveFromUtc=0, effectiveThroughUtc=0, confirmedUtc=0;
    bool operator==(const ElectricityPreferences &b) const {
        return collectEnergy==b.collectEnergy && showEnergy==b.showEnergy && tariffEnabled==b.tariffEnabled && exclusionsAcknowledged==b.exclusionsAcknowledged &&
            recordIntervalMs==b.recordIntervalMs && maximumGapMs==b.maximumGapMs && country==b.country && region==b.region && supplier==b.supplier && product==b.product &&
            version==b.version && currency==b.currency && ratePerKwh==b.ratePerKwh && fixedStandardTime==b.fixedStandardTime && fixedOffsetSeconds==b.fixedOffsetSeconds &&
            effectiveFromUtc==b.effectiveFromUtc && effectiveThroughUtc==b.effectiveThroughUtc && confirmedUtc==b.confirmedUtc;
    }
    bool operator!=(const ElectricityPreferences &b) const { return !(*this==b); }
};
inline bool sameElectricityDeclaration(const ElectricityPreferences &a,const ElectricityPreferences &b) {
    return a.country==b.country && a.region==b.region && a.supplier==b.supplier && a.product==b.product && a.currency==b.currency &&
        a.ratePerKwh==b.ratePerKwh && a.fixedStandardTime==b.fixedStandardTime && a.fixedOffsetSeconds==b.fixedOffsetSeconds &&
        a.effectiveFromUtc==b.effectiveFromUtc && a.effectiveThroughUtc==b.effectiveThroughUtc;
}
inline bool electricityLabel(const std::string &s) {
    if(s.size()>64)return false;
    for(unsigned char c:s)if(c<32 || c>126)return false; // native ASCII editor only until reviewed UTF-8/cell/RTL infrastructure
    return s.empty() || (s.front()!=' ' && s.back()!=' ');
}
inline bool canonicalElectricityRate(const std::string &input, std::string *out) {
    if(!out || input.empty() || input.size()>64)return false;
    const bool negative=input.front()=='-'; const auto text=negative?input.substr(1):input;
    if(text.empty())return false;
    const auto dot=text.find('.');
    if(dot==0 || dot==text.size()-1 || (dot!=std::string::npos && text.find('.',dot+1)!=std::string::npos))return false;
    unsigned digits=0;
    for(char c:text){if(c=='.')continue; if(c<'0'||c>'9')return false; ++digits;}
    if(digits>38 || (dot!=std::string::npos && text.size()-dot-1>18))return false;
    auto integer=text.substr(0,dot), fraction=dot==std::string::npos?"":text.substr(dot+1);
    const auto first=integer.find_first_not_of('0'); integer=first==std::string::npos?"0":integer.substr(first);
    while(!fraction.empty()&&fraction.back()=='0')fraction.pop_back();
    *out=(negative&&(integer!="0"||!fraction.empty())?"-":"")+integer+(fraction.empty()?"":"."+fraction); return true;
}
inline bool electricityPreferencesValid(const ElectricityPreferences &p,std::string *error=nullptr) {
    auto fail=[&](const char *s){if(error)*error=s; return false;};
    if(p.recordIntervalMs<500 || p.recordIntervalMs>60000 || p.maximumGapMs<p.recordIntervalMs || p.maximumGapMs>60000)return fail("Energy selection: 500..60000 ms; maximum gap must be >= selection and <=60000 ms.");
    for(const auto *label:{&p.country,&p.region,&p.supplier,&p.product,&p.version})if(!electricityLabel(*label))return fail("Use <=64 printable ASCII characters with no surrounding space; no account/credential identifiers.");
    if(!p.currency.empty() && (p.currency.size()!=3 || p.currency.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos))return fail("Currency must be an explicit three-uppercase-letter identifier; no symbol, peg or FX inference.");
    if(p.fixedOffsetSeconds < -64800 || p.fixedOffsetSeconds>64800 || p.fixedOffsetSeconds%60 || (!p.fixedStandardTime&&p.fixedOffsetSeconds))return fail("Choose UTC (offset 0) or a fixed STANDARD offset in whole minutes within +/-18 hours; no DST inference.");
    if(p.effectiveFromUtc>253402300799ULL || p.effectiveThroughUtc>253402300799ULL || p.confirmedUtc>253402300799ULL)return fail("Electricity UTC seconds are outside the supported range.");
    std::string normalized;
    if(!p.ratePerKwh.empty() && (!canonicalElectricityRate(p.ratePerKwh,&normalized)||normalized!=p.ratePerKwh))return fail("Rate must be a canonical signed decimal (<=38 digits / <=18 decimals); zero is valid.");
    if(p.tariffEnabled) {
        if(p.country.empty()||p.region.empty()||p.supplier.empty()||p.product.empty()||p.version.empty()||p.currency.empty()||p.ratePerKwh.empty())return fail("Enabled tariff needs country, region, supplier, product, version, currency and an explicit rate (zero allowed).");
        if(!p.exclusionsAcknowledged)return fail("Explicitly acknowledge ENERGY-ONLY exclusions: no tier/TOU/dynamic/fees/tax/shared bill; not a utility bill.");
        if(!p.effectiveFromUtc || p.effectiveThroughUtc<=p.effectiveFromUtc)return fail("Enabled tariff needs explicit increasing effective UTC seconds; no unlimited or inferred revision.");
        if(!p.confirmedUtc)return fail("Enabled tariff requires an explicit settings confirmation.");
    }
    return true;
}
inline bool confirmElectricityPreferences(ElectricityPreferences *p,uint64_t nowUtc,std::string *error) {
    if(!p||!nowUtc||nowUtc>253402300799ULL){if(error)*error="UTC clock unavailable; electricity settings unchanged."; return false;}
    auto value=*p; value.confirmedUtc=nowUtc;
    if(!electricityPreferencesValid(value,error))return false;
    *p=std::move(value); return true;
}
inline std::string electricityPreferencesJson(const ElectricityPreferences &p) {
    return "{\n\"schema_version\":1,\"rule_model\":\"flat_energy_only\",\"measurement_basis\":\"gpu_board_proxy\",\n\"collect_energy\":"+std::string(p.collectEnergy?"true":"false")+
        ",\"show_energy\":"+(p.showEnergy?"true":"false")+",\"tariff_enabled\":"+(p.tariffEnabled?"true":"false")+",\"exclusions_acknowledged\":"+(p.exclusionsAcknowledged?"true":"false")+
        ",\n\"record_interval_ms\":"+std::to_string(p.recordIntervalMs)+",\"maximum_gap_ms\":"+std::to_string(p.maximumGapMs)+",\n\"country\":"+jsonQuote(p.country)+",\"region\":"+jsonQuote(p.region)+
        ",\"supplier\":"+jsonQuote(p.supplier)+",\"product\":"+jsonQuote(p.product)+",\"version\":"+jsonQuote(p.version)+",\"currency\":"+jsonQuote(p.currency)+",\"rate_per_kwh\":"+jsonQuote(p.ratePerKwh)+
        ",\n\"clock\":"+jsonQuote(p.fixedStandardTime?"fixed_standard":"utc")+",\"fixed_offset_seconds\":"+std::to_string(p.fixedOffsetSeconds)+",\"effective_from_utc\":"+std::to_string(p.effectiveFromUtc)+
        ",\"effective_through_utc\":"+std::to_string(p.effectiveThroughUtc)+",\"confirmed_utc\":"+std::to_string(p.confirmedUtc)+"\n}\n";
}
inline bool parseElectricityPreferences(const std::string &text,ElectricityPreferences *out,std::string *error) {
    auto fail=[&](const char *s){if(error)*error=s; return false;};
    JsonValue root;
    if(!out||text.size()>8192||!parseJsonStrict(text,&root,nullptr)||!root.isObject())return fail("Invalid electricity preferences; file preserved.");
    const char *keys[]={"schema_version","rule_model","measurement_basis","collect_energy","show_energy","tariff_enabled","exclusions_acknowledged","record_interval_ms","maximum_gap_ms","country","region","supplier","product","version","currency","rate_per_kwh","clock","fixed_offset_seconds","effective_from_utc","effective_through_utc","confirmed_utc"};
    if(root.members.size()!=21)return fail("Missing/unknown electricity preference; no rules are silently flattened.");
    for(const auto *key:keys)if(!root.find(key))return fail("Missing/unknown electricity preference; file preserved.");
    uint64_t schema=0,selection=0,gap=0; ElectricityPreferences p;
    auto is=[&](const char *key,const char *value){const auto *v=root.find(key);return v->isString()&&v->text==value;};
    if(!root.find("schema_version")->asUint64(&schema)||schema!=1||!is("rule_model","flat_energy_only")||!is("measurement_basis","gpu_board_proxy"))return fail("Unsupported electricity schema/model/basis; file preserved.");
    for(const auto &item:std::vector<std::pair<const char *,bool *>>{{"collect_energy",&p.collectEnergy},{"show_energy",&p.showEnergy},{"tariff_enabled",&p.tariffEnabled},{"exclusions_acknowledged",&p.exclusionsAcknowledged}}) {
        const auto *v=root.find(item.first); if(!v->isBool())return fail("Invalid electricity toggle type."); *item.second=v->boolean;
    }
    for(const auto &item:std::vector<std::pair<const char *,std::string *>>{{"country",&p.country},{"region",&p.region},{"supplier",&p.supplier},{"product",&p.product},{"version",&p.version},{"currency",&p.currency},{"rate_per_kwh",&p.ratePerKwh}}) {
        const auto *v=root.find(item.first); if(!v->isString())return fail("Electricity identities/rate must be strings."); *item.second=v->text;
    }
    const auto *offset=root.find("fixed_offset_seconds");
    if(!offset->isNumber()||offset->text.size()>6)return fail("Invalid fixed standard offset.");
    const bool negative=!offset->text.empty()&&offset->text[0]=='-';
    uint64_t magnitude=0; JsonValue unsignedOffset=*offset; if(negative)unsignedOffset.text.erase(0,1);
    if(!unsignedOffset.asUint64(&magnitude)||magnitude>64800)return fail("Invalid fixed standard offset.");
    p.fixedOffsetSeconds=static_cast<int>(magnitude)*(negative?-1:1);
    if(is("clock","fixed_standard"))p.fixedStandardTime=true; else if(!is("clock","utc"))return fail("Named/DST clock settings are not supported by this native flat page; no OS fallback.");
    if(!root.find("record_interval_ms")->asUint64(&selection)||selection>60000||!root.find("maximum_gap_ms")->asUint64(&gap)||gap>60000||
        !root.find("effective_from_utc")->asUint64(&p.effectiveFromUtc)||!root.find("effective_through_utc")->asUint64(&p.effectiveThroughUtc)||!root.find("confirmed_utc")->asUint64(&p.confirmedUtc))return fail("Invalid electricity timing type/range.");
    p.recordIntervalMs=static_cast<unsigned>(selection); p.maximumGapMs=static_cast<unsigned>(gap);
    if(!p.ratePerKwh.empty()&&!canonicalElectricityRate(p.ratePerKwh,&p.ratePerKwh))return fail("Invalid signed electricity decimal.");
    if(!electricityPreferencesValid(p,error))return false;
    *out=std::move(p); return true;
}
class ElectricityPreferencesStore {
   public:
    explicit ElectricityPreferencesStore(std::filesystem::path path):path_(std::move(path)){}
    bool load(ElectricityPreferences *out,std::string *error) {
        if(!out)return fail(error,"Electricity output unavailable.");
        std::string text; bool exists=false; if(!read(&text,&exists,error))return false;
        ElectricityPreferences p; if(exists&&!parseElectricityPreferences(text,&p,error))return false;
        original_=std::move(text); existed_=exists; loaded_=true; baseline_=p; *out=std::move(p); return true;
    }
    bool save(const ElectricityPreferences &p,std::string *error) {
        if(!loaded_)return fail(error,"Electricity target was not loaded.");
        if(!electricityPreferencesValid(p,error))return false;
        if(!baseline_.version.empty() && !baseline_.ratePerKwh.empty() && baseline_.version==p.version && !sameElectricityDeclaration(baseline_,p))
            return fail(error,"Declaration changed: choose a NEW effective version key for identity/rate/clock/effective-range changes. Current session unchanged.");
        std::error_code ec; if(path_.has_parent_path())std::filesystem::create_directories(path_.parent_path(),ec);
        if(ec)return fail(error,"Cannot create electricity settings directory.");
        auto lock=path_; lock+=".lock"; RuntimePreferencesLease lease;
        if(!lease.acquire(lock))return fail(error,"Electricity settings locked or invalid lock file.");
        std::string text; bool exists=false; if(!read(&text,&exists,error))return false;
        if(exists!=existed_||text!=original_)return fail(error,"Electricity settings changed externally; reopen before saving.");
        static std::atomic<unsigned> serial{0}; auto temporary=path_;
#if defined(_WIN32)
        temporary+=".tmp-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
#else
        temporary+=".tmp-"+std::to_string(getpid())+"-"+std::to_string(++serial);
#endif
        int fd=-1;
#if defined(_WIN32)
        if(_wsopen_s(&fd,temporary.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0)return fail(error,"Cannot create temporary electricity settings.");
        FILE *f=_fdopen(fd,"wb");
#else
        fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd<0)return fail(error,"Cannot create temporary electricity settings.");
        FILE *f=fdopen(fd,"wb");
#endif
        if(!f){
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::filesystem::remove(temporary,ec); return fail(error,"Cannot open temporary electricity settings.");
        }
        const auto encoded=electricityPreferencesJson(p); bool ok=fwrite(encoded.data(),1,encoded.size(),f)==encoded.size()&&fflush(f)==0;
#if defined(_WIN32)
        ok=(_commit(fd)==0)&&ok;
#else
        ok=(fsync(fd)==0)&&ok;
#endif
        ok=(fclose(f)==0)&&ok;
        if(ok){
#if defined(_WIN32)
            ok=MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
            std::filesystem::rename(temporary,path_,ec); ok=!ec;
#endif
        }
        if(!ok){std::filesystem::remove(temporary,ec); return fail(error,"Cannot replace electricity settings; current session unchanged.");}
        original_=encoded; existed_=true; baseline_=p; return true;
    }
   private:
    std::filesystem::path path_; std::string original_; bool loaded_=false,existed_=false;
    ElectricityPreferences baseline_;
    static bool fail(std::string *error,const char *s){if(error)*error=s; return false;}
    bool read(std::string *text,bool *exists,std::string *error)const {
        std::error_code ec; const auto status=std::filesystem::symlink_status(path_,ec);
        if(ec==std::errc::no_such_file_or_directory||(!ec&&status.type()==std::filesystem::file_type::not_found)){text->clear();*exists=false;return true;}
        if(ec||!std::filesystem::is_regular_file(status))return fail(error,"Electricity settings must be a regular file; preserved.");
        std::ifstream f(path_,std::ios::binary); if(!f)return fail(error,"Cannot read electricity settings; preserved.");
        char data[8193];f.read(data,sizeof(data));
        if(f.bad()||f.gcount()>8192)return fail(error,"Electricity settings oversized/unreadable; preserved.");
        text->assign(data,static_cast<size_t>(f.gcount()));*exists=true;return true;
    }
};
} // namespace om
