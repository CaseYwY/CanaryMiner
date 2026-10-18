// Tiny check harness shared by the product-layer CPU tests.
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

static int g_checks = 0, g_failures = 0;

static void checkTrue(const char *name, bool ok) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", name);
    }
}

#define CHECK(expr) checkTrue(#expr, (expr))
#define CHECK_MSG(name, expr) checkTrue(name, (expr))

static int finishTests(const char *suite) {
    std::printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
    return g_failures ? 1 : 0;
}

/** A fresh scratch directory under the system temp dir, removed by the caller. */
[[maybe_unused]] static std::string makeScratchDir(const char *tag) {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / (std::string("canary-test-") + tag + "-" + std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base.string();
}
