#include "credit_ledger.h"
#include "json_dom.h"
#include "local_store_lease.h"
#include "sqlite_runtime.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace om {
namespace {
constexpr int64_t kLastMs = 253402300799999LL;
constexpr int64_t kApplication = 0x434d434c;
bool instant(int64_t value) { return value > 0 && value <= kLastMs; }
bool identifier(const std::string &value) {
    if (value.empty() || value.size() > 128) return false;
    for (char c : value) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '-' && c != '_' && c != '.' && c != ':') return false;
    return true;
}
bool validContract(const CreditSourceContract &c) {
    return c.qualification == CreditQualification::SyntheticFixture && c.asset == CreditAsset::PearlMainnetNative &&
        (c.scope == CreditScope::Worker || c.scope == CreditScope::Account) && identifier(c.provider) && c.provider.rfind("fixture.", 0) == 0 &&
        identifier(c.contractVersion) && identifier(c.scopeId) && identifier(c.scopeGeneration) && c.timeBasis == CreditTimeBasis::PostedUtcMilliseconds &&
        c.revisionModel == CreditRevisionModel::ReplacementValues && c.amountBasis == CreditAmountBasis::PostedNetOfPoolCharges &&
        c.scaleDeclared && c.amountScale <= ExactDecimal::maxScale && instant(c.historyStartMs) &&
        (c.scope == CreditScope::Account ? c.accountScopeAcknowledged : !c.accountScopeAcknowledged) &&
        (!c.cursor || (identifier(c.cursor->version) && c.cursor->version.rfind("fixture.",0)==0 && validCreditCursorToken(c.cursor->initial)));
}
std::string bindingText(const CreditSourceContract &c) {
    return "[" + std::to_string(static_cast<unsigned>(c.qualification)) + "," + std::to_string(static_cast<unsigned>(c.asset)) + "," +
        std::to_string(static_cast<unsigned>(c.scope)) + "," + jsonQuote(c.provider) + "," + jsonQuote(c.contractVersion) + "," + jsonQuote(c.scopeId) + "," +
        jsonQuote(c.scopeGeneration) + "," + std::to_string(static_cast<unsigned>(c.timeBasis)) + "," + std::to_string(static_cast<unsigned>(c.revisionModel)) + "," +
        std::to_string(static_cast<unsigned>(c.amountBasis)) + "," +
        std::to_string(c.scaleDeclared ? 1 : 0) + "," + std::to_string(c.accountScopeAcknowledged ? 1 : 0) + "," + std::to_string(c.amountScale) + "," + std::to_string(c.historyStartMs) +
        (c.cursor ? ",["+jsonQuote(c.cursor->version)+","+jsonQuote(c.cursor->initial)+"]" : "") + "]";
}
bool validRecord(const CreditRevision &r, const CreditSourceContract &c, int64_t asOf) {
    if (!identifier(r.id) || !r.revision || r.revision > static_cast<uint64_t>(INT64_MAX) || !instant(r.postedAtMs) ||
        r.postedAtMs < c.historyStartMs || r.postedAtMs > asOf || r.amount.scale() > c.amountScale) return false;
    if (r.kind == CreditKind::PostedCredit) return r.reference.empty() && !r.amount.negative();
    return r.kind == CreditKind::PostedAdjustment && identifier(r.reference) && r.reference != r.id;
}
std::string recordText(const CreditRevision &r) {
    return "[" + jsonQuote(r.id) + "," + std::to_string(r.revision) + "," + std::to_string(static_cast<unsigned>(r.kind)) + "," + jsonQuote(r.reference) + "," +
        std::to_string(r.postedAtMs) + "," + jsonQuote(r.amount.text()) + "]";
}
bool sameRecord(const CreditRevision &a, const CreditRevision &b) { return recordText(a) == recordText(b); }
bool sameJson(const JsonValue &a, const JsonValue &b) {
    if (a.type != b.type || a.text != b.text || a.boolean != b.boolean || a.items.size() != b.items.size() || a.members.size() != b.members.size()) return false;
    for (size_t i = 0; i < a.items.size(); ++i) if (!sameJson(a.items[i], b.items[i])) return false;
    for (size_t i = 0; i < a.members.size(); ++i) if (a.members[i].first != b.members[i].first || !sameJson(a.members[i].second,b.members[i].second)) return false;
    return true;
}
bool normalizeCoverage(std::vector<CreditCoverage> *ranges, const CreditSourceContract &c, int64_t asOf) {
    if (ranges->size() > 128) return false;
    for (const auto &r : *ranges) if (r.fromExclusiveMs < c.historyStartMs - 1 || r.fromExclusiveMs >= r.throughInclusiveMs || r.throughInclusiveMs > asOf) return false;
    std::sort(ranges->begin(), ranges->end(), [](auto a, auto b) { return a.fromExclusiveMs < b.fromExclusiveMs; });
    std::vector<CreditCoverage> merged;
    for (auto r : *ranges) {
        if (!merged.empty() && r.fromExclusiveMs <= merged.back().throughInclusiveMs) merged.back().throughInclusiveMs = (std::max)(merged.back().throughInclusiveMs, r.throughInclusiveMs);
        else merged.push_back(r);
    }
    *ranges = std::move(merged); return true;
}
std::string canonicalBatch(const CreditBatch &b) {
    std::string text = "[" + bindingText(b.binding) + "," + std::to_string(b.sequence) + "," + jsonQuote(b.snapshotId) + "," + std::to_string(b.sourceAsOfMs) + ",[";
    for (size_t i = 0; i < b.records.size(); ++i) { if (i) text += ','; text += recordText(b.records[i]); }
    text += "],[";
    for (size_t i = 0; i < b.coverage.size(); ++i) {
        if (i) text += ',';
        text += "[" + std::to_string(b.coverage[i].fromExclusiveMs) + "," + std::to_string(b.coverage[i].throughInclusiveMs) + "]";
    }
    return text + "]" + (b.cursor ? ",["+jsonQuote(b.cursor->before)+","+jsonQuote(b.cursor->after)+"]" : "") + "]";
    // Local rediscovery time is not snapshot identity; replay cannot freshen old coverage/cursors.
}
struct Param {
    bool text = false;
    int64_t integer = 0;
    std::string value;
    Param(int64_t n) : integer(n) {}
    Param(const std::string &s) : text(true), value(s) {}
};
}  // namespace

