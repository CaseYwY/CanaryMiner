#include "product/credit_process.h"
#include "credit_fixture.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
#include <fstream>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#endif
using namespace om;
using namespace credit_fixture;
namespace {
bool marker(const std::filesystem::path &p) {
    auto file = p; file += ".fixture-ready";
    const auto until = std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until) { if (std::filesystem::exists(file)) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return false;
}
CreditProcessRequest request(const std::filesystem::path &file) {
    CreditProcessRequest r; r.generation = 1; r.ticket = 1; r.file = file; r.binding = contract(); r.nowUtcMs = T;
    r.queries = {{false,T-day,T},{true,0,T}}; return r;
}
void env(const char *name, const char *value) {
#if defined(_WIN32)
    _putenv_s(name,value);
#else
    if (*value) setenv(name,value,1); else unsetenv(name);
#endif
}
}
int main(int argc, char **argv) {
    CHECK(argc == 3); if (argc != 3) return finishTests("credit process");
    const auto root = std::filesystem::path(makeScratchDir("credit-process"));
    const auto helper = std::filesystem::absolute(argv[1]), fixture = std::filesystem::absolute(argv[2]);
    auto r = request(root/std::filesystem::u8path("fixture credits-\xe6\xb5\x8b\xe8\xaf\x95.db"));
    std::atomic<bool> cancel{false};
    CHECK(validCreditSourceContract(r.binding));
    auto wrong = r.binding; wrong.provider = "unqualified.actual-provider";
    CHECK(!validCreditSourceContract(wrong) && !sameCreditSourceContract(r.binding,wrong));
    std::string wire; CreditProcessRequest decoded;
    CHECK(encodeCreditRequest(r,&wire) && decodeCreditRequest(wire,&decoded));
    CHECK(decoded.file == r.file && sameCreditSourceContract(decoded.binding,r.binding) && decoded.queries.size() == 2);
    const auto original = wire;
    for (const auto &bad : std::vector<std::string>{"", "[]", original+"x", original.substr(0,original.size()-1), std::string(localTaskRequestLimit+1,' '), "[\"CMCR2\"]"}) CHECK(!decodeCreditRequest(bad,&decoded));
    auto changed = original; changed.replace(changed.find("CMCR1"),6,"CMCR2"); CHECK(!decodeCreditRequest(changed,&decoded));
    auto invalid = r; invalid.binding = wrong; CHECK(!encodeCreditRequest(invalid,&wire));
    invalid = r; invalid.generation = 0; CHECK(!encodeCreditRequest(invalid,&wire));
    invalid = r; invalid.file = "relative.db"; CHECK(!encodeCreditRequest(invalid,&wire));
#if defined(_WIN32)
    invalid = r; invalid.file += ":alternate-stream"; CHECK(!encodeCreditRequest(invalid,&wire));
#endif
    invalid = r; invalid.queries.resize(5); CHECK(!encodeCreditRequest(invalid,&wire));
    invalid = r; invalid.queries[0].throughInclusiveMs++; CHECK(!encodeCreditRequest(invalid,&wire));
    invalid = r; invalid.limits.maxSnapshots = 0; CHECK(!encodeCreditRequest(invalid,&wire));
    invalid = r; invalid.batch = batch(1,T); invalid.batch->records.resize(513); CHECK(!encodeCreditRequest(invalid,&wire));
    invalid = r; invalid.batch = batch(1,T,{record("fixture.id",1,T,"1"),record("fixture.id",1,T,"1")}); CHECK(!encodeCreditRequest(invalid,&wire));
    CHECK(!std::filesystem::exists(r.file));
    CreditProcessClient client(helper), fake(fixture);
    cancel = true; CHECK(client.transact(r,cancel,5000).transport == LocalTaskStatus::Cancelled); cancel = false;
    CHECK(!std::filesystem::exists(r.file));
    CHECK(CreditProcessClient("relative").transact(r,cancel,5000).transport == LocalTaskStatus::Invalid);
    CHECK(CreditProcessClient(root/"missing-helper").transact(r,cancel,5000).transport == LocalTaskStatus::Unavailable);
    CHECK(client.transact(r,cancel,0).transport == LocalTaskStatus::Invalid);
#if !defined(_WIN32)
    struct sigaction previous{}, ignored{}; ignored.sa_handler = SIG_IGN; sigemptyset(&ignored.sa_mask);
    CHECK(sigaction(SIGCHLD,&ignored,&previous) == 0);
    CHECK(client.transact(r,cancel,5000).transport == LocalTaskStatus::Unavailable);
    CHECK(sigaction(SIGCHLD,&previous,nullptr) == 0);
#endif
    auto result = client.transact(r,cancel,5000);
    CHECK(result.transport == LocalTaskStatus::Ok && result.reply && result.reply->opened == CreditLedgerStatus::Ok && !result.writeOutcomeUnknown);
    CHECK(result.reply && result.reply->totals.size() == 2 && result.reply->totals[0].completeness == CreditCompleteness::Unavailable && !result.reply->totals[0].completePostedNet);
    r.batch = batch(1,T,{record("fixture.large",1,T,"9007199254740993.12345678")});
    CHECK(encodeCreditRequest(r,&wire) && decodeCreditRequest(wire,&decoded));
    CHECK(decoded.batch && decoded.batch->records[0].amount.text() == "9007199254740993.12345678");
    result = client.transact(r,cancel,5000);
    CHECK(result.reply && result.reply->applied == CreditLedgerStatus::Ok && !result.writeOutcomeUnknown);
    {
        CreditLedger held; CHECK(held.open(r.file,r.binding,T) == CreditLedgerStatus::Ok);
        auto refused = client.transact(r,cancel,5000);
        CHECK(refused.reply && refused.reply->opened == CreditLedgerStatus::Storage && !refused.reply->applied && !refused.writeOutcomeUnknown);
    }
    CHECK(result.reply && result.reply->totals[0].completePostedNet && result.reply->totals[0].knownPostedNet.text() == "9007199254740993.12345678");
    CHECK(encodeCreditReply(r,*result.reply,&wire));
    CreditProcessReply reply;
    CHECK(decodeCreditReply(r,wire,&reply));
    CHECK(!decodeCreditReply(r,wire+"x",&reply));
    auto badReply = reply; badReply.ticket++; CHECK(!encodeCreditReply(r,badReply,&wire));
    badReply = reply; badReply.totals[0].coveredMs++; CHECK(!encodeCreditReply(r,badReply,&wire));
    badReply = reply; badReply.totals[0].matchedRecords = 0; CHECK(!encodeCreditReply(r,badReply,&wire));
    badReply = reply; badReply.totals[0].observedAtMs++; CHECK(!encodeCreditReply(r,badReply,&wire));
    badReply = reply; badReply.totals[0].completePostedNet.reset(); CHECK(!encodeCreditReply(r,badReply,&wire));
    r.ticket++; r.nowUtcMs += 1000; for (auto &q : r.queries) q.throughInclusiveMs = r.nowUtcMs;
    result = client.transact(r,cancel,5000);
    CHECK(result.reply && result.reply->applied == CreditLedgerStatus::Replay && result.reply->totals[0].observedAtMs == T && result.reply->totals[0].completeness == CreditCompleteness::Partial);
    invalid = r; invalid.binding.scopeGeneration = "fixture.other-generation"; invalid.batch->binding = invalid.binding;
    result = client.transact(invalid,cancel,5000);
    CHECK(result.reply && result.reply->opened == CreditLedgerStatus::Attribution && !result.reply->applied && !result.writeOutcomeUnknown);

    env("CANARY_SYNTHETIC_SECRET","must-not-inherit"); env("HTTPS_PROXY","must-not-inherit"); env("SSLKEYLOGFILE","must-not-inherit");
#if !defined(_WIN32)
    const int descriptor = open("/dev/null",O_RDONLY); CHECK(descriptor >= 0);
#endif
    r.batch.reset(); CHECK(fake.transact(r,cancel,5000).transport == LocalTaskStatus::Ok);
    // Helper stays before apply: timeout is finite and another transaction cannot run through this client's busy guard.
    r.batch = batch(2,T+1000,{record("fixture.stall-before-apply",1,T,"1")});
    CreditProcessResult stalled;
    auto start = std::chrono::steady_clock::now();
    std::thread running([&]{stalled = fake.transact(r,cancel,1200);});
    CHECK(marker(r.file));
    CHECK(fake.transact(r,cancel,5000).transport == LocalTaskStatus::Busy);
    running.join();
    CHECK(stalled.transport == LocalTaskStatus::Timeout && stalled.writeOutcomeUnknown && !stalled.reply);
    CHECK(std::chrono::steady_clock::now()-start < std::chrono::seconds(3));
    auto markerFile = r.file; markerFile += ".fixture-ready"; std::filesystem::remove(markerFile);
    auto query = r; query.batch.reset();
    result = client.transact(query,cancel,5000);
    CHECK(result.reply && result.reply->totals[0].knownPostedNet.text() == "9007199254740993.12345678");
    // Lost ACK after durable commit is UNKNOWN, not rolled back. Exact replay neither duplicates nor freshens source time.
    r.batch = batch(2,T+1000,{record("fixture.stall-after-apply",1,T,"1")});
    start = std::chrono::steady_clock::now();
    std::thread committed([&]{stalled = fake.transact(r,cancel,15000);});
    CHECK(marker(r.file)); cancel = true; committed.join();
    CHECK(stalled.transport == LocalTaskStatus::Cancelled && stalled.writeOutcomeUnknown && !stalled.reply);
    CHECK(std::chrono::steady_clock::now()-start < std::chrono::seconds(3)); cancel = false;
    result = fake.transact(r,cancel,5000);
    CHECK(result.reply && result.reply->applied == CreditLedgerStatus::Replay && result.reply->totals[0].knownPostedNet.text() == "9007199254740994.12345678");
    r.batch = batch(3,T+1000,{record("fixture.bad-ack",1,T,"2")});
    result = fake.transact(r,cancel,5000);
    CHECK(result.transport == LocalTaskStatus::Unavailable && result.writeOutcomeUnknown && !result.reply);
    result = client.transact(r,cancel,5000);
    CHECK(result.reply && result.reply->applied == CreditLedgerStatus::Replay && result.reply->totals[0].knownPostedNet.text() == "9007199254740996.12345678");
    r.batch = batch(4,T+1000,{record("fixture.overflow-ack",1,T,"3")});
    result = fake.transact(r,cancel,5000);
    CHECK(result.transport == LocalTaskStatus::Unavailable && result.writeOutcomeUnknown && !result.reply);
    r.batch.reset(); result = client.transact(r,cancel,5000);
    CHECK(result.reply && result.reply->totals[0].knownPostedNet.text() == "9007199254740999.12345678");
    // Native helper path with spaces and non-ASCII; request path remains inside IPC, not command arguments.
    const auto directory = root/std::filesystem::u8path("fixture helper \xe6\xb5\x8b\xe8\xaf\x95"); std::filesystem::create_directory(directory);
    const auto copy = directory/helper.filename(); CHECK(std::filesystem::copy_file(helper,copy));
    CHECK(CreditProcessClient(copy).transact(r,cancel,5000).transport == LocalTaskStatus::Ok);
    auto unopened = request(root/"fixture-stall-open.db");
    result = fake.transact(unopened,cancel,300);
    CHECK(result.transport == LocalTaskStatus::Timeout && !result.writeOutcomeUnknown && !std::filesystem::exists(unopened.file));
#if !defined(_WIN32)
    close(descriptor);
#endif
    env("CANARY_SYNTHETIC_SECRET",""); env("HTTPS_PROXY",""); env("SSLKEYLOGFILE","");
    std::filesystem::remove_all(root);
    return finishTests("credit process");
}
