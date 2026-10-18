// H-019 task 4 (live v28u): the guard-restart budget mirrors the five v28u cases.
#include <cstdio>

#include "product/restart_budget.h"

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-68s %s\n", name, condition ? "ok" : "FAIL");
    if (!condition) ++failures;
}

}  // namespace

int main() {
    std::puts("guard-restart budget (3 per rolling hour, 10 s delay):");
    {
        om::GuardRestartBudget budget;
        const bool first = budget.admit(0);
        const bool second = budget.admit(0);
        const bool third = budget.admit(0);
        check(first && second && third && budget.usedInWindow(0) == 3,
              "three clean guard stops restart and then mining resumes");
        check(om::GuardRestartBudget::kDelayMs == 10000, "restart delay is 10 s, not the transport backoff");
    }
    {
        om::GuardRestartBudget budget;
        check(budget.admit(1000) && budget.admit(2000) && budget.admit(3000),
              "first three stops inside the window are admitted");
        check(!budget.admit(4000) && budget.usedInWindow(4000) == 3,
              "the fourth stop within the hour stops the session");
    }
    {
        om::GuardRestartBudget budget;
        bool all = true;
        for (uint64_t t = 1300000; t <= 6500000; t += 1300000) all = all && budget.admit(t);
        check(all, "window expiry frees the budget; stops more than an hour apart all restart");
    }
    {
        om::GuardRestartBudget budget;
        check(budget.usedInWindow(0) == 0 && budget.admit(0),
              "an unused budget admits its first stop");
        check(budget.usedInWindow(3599999) == 1 && budget.usedInWindow(3600000) == 0,
              "a stop ages out exactly one hour after it happened");
    }
    std::printf("\nguard-restart budget: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