bool validCreditSourceContract(const CreditSourceContract &c) { return validContract(c); }
bool validCreditCursorToken(const std::string &s) {
    return !s.empty() && s.size()<=512 && std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33 && c<=126;});
}
bool sameCreditSourceContract(const CreditSourceContract &a, const CreditSourceContract &b) { return bindingText(a) == bindingText(b); }
bool boundedCreditBatchInput(const CreditBatch &b, int64_t now) {
    if (!validContract(b.binding) || !instant(now) || !instant(b.observedAtMs) || b.observedAtMs > now || !instant(b.sourceAsOfMs) || b.sourceAsOfMs > b.observedAtMs ||
        b.sourceAsOfMs < b.binding.historyStartMs-1 || !b.sequence || b.sequence > static_cast<uint64_t>(INT64_MAX) || !identifier(b.snapshotId) || b.records.size() > 512 || b.coverage.size() > 128) return false;
    std::set<std::string> ids;
    for (const auto &r : b.records) if (!validRecord(r,b.binding,b.sourceAsOfMs) || !ids.insert(r.id).second) return false;
    for (const auto &c : b.coverage) if (c.fromExclusiveMs < b.binding.historyStartMs-1 || c.fromExclusiveMs >= c.throughInclusiveMs || c.throughInclusiveMs > b.sourceAsOfMs) return false;
    if (b.cursor.has_value()!=b.binding.cursor.has_value() || (b.cursor && (!validCreditCursorToken(b.cursor->before) || !validCreditCursorToken(b.cursor->after) ||
        b.cursor->before==b.cursor->after || b.cursor->after==b.binding.cursor->initial || (b.sequence==1 && b.cursor->before!=b.binding.cursor->initial)))) return false;
    return true;
}

const char *creditLedgerStatusName(CreditLedgerStatus status) {
    switch (status) {
        case CreditLedgerStatus::Ok: return "ok";
        case CreditLedgerStatus::Replay: return "idempotent snapshot replay";
        case CreditLedgerStatus::StaleBatch: return "old snapshot ignored";
        case CreditLedgerStatus::Invalid: return "invalid/out-of-budget posted-credit facts";
        case CreditLedgerStatus::Unsupported: return "no qualified attributable credit contract";
        case CreditLedgerStatus::Attribution: return "asset/provider/scope/precision binding mismatch";
        case CreditLedgerStatus::Conflict: return "conflicting credit revision or snapshot";
        case CreditLedgerStatus::SequenceGap: return "credit revision-stream sequence gap";
        case CreditLedgerStatus::Clock: return "credit observation/clock boundary invalid";
        case CreditLedgerStatus::Capacity: return "credit ledger capacity/arithmetic limit";
        case CreditLedgerStatus::Storage: return "credit storage unavailable/integrity failure; file preserved";
    }
    return "unsupported credit state";
}

