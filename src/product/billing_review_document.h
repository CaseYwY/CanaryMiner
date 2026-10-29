// Pure bounded private document, built only on snapshot/reveal changes. No storage, helper or source calls.
#pragma once
#include "billing_owner.h"
#include <type_traits>
namespace om { namespace billing_review_detail {
inline std::string text(const std::string &s){
    static constexpr char hex[]="0123456789abcdef";std::string out="\"";
    for(unsigned char c:s){if(c>=32&&c<=126&&c!='\\'&&c!='\"')out+=static_cast<char>(c);else{out+="\\x";out+=hex[c>>4];out+=hex[c&15];}}
    return out+'\"'; // byte-preserving ASCII; no terminal control or Unicode truncation
}
template<class T>inline std::string number(T n){
    if constexpr(std::is_enum<T>::value)return std::to_string(static_cast<typename std::underlying_type<T>::type>(n));
    else return std::to_string(n);
}
struct Document {
    std::vector<std::string> lines;
    size_t bytes=0;bool complete=true;
    void add(const std::string &s){if(!complete)return;if(lines.size()>=131072||s.size()>16*1024*1024-bytes){complete=false;return;}bytes+=s.size();lines.push_back(s);}
    void field(const std::string &p,const std::string &s){add(p+" = "+s);}
    void string(const std::string &p,const std::string &s){if(s.size()>128*1024){complete=false;return;}field(p,text(s));}
    template<class T>void n(const std::string &p,T v){field(p,number(v));}
    void decimal(const std::string &p,const ExactDecimal &v){field(p,v.text());}
    void optional(const std::string &p,const std::optional<ExactDecimal> &v){field(p,v?v->text():"UNAVAILABLE / not declared");}
    void amount(const std::string &p,const std::optional<ElectricityAmount> &v){field(p,v?v->numerator.text()+" / 3600000000":"UNAVAILABLE");}
    void evidence(const std::string &p,const PowerEvidence &e){n(p+".basis (0 unknown,1 GPU,2 wall,3 incremental,4 meter)",e.basis);n(p+".origin (1 fixture,2 observed,3 forecast)",e.origin);string(p+".scope",e.scope);string(p+".adapter",e.adapter);}
    void energyBinding(const std::string &p,const EnergyHistoryBinding &b){evidence(p+".evidence",b.evidence);n(p+".historyBegin UTC ms",b.historyBeginUtcMs);
        if(b.channels.size()>64){complete=false;return;}for(size_t i=0;i<b.channels.size();++i)string(p+".channel["+number(i)+"]",b.channels[i]);}
    void energyLimits(const std::string &p,const EnergyHistoryLimits &l){n(p+".maxBatches",l.maxBatches);n(p+".maxReadings",l.maxReadings);n(p+".maxSessions",l.maxSessions);n(p+".maxWindows",l.maxWindows);n(p+".maxDatabaseBytes",l.maxDatabaseBytes);
        if(l.compaction){n(p+".compaction.afterBatches",l.compaction->afterBatches);n(p+".compaction.maxSegments",l.compaction->maxSegments);}else field(p+".compaction","OFF");}
    void tariff(const TariffPlan &t){const std::string p="tariff";n(p+".declared",t.declared);n(p+".origin (1 fixture,2 declared offline)",t.origin);
        string(p+".key",t.key);string(p+".territory",t.territory);string(p+".supplier",t.supplier);string(p+".product",t.product);string(p+".currency",t.currency);
        n(p+".clock.kind (0 UTC,1 fixed,2 IANA)",t.clock.kind);string(p+".clock.zone",t.clock.zone);n(p+".clock.fixedOffsetSeconds",t.clock.fixedOffsetSeconds);
        n(p+".tiers (0 none,1 progressive,2 all-units)",t.tiers);n(p+".rounding (0 unsupported,1 display only)",t.rounding);decimal(p+".cycleCharge",t.cycleCharge);
        optional(p+".minimumCycleSubtotal",t.minimumCycleSubtotal);optional(p+".subtotalMultiplier",t.subtotalMultiplier);n(p+".unsupportedRules",t.unsupportedRules);
        if(t.revisions.size()>128){complete=false;return;}size_t rows=0;
        for(size_t i=0;i<t.revisions.size();++i){const auto &r=t.revisions[i];const auto q=p+".revision["+number(i)+"]";
            if(r.calendar.size()>64||r.dynamic.size()>4096||r.bands.size()>64||(rows+=r.calendar.size()+r.dynamic.size()+r.bands.size())>4096){complete=false;return;}
            string(q+".key",r.key);n(q+".begin UTC ms",r.beginUtcMs);n(q+".end UTC ms (exclusive)",r.endUtcMs);n(q+".published UTC ms",r.publishedUtcMs);
            n(q+".timeMode (0 constant,1 calendar,2 dynamic)",r.timeMode);decimal(q+".baseRate currency/kWh",r.baseRatePerKwh);decimal(q+".chargePerCivilDay",r.chargePerCivilDay);
            for(size_t j=0;j<r.calendar.size();++j){const auto &c=r.calendar[j];const auto k=q+".calendar["+number(j)+"]";string(k+".key",c.key);n(k+".beginMinute",c.beginMinute);n(k+".endMinute",c.endMinute);
                n(k+".weekdayMask",c.weekdayMask);n(k+".monthMask",c.monthMask);n(k+".priority",c.priority);field(k+".date",c.date?number(c.date->year)+"-"+number(c.date->month)+"-"+number(c.date->day):"ANY");n(k+".published UTC ms",c.publishedUtcMs);decimal(k+".rate currency/kWh",c.ratePerKwh);}
            for(size_t j=0;j<r.dynamic.size();++j){const auto &d=r.dynamic[j];const auto k=q+".dynamic["+number(j)+"]";n(k+".begin UTC ms",d.beginUtcMs);n(k+".end UTC ms (exclusive)",d.endUtcMs);n(k+".published UTC ms",d.publishedUtcMs);decimal(k+".rate currency/kWh",d.ratePerKwh);}
            for(size_t j=0;j<r.bands.size();++j){const auto &b=r.bands[j];const auto k=q+".band["+number(j)+"]";field(k+".through kWh",b.throughKwh?b.throughKwh->text():"UNBOUNDED");decimal(k+".rate currency/kWh",b.ratePerKwh);}
        }
    }
    void binding(const BillingOwnerBinding &b){add("COMPLETE APPROVED BINDING (private)");string("monetary.file",b.file.u8string());string("cycle.key",b.binding.cycleKey);
        n("cycle.begin UTC ms",b.binding.cycleBeginUtcMs);n("cycle.end UTC ms (exclusive)",b.binding.cycleEndUtcMs);evidence("power",b.binding.evidence);tariff(b.binding.plan);
        n("limits.maxBatches",b.limits.maxBatches);n("limits.maxIntervals",b.limits.maxIntervals);n("limits.maxDatabaseBytes",b.limits.maxDatabaseBytes);
        if(b.binding.source){const auto &s=*b.binding.source;string("source.file",s.file.u8string());energyBinding("source.binding",s.binding);energyLimits("source.limits",s.limits);}else field("source","NO bound energy source");}
    void trace(const std::string &p,const PowerTrace &t){evidence(p+".evidence",t.evidence);if(t.intervals.size()>512){complete=false;return;}n(p+".intervalCount",t.intervals.size());
        for(size_t i=0;i<t.intervals.size();++i){const auto &v=t.intervals[i];add(p+"["+number(i)+"] ["+number(v.beginUtcMs)+","+number(v.endUtcMs)+") UTC ms = "+v.watts.text()+" W");}}
    void batch(const std::string &p,const EnergyHistoryBatch &b){n(p+".sequence",b.sequence);string(p+".id",b.id);n(p+".observed UTC ms",b.observedAtMs);string(p+".sessionId",b.sessionId);
        if(b.start){const auto &s=*b.start;string(p+".start.id",s.id);n(p+".start.anchor UTC ms",s.anchorUtcMs);n(p+".start.anchor monotonic ms",s.anchorMonoMs);n(p+".start.maximumGap ms",s.maximumGapMs);}else field(p+".start","NONE");
        if(b.readings.size()>128||b.windows.size()>32){complete=false;return;}
        for(size_t i=0;i<b.readings.size();++i){const auto &r=b.readings[i];add(p+".reading["+number(i)+"] channel "+number(r.channel)+" UTC "+number(r.utcMs)+" mono "+number(r.monotonicMs)+" continuity "+number(r.continuity)+" mW "+(r.milliwatts?number(*r.milliwatts):"MISSING"));}
        for(size_t i=0;i<b.windows.size();++i){const auto &w=b.windows[i];const auto q=p+".window["+number(i)+"]";string(q+".register",w.registerId);string(q+".period",w.periodId);string(q+".ruleVersion",w.ruleVersion);n(q+".begin UTC ms",w.beginUtcMs);n(q+".end UTC ms",w.endUtcMs);n(q+".fixedStandardTime",w.fixedStandardTime);n(q+".offsetSeconds",w.offsetSeconds);}
    }
    void receipt(const std::string &p,const BillingChunk::Receipt &c){const auto &q=c.request;const auto &r=q.source;
        n(p+".request.generation",r.generation);n(p+".request.ticket",r.ticket);string(p+".request.file",r.file.u8string());energyBinding(p+".request.binding",r.binding);energyLimits(p+".request.limits",r.limits);
        n(p+".request.mode",r.mode);n(p+".request.recovery",r.recovery);n(p+".request.originalQuery UTC ms",r.nowUtcMs);n(p+".request.maxIntervals",q.maxIntervals);
        if(q.anchor){n(p+".request.anchor.sequence",q.anchor->sequence);string(p+".request.anchor.digest",q.anchor->digest);}else field(p+".request.anchor","NONE");
        if(r.batch)batch(p+".request.batch",*r.batch);else field(p+".request.batch","NONE");
        if(r.queries.size()>4){complete=false;return;}for(size_t i=0;i<r.queries.size();++i){const auto &a=r.queries[i];const auto k=p+".request.query["+number(i)+"]";n(k+".begin UTC ms",a.beginUtcMs);n(k+".end UTC ms",a.endUtcMs);string(k+".register",a.registerId);string(k+".period",a.periodId);}
        const auto &s=c.reply.source;const auto &h=s.checkpoint;n(p+".reply.status",c.reply.status);field(p+".reply.anchorStatus",c.reply.anchorStatus?number(*c.reply.anchorStatus):"NONE");
        n(p+".reply.source.generation",s.generation);n(p+".reply.source.ticket",s.ticket);n(p+".reply.source.opened",s.opened);field(p+".reply.source.applied",s.applied?number(*s.applied):"NOT ATTEMPTED");
        const auto k=p+".reply.checkpoint";n(k+".status",h.status);n(k+".sequence",h.sequence);n(k+".readingCount",h.readingCount);n(k+".sessionCount",h.sessionCount);n(k+".windowCount",h.windowCount);n(k+".compactedSequence",h.compactedSequence);
        string(k+".batchId",h.batchId);string(k+".sessionId",h.sessionId);string(k+".digest",h.digest);n(k+".observed UTC ms",h.observedAtMs);n(k+".through UTC ms",h.throughUtcMs);
        if(s.totals.size()>4){complete=false;return;}for(size_t i=0;i<s.totals.size();++i){const auto &v=s.totals[i];const auto t=p+".reply.totals["+number(i)+"]";n(t+".status",v.status);n(t+".coverage (0 unavailable,1 partial,2 complete)",v.coverage);n(t+".begin UTC ms",v.beginUtcMs);n(t+".end UTC ms",v.endUtcMs);n(t+".observed UTC ms",v.observedAtMs);n(t+".coveredDeviceMs",v.coveredDeviceMs);n(t+".requestedDeviceMs",v.requestedDeviceMs);amount(t+".known kWh",v.knownKwh);amount(t+".complete kWh",v.completeKwh);}
        if(s.recovery){const auto &v=*s.recovery;const auto t=p+".reply.recovery";n(t+".status",v.status);n(t+".state",v.state);n(t+".priorSequence",v.priorSequence);string(t+".priorDigest",v.priorDigest);field(t+".applied",v.applied?number(*v.applied):"NOT ATTEMPTED");if(v.batch)batch(t+".original",*v.batch);else field(t+".original","NONE");}else field(p+".reply.recovery","NONE");
        if(c.reply.completeTrace)trace(p+".reply.completeTrace",*c.reply.completeTrace);else field(p+".reply.completeTrace","UNAVAILABLE");
    }
    void chunk(const std::string &p,const BillingChunk &c){add(p+" / EXACT ORIGINAL (observation/query times are not renewed)");n(p+".sequence",c.sequence);string(p+".id",c.id);n(p+".observed UTC ms",c.observedUtcMs);trace(p+".trace",c.trace);if(c.receipt)receipt(p+".receipt",*c.receipt);else field(p+".receipt","NONE");}
    void manifest(const std::string &p,const BillingInputManifest &m){n(p+".mode (0 create,1 submit)",m.mode);string(p+".file",m.file.u8string());n(p+".bindingBytes",m.bindingBytes);n(p+".chunkBytes",m.chunkBytes);string(p+".bindingDigest",m.bindingDigest);string(p+".chunkDigest",m.chunkDigest);n(p+".prepared UTC ms",m.preparedUtcMs);string(p+".priorRevision",m.priorRevision);n(p+".priorSequence",m.priorSequence);string(p+".priorDigest",m.priorDigest);}
    void monetary(const std::string &p,const BillingRecoverySnapshot &s){n(p+".status",s.status);n(p+".state (0 unavailable,1 empty,2 prepared,3 confirmed,4 rejected,5 diverged)",s.state);
        if(s.status!=BillingStatus::Ok){add(p+": monetary facts UNAVAILABLE; no zero inferred.");return;}
        field(p+".applied",s.applied?number(*s.applied):"NOT ATTEMPTED");n(p+".priorSequence",s.priorSequence);string(p+".priorDigest",s.priorDigest);string(p+".journalRevision",s.revision);n(p+".recorded UTC ms",s.recordedUtcMs);
        const auto &c=s.committed;const auto k=p+".committed";n(k+".status",c.status);n(k+".sequence",c.sequence);n(k+".intervals",c.intervals);string(k+".batchId",c.batchId);string(k+".digest",c.digest);n(k+".observed UTC ms",c.observedUtcMs);
        if(!c.sequence)add(k+": EMPTY initialization; opening zeros are not observed energy or money.");
        const auto &o=c.opening;n(k+".opening.declared",o.declared);string(k+".opening.planKey",o.planKey);string(k+".opening.timeDatabaseVersion",o.timeDatabaseVersion);evidence(k+".opening.evidence",o.evidence);
        n(k+".opening.cycleBegin UTC ms",o.cycleBeginUtcMs);n(k+".opening.cycleEnd UTC ms",o.cycleEndUtcMs);n(k+".opening.through UTC ms",o.throughUtcMs);amount(k+".opening.imported kWh",o.importedKwh);amount(k+".opening.accrued subtotal",o.accruedSubtotal);
        amount(k+".provisional total (not final payable bill)",c.provisionalTotal);amount(k+".settled total",c.settledTotal);n(k+".settlementDependent",c.settlementDependent);
        if(c.source){n(k+".source.sequence",c.source->checkpoint.sequence);string(k+".source.digest",c.source->checkpoint.digest);n(k+".source.queried UTC ms",c.source->queriedUtcMs);}else field(k+".source","NONE");
        if(s.original)chunk(p+".original",*s.original);else field(p+".original","NONE");
    }
};
inline Document document(const BillingOwnerSnapshot &s){Document d;
    d.add("PRIVATE LOCAL VALUES - exclude terminal captures from sharing.");d.add("Strings are quoted ASCII bytes; \\xHH preserves every control/non-ASCII byte. No truncation.");
    d.add("Energy/charge amounts use exact numerator / 3600000000; rates and watts are decimal. Times are UTC ms.");
    d.add("Billing status: 0 Ok,1 Replay,2 Invalid,3 Unsupported,4 Attribution,5 Conflict,6 SequenceGap,7 Clock,8 Capacity,9 Calculation,10 Storage.");
    d.add("Energy status: 0 Ok,1 Replay,2 Invalid,3 Attribution,4 Conflict,5 SequenceGap,6 Clock,7 Capacity,8 Storage,9 DetailExpired.");
    d.add("Energy recovery: 0 Disabled,1 Empty,2 Prepared,3 Confirmed,4 Rejected,5 Diverged,6 Unavailable. Query recovery: 0 volatile,1 inspect,2 submit,3 retry.");
    d.add("Energy open mode: 0 existing,1 create,2 readonly existing. Power/tariff origin 0 means unknown.");
    if(s.context)d.binding(*s.context);
    if(s.view&&s.view->stage){const auto &v=*s.view->stage;d.add("STAGED ORIGINAL / saved outcome is HISTORICAL, not a current ledger query");d.n("stage.present",v.present);d.n("stage.replacementPending",v.replacementPending);d.n("stage.status",v.status);
        if(v.present){d.n("stage.phase (1 receiving,2 issued,3 complete)",v.phase);d.string("stage.revision",v.revision);d.n("stage.receivedBytes",v.received);d.manifest("stage.manifest",v.manifest);
            if(v.original)d.chunk("stage.original",*v.original);else{d.add("stage prefix is raw private bytes, NOT a typed monetary input");d.string("stage.originalPrefix",v.originalPrefix);}
            if(v.certificate){const auto &c=*v.certificate;d.n("stage.certificate.state",c.state);d.n("stage.certificate.result",c.result);d.string("stage.certificate.revision",c.revision);d.string("stage.certificate.digest",c.digest);d.field("stage.certificate.applied",c.applied?number(*c.applied):"NOT ATTEMPTED");d.string("stage.certificate.canonicalState",c.committed);}
            if(v.outcome)d.monetary("HISTORICAL stage outcome",*v.outcome);}}
    if(s.view&&s.view->monetary){d.add(s.reviewable?"CURRENT READ / audited middle observation":"RETAINED EARLIER READ / refresh required before another action");d.n("view.observed UTC ms",s.view->observedUtcMs);d.monetary("monetary",*s.view->monetary);}
    if(s.uncertain){const auto &u=*s.uncertain;d.add("UNKNOWN HANDOFF OUTCOME / preserve exact input");d.n("unknown.action (0 inspect,1 create,2 submit,3 commit,4 retry,5 reconcile)",u.action);if(u.manifest)d.manifest("unknown.manifest",*u.manifest);if(u.original)d.chunk("unknown.original",*u.original);if(u.monetary)d.monetary("unknown.reviewedPrior",*u.monetary);}
    if(s.actionProof){d.add("HISTORICAL ACTION PROOF / not a fresh monetary read");d.monetary("actionProof",*s.actionProof);}
    if(!s.context||!s.view||!s.view->stable||!s.view->stage||!s.view->monetary){d.add("No validated complete review context in this snapshot.");d.complete=false;}
    if(d.complete)d.add("END OF COMPLETE PRIVATE REVIEW DOCUMENT");
    return d;
}
} } // namespace om::billing_review_detail
