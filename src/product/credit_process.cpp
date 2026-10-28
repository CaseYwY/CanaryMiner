#include "credit_process.h"
#include "json_dom.h"
#include <algorithm>

namespace om {
namespace {
constexpr uint64_t lastMs = 253402300799999ULL;
bool array(const JsonValue &v, size_t n) { return v.isArray() && v.items.size() == n; }
bool u(const JsonValue &v, uint64_t &n, uint64_t max = lastMs) { return v.asUint64(&n) && n <= max; }
bool si(const JsonValue &v, int64_t &n) { uint64_t value = 0; if (!u(v,value)) return false; n = static_cast<int64_t>(value); return true; }
bool str(const JsonValue &v, std::string &s) { if (!v.isString() || v.text.size() > 4096 || v.text.find('\0') != std::string::npos) return false; s = v.text; return true; }
bool dec(const JsonValue &v, ExactDecimal &d) { return v.isString() && v.text.size() <= 150 && ExactDecimal::parse(v.text,&d) && d.text() == v.text; }
bool null(const JsonValue &v) { return v.type == JsonValue::Type::Null; }
std::string contract(const CreditSourceContract &c) {
    return "["+std::to_string(static_cast<unsigned>(c.qualification))+","+std::to_string(static_cast<unsigned>(c.asset))+","+std::to_string(static_cast<unsigned>(c.scope))+","+
        jsonQuote(c.provider)+","+jsonQuote(c.contractVersion)+","+jsonQuote(c.scopeId)+","+jsonQuote(c.scopeGeneration)+","+std::to_string(static_cast<unsigned>(c.timeBasis))+","+
        std::to_string(static_cast<unsigned>(c.revisionModel))+","+std::to_string(static_cast<unsigned>(c.amountBasis))+","+(c.scaleDeclared?"true":"false")+","+
        (c.accountScopeAcknowledged?"true":"false")+","+std::to_string(c.amountScale)+","+std::to_string(c.historyStartMs)+"]";
}
bool contract(const JsonValue &v, CreditSourceContract &c) {
    if (!array(v,14)) return false;
    const auto &a = v.items; uint64_t n[8] = {};
    if (!u(a[0],n[0],1) || !u(a[1],n[1],1) || !u(a[2],n[2],1) || !str(a[3],c.provider) || !str(a[4],c.contractVersion) || !str(a[5],c.scopeId) || !str(a[6],c.scopeGeneration) ||
        !u(a[7],n[3],1) || !u(a[8],n[4],1) || !u(a[9],n[5],1) || !a[10].isBool() || !a[11].isBool() || !u(a[12],n[6],48) || !u(a[13],n[7])) return false;
    c.qualification = static_cast<CreditQualification>(n[0]); c.asset = static_cast<CreditAsset>(n[1]); c.scope = static_cast<CreditScope>(n[2]);
    c.timeBasis = static_cast<CreditTimeBasis>(n[3]); c.revisionModel = static_cast<CreditRevisionModel>(n[4]); c.amountBasis = static_cast<CreditAmountBasis>(n[5]);
    c.scaleDeclared = a[10].boolean; c.accountScopeAcknowledged = a[11].boolean; c.amountScale = static_cast<unsigned>(n[6]); c.historyStartMs = static_cast<int64_t>(n[7]);
    return validCreditSourceContract(c);
}
bool validRequest(const CreditProcessRequest &r) {
    if (!r.generation || !r.ticket || !localTaskPath(r.file) || !validCreditSourceContract(r.binding) || r.nowUtcMs <= 0 || static_cast<uint64_t>(r.nowUtcMs) > lastMs || r.binding.historyStartMs > r.nowUtcMs ||
        r.queries.empty() || r.queries.size() > 4 || !r.limits.maxRecords || r.limits.maxRecords > 100000 || r.limits.maxVersions < r.limits.maxRecords || r.limits.maxVersions > 400000 ||
        !r.limits.maxSnapshots || r.limits.maxSnapshots > 400000 || r.limits.maxDatabaseBytes < 1024*1024 || r.limits.maxDatabaseBytes > 256*1024*1024) return false;
    for (const auto &q : r.queries) {
        const auto from = q.cumulative ? r.binding.historyStartMs-1 : q.fromExclusiveMs;
        if ((q.cumulative && q.fromExclusiveMs != 0) || from < 0 || q.throughInclusiveMs <= from || q.throughInclusiveMs > r.nowUtcMs) return false;
    }
    return !r.batch || (boundedCreditBatchInput(*r.batch,r.nowUtcMs) && sameCreditSourceContract(r.binding,r.batch->binding));
}
bool status(const JsonValue &v, CreditLedgerStatus &s) { uint64_t n = 0; if (!u(v,n,static_cast<unsigned>(CreditLedgerStatus::Storage))) return false; s = static_cast<CreditLedgerStatus>(n); return true; }
std::string totals(const CreditTotals &t) {
    return "["+std::to_string(static_cast<unsigned>(t.status))+","+std::to_string(static_cast<unsigned>(t.completeness))+","+std::to_string(t.fromExclusiveMs)+","+
        std::to_string(t.throughInclusiveMs)+","+std::to_string(t.sourceAsOfMs)+","+std::to_string(t.observedAtMs)+","+std::to_string(t.coveredMs)+","+
        std::to_string(t.requestedMs)+","+std::to_string(t.matchedRecords)+","+jsonQuote(t.knownPostedNet.text())+","+(t.completePostedNet?jsonQuote(t.completePostedNet->text()):"null")+"]";
}
bool totals(const JsonValue &v, const CreditProcessRequest &r, const CreditQuerySpec &q, CreditTotals &t) {
    if (!array(v,11)) return false;
    const auto &a = v.items; uint64_t complete = 0;
    if (!status(a[0],t.status) || !u(a[1],complete,2) || !si(a[2],t.fromExclusiveMs) || !si(a[3],t.throughInclusiveMs) || !si(a[4],t.sourceAsOfMs) || !si(a[5],t.observedAtMs) ||
        !u(a[6],t.coveredMs) || !u(a[7],t.requestedMs) || !u(a[8],t.matchedRecords,r.limits.maxRecords) || !dec(a[9],t.knownPostedNet)) return false;
    t.completeness = static_cast<CreditCompleteness>(complete); t.qualification = r.binding.qualification; t.scope = r.binding.scope;
    const auto from = q.cumulative ? r.binding.historyStartMs-1 : q.fromExclusiveMs;
    if (t.fromExclusiveMs != from || t.throughInclusiveMs != q.throughInclusiveMs || t.sourceAsOfMs > t.observedAtMs || t.observedAtMs > r.nowUtcMs) return false;
    if (!null(a[10])) { ExactDecimal d; if (!dec(a[10],d)) return false; t.completePostedNet = d; }
    if (t.status != CreditLedgerStatus::Ok) return t.completeness == CreditCompleteness::Unavailable && !t.completePostedNet && !t.sourceAsOfMs && !t.observedAtMs && !t.coveredMs && !t.requestedMs && !t.matchedRecords && t.knownPostedNet.zero();
    if (t.requestedMs != static_cast<uint64_t>(t.throughInclusiveMs-t.fromExclusiveMs) || t.coveredMs > t.requestedMs ||
        (t.sourceAsOfMs == 0) != (t.observedAtMs == 0) || ((t.coveredMs || t.matchedRecords) && !t.sourceAsOfMs) ||
        (!t.matchedRecords && !t.knownPostedNet.zero()) || t.knownPostedNet.scale() > r.binding.amountScale) return false;
    const auto coveredBegin = (std::max)(t.fromExclusiveMs,r.binding.historyStartMs-1), coveredEnd = (std::min)(t.throughInclusiveMs,t.sourceAsOfMs);
    if (t.coveredMs > static_cast<uint64_t>((std::max)(int64_t{0},coveredEnd-coveredBegin))) return false;
    const auto expected = t.coveredMs == t.requestedMs ? CreditCompleteness::Complete : (t.coveredMs || t.matchedRecords ? CreditCompleteness::Partial : CreditCompleteness::Unavailable);
    return t.completeness == expected && (t.completePostedNet.has_value() == (expected == CreditCompleteness::Complete)) &&
        (!t.completePostedNet || t.completePostedNet->compare(t.knownPostedNet) == 0) && (!q.cumulative || !t.knownPostedNet.negative());
}
}  // namespace
bool encodeCreditRequest(const CreditProcessRequest &r, std::string *wire) {
    if (!wire || !validRequest(r)) return false;
    try {
        std::string text = "[\"CMCR1\","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(r.file.u8string())+","+contract(r.binding)+",["+
            std::to_string(r.limits.maxRecords)+","+std::to_string(r.limits.maxVersions)+","+std::to_string(r.limits.maxSnapshots)+","+std::to_string(r.limits.maxDatabaseBytes)+"],"+std::to_string(r.nowUtcMs)+",[";
        for (size_t i = 0; i < r.queries.size(); ++i) { if (i) text += ','; const auto &q = r.queries[i]; text += std::string("[")+(q.cumulative?"true":"false")+","+std::to_string(q.fromExclusiveMs)+","+std::to_string(q.throughInclusiveMs)+"]"; }
        text += "],";
        if (r.batch) {
            const auto &b = *r.batch; text += "["+std::to_string(b.sequence)+","+jsonQuote(b.snapshotId)+","+std::to_string(b.sourceAsOfMs)+","+std::to_string(b.observedAtMs)+",[";
            for (size_t i = 0; i < b.records.size(); ++i) { if (i) text += ','; const auto &v = b.records[i]; text += "["+jsonQuote(v.id)+","+std::to_string(v.revision)+","+std::to_string(static_cast<unsigned>(v.kind))+","+jsonQuote(v.reference)+","+std::to_string(v.postedAtMs)+","+jsonQuote(v.amount.text())+"]"; }
            text += "],[";
            for (size_t i = 0; i < b.coverage.size(); ++i) { if (i) text += ','; text += "["+std::to_string(b.coverage[i].fromExclusiveMs)+","+std::to_string(b.coverage[i].throughInclusiveMs)+"]"; }
            text += "]]";
        } else text += "null";
        text += "]";
        if (text.size() > localTaskRequestLimit || !boundedPublicJson(text)) return false;
        *wire = std::move(text); return true;
    } catch (...) { return false; }
}
bool decodeCreditRequest(const std::string &wire, CreditProcessRequest *out) {
    if (!out || wire.size() > localTaskRequestLimit || !boundedPublicJson(wire)) return false;
    JsonValue root;
    if (!parseJsonStrict(wire,&root,nullptr) || !array(root,9)) return false;
    const auto &a = root.items; CreditProcessRequest r; std::string path; uint64_t n[4] = {};
    if (!a[0].isString() || a[0].text != "CMCR1" || !u(a[1],r.generation,UINT64_MAX) || !u(a[2],r.ticket,UINT64_MAX) || !str(a[3],path) || !contract(a[4],r.binding) || !array(a[5],4) ||
        !u(a[5].items[0],n[0],100000) || !u(a[5].items[1],n[1],400000) || !u(a[5].items[2],n[2],400000) || !u(a[5].items[3],n[3],256*1024*1024) || !si(a[6],r.nowUtcMs) || !a[7].isArray() || a[7].items.size() > 4) return false;
    try { r.file = std::filesystem::u8path(path); } catch (...) { return false; }
    r.limits = {static_cast<size_t>(n[0]),static_cast<size_t>(n[1]),static_cast<size_t>(n[2]),n[3]};
    for (const auto &v : a[7].items) { CreditQuerySpec q; if (!array(v,3) || !v.items[0].isBool() || !si(v.items[1],q.fromExclusiveMs) || !si(v.items[2],q.throughInclusiveMs)) return false; q.cumulative = v.items[0].boolean; r.queries.push_back(q); }
    if (!null(a[8])) {
        const auto &v = a[8]; if (!array(v,6)) return false;
        const auto &b = v.items; CreditBatch batch; batch.binding = r.binding;
        if (!u(b[0],batch.sequence,INT64_MAX) || !str(b[1],batch.snapshotId) || !si(b[2],batch.sourceAsOfMs) || !si(b[3],batch.observedAtMs) || !b[4].isArray() || b[4].items.size() > 512 || !b[5].isArray() || b[5].items.size() > 128) return false;
        for (const auto &v : b[4].items) {
            CreditRevision c; uint64_t kind = 0;
            if (!array(v,6) || !str(v.items[0],c.id) || !u(v.items[1],c.revision,INT64_MAX) || !u(v.items[2],kind,1) || !str(v.items[3],c.reference) || !si(v.items[4],c.postedAtMs) || !dec(v.items[5],c.amount)) return false;
            c.kind = static_cast<CreditKind>(kind); batch.records.push_back(std::move(c));
        }
        for (const auto &v : b[5].items) { CreditCoverage c; if (!array(v,2) || !si(v.items[0],c.fromExclusiveMs) || !si(v.items[1],c.throughInclusiveMs)) return false; batch.coverage.push_back(c); }
        r.batch = std::move(batch);
    }
    if (!validRequest(r)) return false;
    *out = std::move(r); return true;
}
bool encodeCreditReply(const CreditProcessRequest &r, const CreditProcessReply &reply, std::string *wire) {
    if (!wire || !validRequest(r) || reply.totals.size() > 4) return false;
    std::string text = "[\"CMCA1\","+std::to_string(reply.generation)+","+std::to_string(reply.ticket)+","+std::to_string(static_cast<unsigned>(reply.opened))+","+
        (reply.applied?std::to_string(static_cast<unsigned>(*reply.applied)):"null")+",[";
    for (size_t i = 0; i < reply.totals.size(); ++i) { if (i) text += ','; text += totals(reply.totals[i]); }
    text += "]]"; CreditProcessReply checked;
    if (!decodeCreditReply(r,text,&checked)) return false;
    *wire = std::move(text); return true;
}
bool decodeCreditReply(const CreditProcessRequest &r, const std::string &wire, CreditProcessReply *out) {
    if (!out || !validRequest(r) || wire.size() > localTaskResponseLimit || !boundedPublicJson(wire)) return false;
    JsonValue root; if (!parseJsonStrict(wire,&root,nullptr) || !array(root,6)) return false;
    const auto &a = root.items; CreditProcessReply reply;
    if (!a[0].isString() || a[0].text != "CMCA1" || !u(a[1],reply.generation,UINT64_MAX) || !u(a[2],reply.ticket,UINT64_MAX) || reply.generation != r.generation || reply.ticket != r.ticket || !status(a[3],reply.opened) || !a[5].isArray()) return false;
    if (!null(a[4])) { CreditLedgerStatus s; if (!status(a[4],s)) return false; reply.applied = s; }
    if (reply.opened != CreditLedgerStatus::Ok) { if (reply.applied || !a[5].items.empty()) return false; }
    else {
        if (reply.applied.has_value() != r.batch.has_value() || a[5].items.size() != r.queries.size()) return false;
        for (size_t i = 0; i < r.queries.size(); ++i) { CreditTotals t; if (!totals(a[5].items[i],r,r.queries[i],t)) return false; reply.totals.push_back(std::move(t)); }
    }
    *out = std::move(reply); return true;
}
CreditProcessReply executeCreditRequest(const CreditProcessRequest &r) {
    CreditProcessReply reply; reply.generation = r.generation; reply.ticket = r.ticket;
    if (!validRequest(r)) { reply.opened = CreditLedgerStatus::Invalid; return reply; }
    CreditLedger ledger; reply.opened = ledger.open(r.file,r.binding,r.nowUtcMs,r.limits);
    if (reply.opened != CreditLedgerStatus::Ok) return reply;
    if (r.batch) reply.applied = ledger.apply(*r.batch,r.nowUtcMs);
    for (const auto &q : r.queries) {
        auto t = q.cumulative ? ledger.cumulative(q.throughInclusiveMs,r.nowUtcMs) : ledger.query(q.fromExclusiveMs,q.throughInclusiveMs,r.nowUtcMs);
        if (t.status != CreditLedgerStatus::Ok) { const auto s = t.status; t = {}; t.status = s; t.qualification = r.binding.qualification; t.scope = r.binding.scope; t.fromExclusiveMs = q.cumulative ? r.binding.historyStartMs-1 : q.fromExclusiveMs; t.throughInclusiveMs = q.throughInclusiveMs; }
        reply.totals.push_back(std::move(t));
    }
    return reply;
}
CreditProcessResult CreditProcessClient::transact(const CreditProcessRequest &request, const std::atomic<bool> &cancel, unsigned timeoutMs) {
    CreditProcessResult result; std::string wire;
    if (cancel.load()) { result.transport = LocalTaskStatus::Cancelled; return result; }
    if (!encodeCreditRequest(request,&wire)) { result.transport = LocalTaskStatus::Invalid; return result; }
    auto task = process_.run(wire,cancel,timeoutMs); result.transport = task.status;
    if (task.status == LocalTaskStatus::Ok) {
        CreditProcessReply reply;
        if (decodeCreditReply(request,task.response,&reply)) result.reply = std::move(reply);
        else result.transport = LocalTaskStatus::Unavailable;
    }
    result.writeOutcomeUnknown = request.batch.has_value() && task.childStarted && (!result.reply || (result.reply->applied && *result.reply->applied == CreditLedgerStatus::Storage));
    return result;
}
}  // namespace om