struct CreditLedger::Impl {
    SqliteRuntime api;
    SqliteDb *db = nullptr;
    LocalStoreLease lease;
    bool attempted = false, ready = false;
    int lastCode = 0;
    CreditSourceContract contract;
    CreditLedgerLimits limits;
    uint64_t sequence = 0;
    int64_t sourceAsOf = 0, observed = 0;
    std::string lastBatch;
    std::string nextCursor, lastSnapshot;
#ifdef CANARY_CREDIT_TESTING
    bool crash = false;
#endif
    ~Impl() { if (db) api.closeV2(db); }
    struct Statement {
        Impl *owner;
        SqliteStatement *value = nullptr;
        Statement(Impl *o, const char *sql, std::initializer_list<Param> params = {}) : owner(o) {
            owner->lastCode = owner->api.prepare(owner->db, sql, -1, &value, nullptr);
            if (owner->lastCode) return;
            int index = 0;
            for (const auto &p : params) {
                if (p.text) {
                    if (p.value.size() > 1024 * 1024) { owner->lastCode = 18; break; }
                    owner->lastCode = owner->api.bindText(value, ++index, p.value.data(), static_cast<int>(p.value.size()), reinterpret_cast<void (*)(void *)>(static_cast<intptr_t>(-1)));
                } else owner->lastCode = owner->api.bindInteger(value, ++index, p.integer);
                if (owner->lastCode) break;
            }
            if (owner->lastCode) { owner->api.finalize(value); value = nullptr; }
        }
        ~Statement() { if (value) owner->api.finalize(value); }
        int step() { if (!value) return owner->lastCode ? owner->lastCode : 1; owner->lastCode = owner->api.step(value); return owner->lastCode; }
    };
    bool exec(const std::string &sql) { lastCode = api.execute(db, sql.c_str(), nullptr, nullptr, nullptr); return lastCode == 0; }
    bool write(const char *sql, std::initializer_list<Param> params = {}) { Statement q(this, sql, params); return q.step() == 101; }
    bool text(SqliteStatement *q, int column, std::string *out) {
        if (api.columnType(q, column) != 3) return false;
        const auto *p = api.columnText(q, column);
        const int bytes = api.columnBytes(q, column);
        if (bytes < 0 || bytes > 1024 * 1024) return false;
        if (!p || (bytes && std::find(p, p + bytes, 0) != p + bytes)) return false;
        out->assign(reinterpret_cast<const char *>(p), static_cast<size_t>(bytes)); return true;
    }
    bool integer(SqliteStatement *q, int column, int64_t *out) {
        if (api.columnType(q, column) != 1) return false;
        *out = api.columnInteger(q, column); return true;
    }
    bool scalar(const char *sql, int64_t *out) {
        Statement q(this, sql); return q.step() == 100 && integer(q.value, 0, out) && q.step() == 101;
    }
    bool isText(const char *sql, const std::string &wanted) {
        Statement q(this, sql); std::string value;
        return q.step() == 100 && text(q.value, 0, &value) && value == wanted && q.step() == 101;
    }
    bool metadata() {
        Statement q(this, "SELECT binding,sequence,source_asof,observed,last_batch FROM meta WHERE id=1");
        int64_t seq = -1, asOf = -1, at = -1;
        std::string binding, batch;
        if (q.step() != 100 || !text(q.value, 0, &binding) || binding != bindingText(contract) || !integer(q.value, 1, &seq) || seq < 0 ||
            !integer(q.value, 2, &asOf) || !integer(q.value, 3, &at) || !text(q.value, 4, &batch) || q.step() != 101) return false;
        if (static_cast<uint64_t>(seq)>limits.maxSnapshots || (!seq && (asOf || at || !batch.empty())) || (seq && (!instant(asOf) || !instant(at) || asOf > at || batch.empty()))) return false;
        if (seq) {
            JsonValue root, expected;
            uint64_t number = 0, time = 0;
            if (!boundedPublicJson(batch) || !parseJsonStrict(batch,&root,nullptr) || !parseJsonStrict(bindingText(contract),&expected,nullptr) ||
                !root.isArray() || root.items.size() != (contract.cursor?7u:6u) || !sameJson(root.items[0],expected) || !root.items[1].asUint64(&number) || number != static_cast<uint64_t>(seq) ||
                !root.items[2].isString() || !identifier(root.items[2].text) || !root.items[3].asUint64(&time) || time != static_cast<uint64_t>(asOf) ||
                !root.items[4].isArray() || root.items[4].items.size() > 512 || !root.items[5].isArray() || root.items[5].items.size() > 128) return false;
            Statement snapshot(this,"SELECT sequence,source_asof,observed FROM snapshots WHERE id=?",{root.items[2].text});
            int64_t found = 0, snapshotAsOf = 0, snapshotObserved = 0;
            if (snapshot.step() != 100 || !integer(snapshot.value,0,&found) || found != seq || !integer(snapshot.value,1,&snapshotAsOf) || snapshotAsOf != asOf ||
                 !integer(snapshot.value,2,&snapshotObserved) || snapshotObserved != at || snapshot.step() != 101) return false;
            lastSnapshot=root.items[2].text;
            if(contract.cursor){
                const auto &step=root.items[6];
                if(!step.isArray()||step.items.size()!=2||!step.items[0].isString()||!step.items[1].isString()||
                    !validCreditCursorToken(step.items[0].text)||!validCreditCursorToken(step.items[1].text)||step.items[0].text==step.items[1].text)return false;
                nextCursor=step.items[1].text;
            }
        }
        if(contract.cursor){
            Statement progress(this,"SELECT sequence,cursor_before,cursor_after,id,source_asof,observed FROM snapshots ORDER BY sequence");
            std::string cursor=contract.cursor->initial;int64_t count=0,previousAsOf=0,previousObserved=0;int code=0;
            while((code=progress.step())==100){
                std::string before,after,id;int64_t number=0,pageAsOf=0,pageObserved=0;
                if(++count>seq||!integer(progress.value,0,&number)||number!=count||!text(progress.value,1,&before)||!text(progress.value,2,&after)||
                    before!=cursor||!validCreditCursorToken(after)||after==before||after==contract.cursor->initial||
                    !text(progress.value,3,&id)||!identifier(id)||!integer(progress.value,4,&pageAsOf)||!integer(progress.value,5,&pageObserved)||
                    !instant(pageAsOf)||!instant(pageObserved)||pageAsOf<contract.historyStartMs-1||pageAsOf<previousAsOf||pageObserved<previousObserved||
                    pageAsOf>pageObserved||pageAsOf>asOf||pageObserved>at)return false;
                cursor=std::move(after);previousAsOf=pageAsOf;previousObserved=pageObserved;
            }
            if(code!=101||count!=seq||(seq&&cursor!=nextCursor))return false;
            int64_t repeated=0;if(!scalar("SELECT count(*) FROM (SELECT cursor_after FROM snapshots GROUP BY cursor_after HAVING count(*)>1)",&repeated)||repeated)return false;
            if(seq){JsonValue last;if(!parseJsonStrict(batch,&last,nullptr))return false;
                Statement previous(this,"SELECT cursor_before,cursor_after FROM snapshots WHERE sequence=?",{seq});std::string before,after;
                if(previous.step()!=100||!text(previous.value,0,&before)||!text(previous.value,1,&after)||previous.step()!=101||before!=last.items[6].items[0].text||after!=last.items[6].items[1].text)return false;}
            nextCursor=cursor;
        }
        if(!seq)lastSnapshot.clear();
        int64_t snapshots = 0, invalid = 0;
        if (!scalar("SELECT count(*) FROM snapshots", &snapshots) || snapshots != seq || static_cast<uint64_t>(snapshots) > limits.maxSnapshots ||
            !scalar("SELECT count(*) FROM snapshots WHERE sequence<1 OR sequence>(SELECT sequence FROM meta WHERE id=1)",&invalid) || invalid) return false;
        sequence = static_cast<uint64_t>(seq); sourceAsOf = asOf; observed = at; lastBatch = std::move(batch); return true;
    }
    bool row(SqliteStatement *q, CreditRevision *record) {
        int64_t revision = 0, kind = -1, posted = 0;
        std::string amount;
        if (!text(q, 0, &record->id) || !integer(q, 1, &revision) || revision <= 0 || !integer(q, 2, &kind) || kind < 0 || kind > 1 ||
            !text(q, 3, &record->reference) || !integer(q, 4, &posted) || !text(q, 5, &amount) || !ExactDecimal::parse(amount, &record->amount) || record->amount.text() != amount) return false;
        record->revision = static_cast<uint64_t>(revision); record->kind = static_cast<CreditKind>(kind); record->postedAtMs = posted;
        return validRecord(*record, contract, sourceAsOf);
    }
    bool records(std::map<std::string,CreditRevision> *out) {
        Statement q(this, "SELECT id,revision,kind,reference,posted_ms,amount FROM records ORDER BY id");
        int code = 0;
        while ((code = q.step()) == 100) {
            CreditRevision record;
            if (out->size() >= limits.maxRecords || !row(q.value, &record) || !out->emplace(record.id, record).second) return false;
        }
        if (code != 101) return false;
        std::map<std::string, ExactDecimal> groups;
        for (const auto &pair : *out) if (pair.second.kind == CreditKind::PostedCredit) groups.emplace(pair.first, pair.second.amount);
        ExactDecimal all;
        for (const auto &pair : *out) {
            const auto &r = pair.second;
            if (r.kind == CreditKind::PostedAdjustment) {
                auto base = groups.find(r.reference);
                if (base == groups.end() || !base->second.add(r.amount, &base->second)) return false;
            }
            if (!all.add(r.amount, &all)) return false;
        }
        for (const auto &g : groups) if (g.second.negative()) return false;
        return !all.negative();
    }
    bool audit(const std::map<std::string,CreditRevision> &current, uint64_t lastSequence = 0, int64_t observedBound = 0) {
        if (!lastSequence) lastSequence = sequence;
        if (!observedBound) observedBound = observed;
        Statement q(this, "SELECT v.id,v.revision,v.kind,v.reference,v.posted_ms,v.amount,v.first_sequence,s.source_asof,s.observed FROM versions v LEFT JOIN snapshots s ON v.first_sequence=s.sequence ORDER BY v.id,v.revision");
        size_t count = 0; std::set<std::string> found;
        int code = 0;
        while ((code = q.step()) == 100) {
            CreditRevision r;
            if (++count > limits.maxVersions || !row(q.value, &r)) return false;
            int64_t first = 0, asOf = 0, at = 0;
            if (!integer(q.value,6,&first) || first < 1 || static_cast<uint64_t>(first) > lastSequence || !integer(q.value,7,&asOf) || !integer(q.value,8,&at) ||
                !instant(asOf) || !instant(at) || r.postedAtMs > asOf || asOf > sourceAsOf || asOf > at || at > observedBound) return false;
            const auto known = current.find(r.id);
            if (known == current.end() || r.kind != known->second.kind || r.reference != known->second.reference || r.revision > known->second.revision) return false;
            if (r.revision == known->second.revision) { if (!sameRecord(r, known->second)) return false; found.insert(r.id); }
        }
        return code == 101 && found.size() == current.size();
    }
    bool coverage(std::vector<CreditCoverage> *out) {
        Statement q(this, "SELECT start_ms,end_ms FROM coverage ORDER BY start_ms");
        int code = 0;
        while ((code = q.step()) == 100) {
            CreditCoverage r;
            if (out->size() >= 128 || !integer(q.value, 0, &r.fromExclusiveMs) || !integer(q.value, 1, &r.throughInclusiveMs) ||
                r.fromExclusiveMs < contract.historyStartMs - 1 || r.fromExclusiveMs >= r.throughInclusiveMs || r.throughInclusiveMs > sourceAsOf ||
                (!out->empty() && out->back().throughInclusiveMs >= r.fromExclusiveMs)) return false;
            out->push_back(r);
        }
        if (code != 101) return false;
        if (sequence) {
            JsonValue root;
            if (!parseJsonStrict(lastBatch,&root,nullptr) || !root.isArray() || root.items.size() != (contract.cursor?7u:6u) || !root.items[5].isArray() || root.items[5].items.size() != out->size()) return false;
            for (size_t i = 0; i < out->size(); ++i) {
                const auto &r = root.items[5].items[i]; uint64_t from = 0, through = 0;
                if (!r.isArray() || r.items.size() != 2 || !r.items[0].asUint64(&from) || !r.items[1].asUint64(&through) ||
                    from != static_cast<uint64_t>((*out)[i].fromExclusiveMs) || through != static_cast<uint64_t>((*out)[i].throughInclusiveMs)) return false;
            }
        }
        return true;
    }
    CreditLedgerStatus storageFailure() { ready = false; return CreditLedgerStatus::Storage; }
};

