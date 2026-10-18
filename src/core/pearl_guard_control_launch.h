// Explicit Linux private-supervisor launch seam, never selected by defaults.
#pragma once
#include "pearl_guard_retirement_v2.h"
#include "pearl_production_evidence.h"
#include <memory>
#include <string>

namespace om {
class PearlGuardControlLaunch final {
public:
    // run:session:cap:absolute_deadline_ms:read_fd:dev:ino:write_fd:dev:ino
    bool admit(const std::string &text,const char *runId,const std::string &transcript,
               uint64_t nowMs,guard_retirement_v2::Profile profile=guard_retirement_v2::Profile::Legacy) {
        if (attempted_) return false;
        attempted_=true;
        std::array<std::string,10> fields;
        size_t begin=0;
        for(size_t i=0;i<fields.size();++i) {
            const size_t end=text.find(':',begin);
            if ((i+1==fields.size()) != (end==std::string::npos)) return false;
            fields[i]=text.substr(begin,end==std::string::npos ? end : end-begin);
            begin=end==std::string::npos ? text.size() : end+1;
        }
        guard_retirement_v2::Id run{},session{};
        if(!identity(fields[0],run) || !identity(fields[1],session) || !runId ||
            fields[0]!=runId || transcript.empty()) return false;
        std::array<uint64_t,8> numbers{};
        for(size_t i=0;i<numbers.size();++i)
            if(!number(fields[i+2],numbers[i])) return false;
        const uint64_t cap=numbers[0],deadline=numbers[1];
        const bool productionProfile=profile==guard_retirement_v2::Profile::Production ||
            profile==guard_retirement_v2::Profile::PersonalProduction;
        if(profile!=guard_retirement_v2::Profile::Legacy &&
           profile!=guard_retirement_v2::Profile::Public24h && !productionProfile) return false;
        const uint64_t horizon=profile==guard_retirement_v2::Profile::Public24h ? 90000000ULL : 10800000ULL;
        if(!cap || cap>guard_retirement_v2::profileCap(profile) ||
           (productionProfile ? deadline!=0 :
               (deadline<=nowMs || deadline-nowMs>horizon)) ||
           numbers[2]>INT32_MAX || numbers[5]>INT32_MAX ||
           static_cast<uint64_t>(static_cast<dev_t>(numbers[3]))!=numbers[3] ||
           static_cast<uint64_t>(static_cast<ino_t>(numbers[4]))!=numbers[4] ||
           static_cast<uint64_t>(static_cast<dev_t>(numbers[6]))!=numbers[6] ||
           static_cast<uint64_t>(static_cast<ino_t>(numbers[7]))!=numbers[7]) return false;
        guard_retirement_v2::PipeBinding input{static_cast<int>(numbers[2]),
            static_cast<dev_t>(numbers[3]),static_cast<ino_t>(numbers[4]),O_RDONLY};
        guard_retirement_v2::PipeBinding output{static_cast<int>(numbers[5]),
            static_cast<dev_t>(numbers[6]),static_cast<ino_t>(numbers[7]),O_WRONLY};
        // Exclusive creation preserves all prior control evidence.
        production_=productionProfile;
        if (production_) {
            if (!productionFile_.open(transcript+".guard-control.jsonl")) return false;
        } else {
            file_=std::fopen((transcript+".guard-control.jsonl").c_str(),"wx");
            if(!file_) return false;
        }
        session_.reset(new guard_retirement_v2::Session(run,session,static_cast<uint32_t>(cap),deadline,profile));
        if(!session_->admit(input,output,1,1,3,retain,this)) { session_.reset(); return false; }
        return true;
    }
    guard_retirement_v2::Session *session() const { return session_.get(); }
    bool close() {
        if (production_) return productionFile_.close();
        if(!file_) return !attempted_;
        const bool flushed=std::fflush(file_)==0 && !std::ferror(file_);
        const bool closed=std::fclose(file_)==0; file_=nullptr;
        return flushed && closed;
    }
    ~PearlGuardControlLaunch() { if(file_) std::fclose(file_); }
private:
    static bool number(const std::string &text,uint64_t &out) {
        if(text.empty() || (text.size()>1 && text[0]=='0')) return false;
        out=0;
        for(char c:text) {
            if(c<'0' || c>'9' || out>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10) return false;
            out=out*10+static_cast<unsigned>(c-'0');
        }
        return true;
    }
    static bool identity(const std::string &text,guard_retirement_v2::Id &out) {
        if(text.size()!=32) return false;
        for(size_t i=0;i<32;++i) {
            char c=text[i];
            if(!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
            const auto value=static_cast<uint8_t>(c<='9' ? c-'0' : c-'a'+10);
            out[i/2]=static_cast<uint8_t>((out[i/2]<<4)|value);
        }
        return true;
    }
    static bool retain(void *context,const char *event,const guard_retirement_v2::Bytes &raw) {
        auto *self=static_cast<PearlGuardControlLaunch *>(context);
        char hex[409]; const char digits[]="0123456789abcdef";
        for(size_t i=0;i<raw.size();++i) { hex[2*i]=digits[raw[i]>>4]; hex[2*i+1]=digits[raw[i]&15]; }
        hex[408]='\0';
        if (self->production_) {
            char line[640];
            const int n=std::snprintf(line,sizeof(line),"{\"event\":\"%s\",\"control_v3_hex\":\"%s\"}\n",event,hex);
            return n>0 && size_t(n)<sizeof(line) && self->productionFile_.write(line,size_t(n))==size_t(n) && self->productionFile_.flush();
        }
        return self->file_ && std::fprintf(self->file_,
            "{\"event\":\"%s\",\"control_v2_hex\":\"%s\"}\n",event,hex)>0 &&
            std::fflush(self->file_)==0 && !std::ferror(self->file_);
    }
    bool attempted_=false,production_=false;
    PearlProductionEvidenceFile productionFile_;
    FILE *file_=nullptr;
    std::unique_ptr<guard_retirement_v2::Session> session_;
};
} // namespace om
