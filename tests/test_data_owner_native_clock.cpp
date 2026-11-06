// A clock is an injected callback, not permission to renew the owner's original native epoch.
// Helpers are absent; this test cannot open a real source or collect native device/game data.
#include "product/native_process_admission.h"
#include "product/billing_compute_owner.h"
#include "product/forecast_owner.h"
#include "product/game_detection_owner.h"
#include "product/market_owner.h"
#include "product/safety_telemetry_owner.h"
#include "billing_source_fixture.h"
#include "credit_fixture.h"
#include "product_test_util.h"
namespace {
using namespace om;
constexpr auto T=credit_fixture::T;
void fault(){NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();}
}
int main(){
    const auto root=std::filesystem::path(makeScratchDir("data-clock-native-scope")),helper=root/"absent-helper",file=root/"never-created.db";
    auto context=std::make_shared<BillingOwnerBinding>();context->file=file;context->binding=billing_source_fixture::binding(root/"never-source.db");
    {BillingOwner owner({helper,helper,helper,helper},*context,{true,1000},[]{fault();return BillingOwnerTime{T,10000};});CHECK(!owner.start(7));owner.stop();CHECK(!owner.start(8));}
    {BillingComputeOwner owner(helper,context,{true,1000,512},[]{fault();return BillingOwnerTime{T,10000};});CHECK(!owner.start(7));owner.stop();CHECK(!owner.start(8));}
    {auto binding=credit_fixture::contract();binding.cursor=CreditCursorContract{"fixture.exclusive-acyclic-v1","fixture:initial"};ForecastOwnerPolicy policy;policy.enabled=true;
        IncomeForecastOwner owner(helper,file,binding,policy,[]{fault();return ForecastTime{T,10000};});CHECK(!owner.start(7));owner.stop();CHECK(!owner.start(8));}
    {GameDetectionPolicy policy;policy.enabled=true;
#if defined(_WIN32)
        constexpr auto adapter=GameProcessAdapter::WindowsSessionImage;
#else
        constexpr auto adapter=GameProcessAdapter::LinuxUidNativeImage;
#endif
        GameDetectionOwner owner(helper,{"fixture.game","fixture-owned-game",adapter},policy,[]{fault();return GameGuardTime{T,10000};});CHECK(!owner.start(7));owner.stop();CHECK(!owner.start(8));}
    {SafetyTelemetryPolicy policy;policy.enabled=true;SafetyTelemetryOwner owner(helper,{"GPU-aaaaaaaa-0000-0000-0000-000000000001"},policy,[]{fault();return GameGuardTime{T,10000};});
        CHECK(!owner.start(7));owner.stop();CHECK(!owner.start(8));}
    {std::atomic<unsigned> fetches{0};MarketDataOwner owner([&](const PublicJsonRequest &,const std::atomic<bool> &,PublicHttpsLimits){++fetches;return PublicJsonResponse{};},
        {},MarketOrigin::SyntheticFixture,false,[]{fault();return std::pair<uint64_t,uint64_t>{10000,static_cast<uint64_t>(T/1000)};});
        CHECK(!owner.start(7));owner.stop();CHECK(!owner.start(8));CHECK(fetches==0);}
    CHECK(!nativeProcessUnresolved()&&!std::filesystem::exists(helper)&&!std::filesystem::exists(file)&&!std::filesystem::exists(root/"never-source.db"));
    std::filesystem::remove(root);return finishTests("six owner start callbacks cannot renew original native lifetimes");
}
