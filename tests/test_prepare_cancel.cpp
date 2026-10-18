#include "../src/core/preparation_cancellation.h"
#include "product_test_util.h"

int main() {
    volatile sig_atomic_t stop = 0;
    om::PreparationCancellation cancellation;
    cancellation.bind(&stop);
    int completed = 0;
    const bool result = cancellation.runStage([&] { ++completed; return true; }) &&
        cancellation.runStage([&] { ++completed; stop = 1; return true; }) &&
        cancellation.runStage([&] { ++completed; return true; });
    CHECK(!result);
    CHECK(completed == 2);
    CHECK(cancellation.requested());

    stop = 0;
    CHECK(cancellation.runStage([&] { ++completed; return false; }) == false);
    CHECK(completed == 3);
    stop = 1;
    CHECK(cancellation.runStage([&] { ++completed; return true; }) == false);
    CHECK(completed == 3);
    return finishTests("test_prepare_cancel");
}
