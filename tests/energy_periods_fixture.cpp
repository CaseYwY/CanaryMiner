#include "product/energy_authorization.h"
#include "product/energy_recovery.h"
#include "product/energy_collector_process.h"
#include "product/energy_runtime.h"
#include "product/local_store_lease.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    using namespace om;
    using Status = EnergyHistoryStatus;
    if (argc != 4) return 1;
    const std::string_view scenario(argv[3]);
    const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    if (scenario == "hold" || scenario == "hold-legacy" || scenario == "hold-operation") {
        LocalStoreLease lease;
        const auto auth = root / energyAuthorizationName;
        const auto lock = scenario == "hold" ? energyHistoryActivityPath(auth) :
                          scenario == "hold-legacy" ? energyCollectorLeasePath(auth) : energyCollectorOperationPath(auth);
        if (!lease.acquire(lock)) return 1;
        std::cout << "ready" << std::endl;
        std::cin.get();
        return 0;
    }
    const auto reset = std::stoll(argv[2]);
    const int64_t scale = scenario == "priced-v1" || scenario == "priced-v2" ? 5 : 1;
    const auto utc = [=](int64_t offset) { return reset + offset * scale; };
    const auto mono = [=](unsigned offset) { return 100 + offset * static_cast<unsigned>(scale); };
    EnergyAuthorizationRequest auth;
    auth.generation = auth.ticket = 1;
    auth.file = root / energyAuthorizationName;
    auth.action = EnergyAuthorizationAction::Enroll;
    auth.expected = "absent";
    auth.nowUtcMs = utc(-2000);
    auth.devices = {"GPU-aaaaaaaa-0000-0000-0000-000000000001", "GPU-bbbbbbbb-0000-0000-0000-000000000002"};
    if (scenario != "v1" && scenario != "priced-v1") auth.limits.compaction = EnergyCompactionPolicy{2, 32768};
    const auto enrollment = executeEnergyAuthorizationRequest(auth);
    if (enrollment.status != EnergyAuthorizationStatus::Ok || !enrollment.document) return 2;
    if (scenario == "empty") return 0;

    EnergyHistoryRequest request;
    request.generation = request.ticket = 1;
    request.file = energyAuthorizationStorePath(auth.file, *enrollment.document);
    request.binding = enrollment.document->binding;
    request.limits = enrollment.document->limits;
    request.recovery = EnergyRecoveryMode::Submit;
    const std::string session = "fixture.local-session";
    EnergyHistoryBatch first;
    first.sequence = 1;
    first.id = "energy.1";
    first.observedAtMs = utc(2000);
    first.sessionId = session;
    first.start = EnergyHistorySession{session, utc(-2000), mono(0), 60000};
    const bool zero = scenario == "zero";
    first.readings = {{0, utc(-2000), mono(0), 1, zero ? 0U : 100000U}, {1, utc(-2000), mono(0), 1, zero ? 0U : 200000U},
                      {0, utc(2000), mono(4000), 1, 0}, {1, utc(2000), mono(4000), 1, 0}};
    request.nowUtcMs = first.observedAtMs;
    request.batch = first;
    if (executeEnergyHistoryRequest(request).applied != Status::Ok) return 3;

    EnergyHistoryBatch second;
    second.sequence = 2;
    second.id = "energy.2";
    second.observedAtMs = utc(5000);
    second.sessionId = session;
    // A new continuity epoch prevents a hold across the final gap, even when the elapsed span is short.
    second.readings = {{0, utc(4000), mono(6000), 1, 0}, {1, utc(4000), mono(6000), 1, 0},
                       {0, utc(5000), mono(7000), 2, 0}, {1, utc(5000), mono(7000), 2, 0}};
    request.nowUtcMs = second.observedAtMs;
    request.batch = second;
    if (scenario != "prepared" && scenario != "committed") {
        return executeEnergyHistoryRequest(request).applied == Status::Ok ? 0 : 4;
    }

    EnergyRecoveryJournal journal;
    EnergyHistoryLedger ledger;
    if (journal.open(request) != Status::Ok ||
        ledger.open(request.file, request.binding, request.nowUtcMs, EnergyHistoryOpen::Existing, request.limits) != Status::Ok ||
        journal.prepare(request, ledger.checkpoint(request.nowUtcMs)) != Status::Ok)
        return 5;
    if (scenario == "committed" && ledger.apply(second, request.nowUtcMs) != Status::Ok) return 6;
    return 0;
}
