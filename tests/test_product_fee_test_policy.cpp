#include "../src/product/fee_session.h"
#include "product_test_util.h"

int main() {
    const om::FeePolicy policy = om::productFeePolicy();
    CHECK(std::string(om::feePolicyBuildLabel()) == "test");
    CHECK(policy.cycleMs == 10ull * 60 * 1000);
    CHECK(policy.windowMs == 60ull * 1000);
    CHECK(policy.retryBackoffMs == 2ull * 60 * 1000);
    return finishTests("fee_test_policy");
}
