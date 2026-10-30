#include "game_profit.h"
#include <algorithm>
namespace om {
namespace {
using S=GameProfitStatus;
bool key(const std::string &s){return s.size()>8&&s.size()<=128&&s.rfind("fixture.",0)==0&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});}
bool instant(int64_t t){return t>=0&&t<=253402300799999LL;}
ExactDecimal n(uint64_t v){ExactDecimal out;ExactDecimal::parse(std::to_string(v),&out);return out;}
bool ratio(const ForecastRatio &r){return !r.denominator.negative()&&!r.denominator.zero()&&r.numerator.digits()<=38&&r.denominator.digits()<=38&&r.numerator.scale()<=18&&r.denominator.scale()<=18;}
bool add(ForecastRatio &sum,const ForecastRatio &value){
    if(sum.denominator.compare(value.denominator)==0)return sum.numerator.add(value.numerator,&sum.numerator);
    ExactDecimal a,b,den;
    if(!sum.numerator.multiply(value.denominator,&a)||!value.numerator.multiply(sum.denominator,&b)||!a.add(b,&a)||!sum.denominator.multiply(value.denominator,&den))return false;
    sum={std::move(a),std::move(den)};return true;
}
}
bool sameGameProfitBinding(const GameProfitBinding &a,const GameProfitBinding &b){return a.gameKey==b.gameKey&&a.profileKey==b.profileKey&&a.episodeKey==b.episodeKey&&
    sameCreditSourceContract(a.production,b.production)&&a.power.basis==b.power.basis&&a.power.origin==b.power.origin&&a.power.scope==b.power.scope&&a.power.adapter==b.power.adapter;}
std::shared_ptr<const GameProfitWindow> buildGameProfitWindow(const GameProfitInput &in,S *status,const std::atomic<bool> *cancel){
    auto fail=[&](S s)->std::shared_ptr<const GameProfitWindow>{if(status)*status=s;return {};};
    if(cancel&&cancel->load())return fail(S::Cancelled);
    if(in.qualification!=ForecastQualification::SyntheticFixture||in.binding.production.qualification!=CreditQualification::SyntheticFixture||
        in.binding.production.scope!=CreditScope::Worker||in.binding.power.origin!=PowerOrigin::SyntheticFixture||in.binding.power.basis!=PowerBasis::GpuBoard)return fail(S::Unsupported);
    try{
        if(!key(in.binding.gameKey)||!key(in.binding.profileKey)||!key(in.binding.episodeKey)||!key(in.binding.power.scope)||!key(in.binding.power.adapter)||
            !validCreditSourceContract(in.binding.production)||!in.generation||!in.revision)return fail(S::Invalid);
        if(!instant(in.beginUtcMs)||!instant(in.endUtcMs)||in.endUtcMs<=in.beginUtcMs||in.endMonoMs<=in.beginMonoMs||
            static_cast<uint64_t>(in.endUtcMs-in.beginUtcMs)!=in.endMonoMs-in.beginMonoMs)return fail(S::Clock);
        if(in.endMonoMs-in.beginMonoMs!=gameProfitWindowMs)return fail(S::Coverage);
        if(in.intervals.size()>4096)return fail(S::Capacity);
        ForecastRatio revenue{n(0),n(1)},cost{n(0),n(1)};uint64_t cursor=in.beginMonoMs;
        for(const auto &i:in.intervals){if(cancel&&cancel->load())return fail(S::Cancelled);
            if(i.beginMonoMs!=cursor||i.endMonoMs<=i.beginMonoMs||i.endMonoMs>in.endMonoMs||!i.expectedRevenueUsd||!i.estimatedCostUsd)return fail(S::Coverage);
            if(!key(i.modelReceipt)||!ratio(*i.expectedRevenueUsd)||!ratio(*i.estimatedCostUsd)||i.expectedRevenueUsd->numerator.negative())return fail(S::Invalid);
            if(!add(revenue,*i.expectedRevenueUsd)||!add(cost,*i.estimatedCostUsd))return fail(S::Capacity);
            cursor=i.endMonoMs;
        }
        if(cursor!=in.endMonoMs)return fail(S::Coverage);
        ForecastRatio net;ExactDecimal amount;
        if(!revenue.numerator.multiply(cost.denominator,&net.numerator)||!cost.numerator.multiply(revenue.denominator,&amount)||
            !net.numerator.subtract(amount,&net.numerator)||!revenue.denominator.multiply(cost.denominator,&net.denominator))return fail(S::Capacity);
        auto mean=net;if(!mean.numerator.multiply(n(1000),&mean.numerator)||!mean.denominator.multiply(n(gameProfitWindowMs),&mean.denominator))return fail(S::Capacity);
        auto result=std::shared_ptr<const GameProfitWindow>(new GameProfitWindow(in,std::move(revenue),std::move(cost),std::move(net),std::move(mean)));
        if(cancel&&cancel->load())return fail(S::Cancelled);
        if(status)*status=S::Ok;
        return result;
    }catch(...){return fail(S::Capacity);}
}
} // namespace om