CreditLedger::CreditLedger() : impl_(new Impl()) {}
CreditLedger::~CreditLedger() = default;
CreditLedgerStatus CreditLedger::open(const std::filesystem::path &path, const CreditSourceContract &contract, int64_t now, CreditLedgerLimits limits) {
    auto &s = *impl_;
    if (s.attempted) return CreditLedgerStatus::Invalid;
    s.attempted = true;
    if (contract.qualification != CreditQualification::SyntheticFixture || contract.asset != CreditAsset::PearlMainnetNative) return CreditLedgerStatus::Unsupported;
    if (!validContract(contract) || !instant(now) || contract.historyStartMs > now || !path.is_absolute() || !limits.maxRecords || limits.maxRecords > 100000 ||
        !limits.maxVersions || limits.maxVersions > 400000 || limits.maxVersions < limits.maxRecords || !limits.maxSnapshots || limits.maxSnapshots > 400000 || limits.maxDatabaseBytes < 1024 * 1024 ||
        limits.maxDatabaseBytes > 256 * 1024 * 1024) return CreditLedgerStatus::Invalid;
    s.contract = contract; s.limits = limits;
    try {
        if (!s.api.load()) return CreditLedgerStatus::Storage;
        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return CreditLedgerStatus::Storage;
        auto lock = path; lock += ".lock";
        if (!s.lease.acquire(lock)) return CreditLedgerStatus::Storage;
        const auto status = std::filesystem::symlink_status(path, ec);
        const bool absent = ec == std::errc::no_such_file_or_directory || (!ec && status.type() == std::filesystem::file_type::not_found);
        if (!absent && (ec || !std::filesystem::is_regular_file(status) || std::filesystem::file_size(path) > limits.maxDatabaseBytes)) return CreditLedgerStatus::Storage;
        if (absent) {
#if defined(_WIN32)
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return CreditLedgerStatus::Storage;
            CloseHandle(file);
#else
            const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (fd < 0) return CreditLedgerStatus::Storage;
            ::close(fd);
#endif
        }
        if (s.api.openV2(path.u8string().c_str(), &s.db, 2 | 0x10000 | 0x1000000, nullptr) != 0) return CreditLedgerStatus::Storage;
        s.api.busyTimeout(s.db, 0);
        s.api.limit(s.db, 0, 1024 * 1024 + 4096); s.api.limit(s.db, 1, 65536); s.api.limit(s.db, 2, 128); s.api.limit(s.db, 3, 32); s.api.limit(s.db, 7, 0);
        int64_t app = 0, version = 0;
        if (!s.scalar("PRAGMA application_id", &app) || !s.scalar("PRAGMA user_version", &version) || (!absent && (app != kApplication || version != (contract.cursor?2:1)))) return CreditLedgerStatus::Storage;
        if (!s.exec("PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON; PRAGMA temp_store=MEMORY; PRAGMA cache_size=-2048;")) return CreditLedgerStatus::Storage;
        if (!absent) {
            int64_t extra = 0;
            if (!s.isText("PRAGMA quick_check(1)", "ok") || !s.scalar("SELECT count(*) FROM sqlite_master WHERE type IN ('trigger','view') OR (type='table' AND name NOT IN ('meta','records','versions','coverage','snapshots'))", &extra) || extra) return CreditLedgerStatus::Storage;
            if (!s.isText("SELECT binding FROM meta WHERE id=1", bindingText(contract))) return CreditLedgerStatus::Attribution;
            if (!s.metadata()) return CreditLedgerStatus::Storage;
            if (now < s.observed) return CreditLedgerStatus::Clock;
            std::map<std::string,CreditRevision> rows; std::vector<CreditCoverage> ranges;
            if (!s.records(&rows) || !s.audit(rows) || !s.coverage(&ranges)) return CreditLedgerStatus::Storage;
        }
        if (!s.isText("PRAGMA journal_mode=WAL", "wal") || !s.exec("PRAGMA synchronous=FULL; PRAGMA wal_autocheckpoint=256; PRAGMA journal_size_limit=1048576;")) return CreditLedgerStatus::Storage;
        int64_t page = 0;
        if (!s.scalar("PRAGMA page_size", &page) || page < 512 || page > 65536 || !s.exec("PRAGMA max_page_count=" + std::to_string(limits.maxDatabaseBytes / static_cast<uint64_t>(page)))) return CreditLedgerStatus::Storage;
        if (absent) {
            if (!s.exec("BEGIN IMMEDIATE; CREATE TABLE meta(id INTEGER PRIMARY KEY CHECK(id=1),binding TEXT NOT NULL,sequence INTEGER NOT NULL,source_asof INTEGER NOT NULL,observed INTEGER NOT NULL,last_batch TEXT NOT NULL);"
                "CREATE TABLE records(id TEXT PRIMARY KEY NOT NULL,revision INTEGER NOT NULL,kind INTEGER NOT NULL,reference TEXT NOT NULL,posted_ms INTEGER NOT NULL,amount TEXT NOT NULL);"
                "CREATE TABLE versions(id TEXT NOT NULL,revision INTEGER NOT NULL,kind INTEGER NOT NULL,reference TEXT NOT NULL,posted_ms INTEGER NOT NULL,amount TEXT NOT NULL,first_sequence INTEGER NOT NULL REFERENCES snapshots(sequence),PRIMARY KEY(id,revision));"
                "CREATE TABLE coverage(start_ms INTEGER PRIMARY KEY,end_ms INTEGER NOT NULL);")) return CreditLedgerStatus::Storage;
            if(!s.exec(std::string("CREATE TABLE snapshots(id TEXT PRIMARY KEY NOT NULL,sequence INTEGER NOT NULL UNIQUE,source_asof INTEGER NOT NULL,observed INTEGER NOT NULL")+
                (contract.cursor?",cursor_before TEXT NOT NULL,cursor_after TEXT NOT NULL UNIQUE":"")+");"))return CreditLedgerStatus::Storage;
            if (!s.write("INSERT INTO meta VALUES(1,?,0,0,0,'')", {bindingText(contract)}) ||
                !s.exec("PRAGMA application_id=" + std::to_string(kApplication) + "; PRAGMA user_version="+std::to_string(contract.cursor?2:1)+"; COMMIT;")) { s.exec("ROLLBACK;"); return CreditLedgerStatus::Storage; }
            if(contract.cursor)s.nextCursor=contract.cursor->initial;
        }
        s.ready = true; return CreditLedgerStatus::Ok;
    } catch (...) { return CreditLedgerStatus::Storage; }
}

