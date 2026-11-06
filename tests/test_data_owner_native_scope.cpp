// Six additional owner entry boundaries. The helper is deliberately absent; no store or real source is available.
#include "product/native_process_admission.h"
#include "product/billing_owner.h"
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
void attempt(const std::filesystem::path &helper,const std::filesystem::path &file,bool allowed){
    auto context=std::make_shared<BillingOwnerBinding>();context->file=file;context->binding=billing_source_fixture::binding(file.parent_path()/"never-source.db");
    {BillingOwner owner({helper,helper,helper,helper},*context,{true,1000},[]{return BillingOwnerTime{T,10000};});CHECK(owner.start(7)==allowed);owner.stop();}
    {BillingComputeOwner owner(helper,context,{true,1000,512},[]{return BillingOwnerTime{T,10000};});CHECK(owner.start(7)==allowed);owner.stop();}
    {auto binding=credit_fixture::contract();binding.cursor=CreditCursorContract{"fixture.exclusive-acyclic-v1","fixture:initial"};ForecastOwnerPolicy policy;policy.enabled=true;
        IncomeForecastOwner owner(helper,file,binding,policy,[]{return ForecastTime{T,10000};});CHECK(owner.start(7)==allowed);owner.stop();}
    {GameDetectionPolicy policy;policy.enabled=true;
#if defined(_WIN32)
        constexpr auto adapter=GameProcessAdapter::WindowsSessionImage;
#else
        constexpr auto adapter=GameProcessAdapter::LinuxUidNativeImage;
#endif
        GameDetectionOwner owner(helper,{"fixture.game","fixture-owned-game",adapter},policy,[]{return GameGuardTime{T,10000};});CHECK(owner.start(7)==allowed);owner.stop();}
    {SafetyTelemetryPolicy policy;policy.enabled=true;SafetyTelemetryOwner owner(helper,{"GPU-aaaaaaaa-0000-0000-0000-000000000001"},policy,[]{return GameGuardTime{T,10000};});CHECK(owner.start(7)==allowed);owner.stop();}
    {std::atomic<unsigned> fetches{0};MarketDataOwner owner([&](const PublicJsonRequest &,const std::atomic<bool> &,PublicHttpsLimits){++fetches;return PublicJsonResponse{};},
        {},MarketOrigin::SyntheticFixture,true,[]{return std::pair<uint64_t,uint64_t>{10000,static_cast<uint64_t>(T/1000)};});
        CHECK(owner.start(7)==allowed);owner.stop();CHECK(allowed||fetches==0);}
}
}
int main(){
    const auto root=std::filesystem::path(makeScratchDir("data-original-scope")),helper=root/"explicitly-absent-helper",file=root/"never-created.db";
    CHECK(!std::filesystem::exists(helper));const auto original=nativeProcessEpoch();NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();
    {NativeProcessScope old(original);attempt(helper,file,false);}
    attempt(helper,file,true);CHECK(!std::filesystem::exists(file)&&!std::filesystem::exists(root/"never-source.db")&&!nativeProcessUnresolved());
    std::filesystem::remove(root);return finishTests("six data/safety owner original caller scopes and fresh controls (no helper/source)");
}