CreditLedgerStatus CreditLedger::apply(const CreditBatch &input, int64_t now) {
    auto &s = *impl_;
    if (!s.ready) return CreditLedgerStatus::Storage;
    if (!validContract(input.binding) || bindingText(input.binding) != bindingText(s.contract)) return CreditLedgerStatus::Attribution;
    if (!instant(now) || !instant(input.observedAtMs) || input.observedAtMs > now || !instant(input.sourceAsOfMs) || input.sourceAsOfMs > input.observedAtMs ||
        input.sourceAsOfMs < s.contract.historyStartMs - 1) return CreditLedgerStatus::Clock;
    if (!input.sequence || input.sequence > static_cast<uint64_t>(INT64_MAX) || !identifier(input.snapshotId) || input.records.size() > 512) return CreditLedgerStatus::Invalid;
    if(input.cursor.has_value()!=s.contract.cursor.has_value() || (input.cursor && !boundedCreditBatchInput(input,now)))return CreditLedgerStatus::Invalid;
    try {
        CreditBatch batch = input;
        if (!normalizeCoverage(&batch.coverage, s.contract, batch.sourceAsOfMs)) return CreditLedgerStatus::Invalid;
        std::sort(batch.records.begin(), batch.records.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        for (size_t i = 0; i < batch.records.size(); ++i)
            if (!validRecord(batch.records[i], s.contract, batch.sourceAsOfMs) || (i && batch.records[i - 1].id == batch.records[i].id)) return CreditLedgerStatus::Invalid;
        const auto canonical = canonicalBatch(batch);
        if (canonical.size() > 1024 * 1024) return CreditLedgerStatus::Capacity;
        if (!s.exec("BEGIN IMMEDIATE;")) return CreditLedgerStatus::Storage;
        struct Transaction { Impl &s; bool committed = false; ~Transaction() { if (!committed && !s.exec("ROLLBACK;")) s.ready = false; } } transaction{s};
        if (!s.metadata()) return s.storageFailure();
        if (now < s.observed) return CreditLedgerStatus::Clock;
        if (batch.sequence < s.sequence) return CreditLedgerStatus::StaleBatch;
        if (batch.sequence == s.sequence) return canonical == s.lastBatch ? CreditLedgerStatus::Replay : CreditLedgerStatus::Conflict;
        if (batch.sequence != s.sequence + 1) return CreditLedgerStatus::SequenceGap;
        if(batch.cursor){
            if(batch.cursor->before!=s.nextCursor)return CreditLedgerStatus::Conflict;
            Impl::Statement used(&s,"SELECT sequence FROM snapshots WHERE cursor_after=?",{batch.cursor->after});
            const int code=used.step();if(code==100)return CreditLedgerStatus::Conflict;if(code!=101)return s.storageFailure();
        }
        if (batch.observedAtMs < s.observed || batch.sourceAsOfMs < s.sourceAsOf) return CreditLedgerStatus::Clock;
        if (batch.sequence > s.limits.maxSnapshots) return CreditLedgerStatus::Capacity;
        { Impl::Statement prior(&s,"SELECT sequence FROM snapshots WHERE id=?",{batch.snapshotId});
            const int code = prior.step(); if (code == 100) return CreditLedgerStatus::Conflict; if (code != 101) return s.storageFailure(); }
        const auto oldAsOf = s.sourceAsOf;
        struct Restore { Impl &s; int64_t old; bool committed = false; ~Restore() { if (!committed) s.sourceAsOf = old; } } restore{s,oldAsOf};
        s.sourceAsOf = batch.sourceAsOfMs;
        if(batch.cursor){if(!s.write("INSERT INTO snapshots VALUES(?,?,?,?,?,?)",{batch.snapshotId,static_cast<int64_t>(batch.sequence),batch.sourceAsOfMs,batch.observedAtMs,batch.cursor->before,batch.cursor->after}))return s.storageFailure();}
        else if (!s.write("INSERT INTO snapshots VALUES(?,?,?,?)",{batch.snapshotId,static_cast<int64_t>(batch.sequence),batch.sourceAsOfMs,batch.observedAtMs})) return s.storageFailure();
        for (const auto &r : batch.records) {
            Impl::Statement old(&s, "SELECT id,revision,kind,reference,posted_ms,amount FROM versions WHERE id=? AND revision=?", {r.id, static_cast<int64_t>(r.revision)});
            const int code = old.step();
            if (code == 100) { CreditRevision known; if (!s.row(old.value, &known) || !sameRecord(known,r)) return CreditLedgerStatus::Conflict; }
            else if (code != 101) return s.storageFailure();
            Impl::Statement current(&s, "SELECT id,revision,kind,reference,posted_ms,amount FROM records WHERE id=?", {r.id});
            const int currentCode = current.step();
            CreditRevision known;
            if (currentCode == 100 && (!s.row(current.value, &known) || r.kind != known.kind || r.reference != known.reference)) return CreditLedgerStatus::Conflict;
            if (currentCode != 100 && currentCode != 101) return s.storageFailure();
            if (code == 101 && !s.write("INSERT INTO versions VALUES(?,?,?,?,?,?,?)", {r.id, static_cast<int64_t>(r.revision), static_cast<int64_t>(r.kind), r.reference, r.postedAtMs, r.amount.text(),static_cast<int64_t>(batch.sequence)})) return s.storageFailure();
            if (currentCode == 101 || known.revision < r.revision) {
                if (!s.write("INSERT INTO records VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET revision=excluded.revision,kind=excluded.kind,reference=excluded.reference,posted_ms=excluded.posted_ms,amount=excluded.amount",
                    {r.id, static_cast<int64_t>(r.revision), static_cast<int64_t>(r.kind), r.reference, r.postedAtMs, r.amount.text()})) return s.storageFailure();
            }
        }
        int64_t recordCount = 0, versionCount = 0;
        if (!s.scalar("SELECT count(*) FROM records", &recordCount) || !s.scalar("SELECT count(*) FROM versions", &versionCount)) return s.storageFailure();
        if (recordCount < 0 || versionCount < 0 || static_cast<uint64_t>(recordCount) > s.limits.maxRecords || static_cast<uint64_t>(versionCount) > s.limits.maxVersions) return CreditLedgerStatus::Capacity;
        std::map<std::string,CreditRevision> rows;
        if (!s.records(&rows) || !s.audit(rows,batch.sequence,batch.observedAtMs)) return CreditLedgerStatus::Invalid;
        if (!s.exec("DELETE FROM coverage;")) return s.storageFailure();
        for (const auto &r : batch.coverage) if (!s.write("INSERT INTO coverage VALUES(?,?)", {r.fromExclusiveMs,r.throughInclusiveMs})) return s.storageFailure();
        if (!s.write("UPDATE meta SET sequence=?,source_asof=?,observed=?,last_batch=? WHERE id=1", {static_cast<int64_t>(batch.sequence),batch.sourceAsOfMs,batch.observedAtMs,canonical})) return s.storageFailure();
#ifdef CANARY_CREDIT_TESTING
        if (s.crash) std::_Exit(17);
#endif
        if (!s.exec("COMMIT;")) return s.storageFailure();
        transaction.committed = true; restore.committed = true;
        s.sequence = batch.sequence; s.observed = batch.observedAtMs; s.lastBatch = canonical;
        s.lastSnapshot=batch.snapshotId;if(batch.cursor)s.nextCursor=batch.cursor->after;
        return CreditLedgerStatus::Ok;
    } catch (...) { return s.storageFailure(); }
}

CreditLedgerStatus CreditLedger::verifyLatest(const CreditBatch &input, int64_t now) {
    auto &s=*impl_;
    if (!s.ready) return CreditLedgerStatus::Storage;
    if (!sameCreditSourceContract(input.binding,s.contract)) return CreditLedgerStatus::Attribution;
    if (!boundedCreditBatchInput(input,now)) return CreditLedgerStatus::Invalid;
    try {
        auto batch=input;
        if (!normalizeCoverage(&batch.coverage,s.contract,batch.sourceAsOfMs)) return CreditLedgerStatus::Invalid;
        std::sort(batch.records.begin(),batch.records.end(),[](const auto &a,const auto &b){return a.id<b.id;});
        const auto canonical=canonicalBatch(batch);
        if (!s.exec("BEGIN;")) return s.storageFailure();
        struct ReadTransaction { Impl &s; bool ended=false; ~ReadTransaction() { if (!ended && !s.exec("ROLLBACK;")) s.ready=false; } } read{s};
        if (!s.metadata()) return s.storageFailure();
        if (now<s.observed) return CreditLedgerStatus::Clock;
        const auto result=batch.sequence<s.sequence ? CreditLedgerStatus::StaleBatch :
            batch.sequence==s.sequence && canonical==s.lastBatch ? CreditLedgerStatus::Replay : CreditLedgerStatus::Conflict;
        if (!s.exec("ROLLBACK;")) return s.storageFailure();
        read.ended=true; return result;
    } catch (...) { return s.storageFailure(); }
}

CreditCursorCheckpoint CreditLedger::cursor(int64_t now){
    auto &s=*impl_;CreditCursorCheckpoint result;
    if(!s.ready)return result;
    if(!s.contract.cursor){result.status=CreditLedgerStatus::Unsupported;return result;}
    if(!instant(now)){result.status=CreditLedgerStatus::Clock;return result;}
    try{
        if(!s.exec("BEGIN;"))return result;
        struct Read {Impl &s;bool ended=false;~Read(){if(!ended&&!s.exec("ROLLBACK;"))s.ready=false;}} read{s};
        if(!s.metadata()){s.storageFailure();return result;}
        if(now<s.observed){result.status=CreditLedgerStatus::Clock;return result;}
        if(!s.exec("ROLLBACK;")){s.storageFailure();return result;}read.ended=true;
        result.status=CreditLedgerStatus::Ok;result.sequence=s.sequence;result.snapshotId=s.lastSnapshot;result.next=s.nextCursor;
        result.sourceAsOfMs=s.sourceAsOf;result.observedAtMs=s.observed;return result;
    }catch(...){s.storageFailure();return {};}
}
CreditTotals CreditLedger::query(int64_t start, int64_t end, int64_t now) {
    auto &s = *impl_;
    CreditTotals result; result.fromExclusiveMs = start; result.throughInclusiveMs = end;
    result.qualification = s.contract.qualification; result.scope = s.contract.scope;
    if (!s.ready) return result;
    if (!instant(now) || start < 0 || end <= start || !instant(end) || end > now) { result.status = CreditLedgerStatus::Invalid; return result; }
    try {
        if (!s.exec("BEGIN;")) return result;
        struct ReadTransaction { Impl &s; bool ended = false; ~ReadTransaction() { if (!ended && !s.exec("ROLLBACK;")) s.ready = false; } } read{s};
        if (!s.metadata()) { s.storageFailure(); return result; }
        if (now < s.observed) { result.status = CreditLedgerStatus::Clock; return result; }
        std::map<std::string,CreditRevision> rows; std::vector<CreditCoverage> coverage;
        if (!s.records(&rows) || !s.audit(rows) || !s.coverage(&coverage)) { s.storageFailure(); return result; }
        result.sourceAsOfMs = s.sourceAsOf; result.observedAtMs = s.observed; result.requestedMs = static_cast<uint64_t>(end - start);
        for (const auto &pair : rows) if (pair.second.postedAtMs > start && pair.second.postedAtMs <= end) {
            if (!result.knownPostedNet.add(pair.second.amount, &result.knownPostedNet)) { result.status = CreditLedgerStatus::Capacity; return result; }
            ++result.matchedRecords;
        }
        for (const auto &r : coverage) {
            const auto from = (std::max)(start,r.fromExclusiveMs), through = (std::min)(end,r.throughInclusiveMs);
            if (through > from) result.coveredMs += static_cast<uint64_t>(through - from);
        }
        result.status = CreditLedgerStatus::Ok;
        result.completeness = result.coveredMs == result.requestedMs ? CreditCompleteness::Complete :
            (result.coveredMs || result.matchedRecords ? CreditCompleteness::Partial : CreditCompleteness::Unavailable);
        if (result.completeness == CreditCompleteness::Complete) result.completePostedNet = result.knownPostedNet;
        if (!s.exec("ROLLBACK;")) { s.storageFailure(); return CreditTotals{}; }
        read.ended = true;
        return result;
    } catch (...) { s.storageFailure(); return CreditTotals{}; }
}
CreditTotals CreditLedger::cumulative(int64_t end, int64_t now) { return query(impl_->contract.historyStartMs - 1, end, now); }

CreditReferenceValue creditProductionReference(const CreditTotals &totals, const MarketLatest &market) {
    CreditReferenceValue result;
    if (totals.status != CreditLedgerStatus::Ok || totals.qualification != CreditQualification::SyntheticFixture || totals.completeness == CreditCompleteness::Unavailable) return result;
    result.complete = totals.completeness == CreditCompleteness::Complete;
    ExactDecimal value;
    if (market.tokenStatus == MarketFreshness::Available && market.tokenUsdt && !market.tokenUsdt->negative() && !market.tokenUsdt->zero() &&
        totals.knownPostedNet.multiply(*market.tokenUsdt, &value)) result.knownUsdt = value;
    if (market.usdStatus == MarketFreshness::Available && market.tokenUsd && !market.tokenUsd->negative() && !market.tokenUsd->zero() &&
        totals.knownPostedNet.multiply(*market.tokenUsd, &value)) result.knownUsd = value;
    return result;
}
#ifdef CANARY_CREDIT_TESTING
void CreditLedgerTestPeer::crashBeforeCommit(CreditLedger &ledger) { ledger.impl_->crash = true; }
#endif
}  // namespace om
