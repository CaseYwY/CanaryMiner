// Existing-only, parent-bound credit reads for a forecast. No source write, provider fetch or replay command.
#pragma once
#include "credit_process.h"
namespace om {
constexpr size_t forecastQueryLimit=creditRecoveryRequestLimit;
bool validForecastQuery(const CreditProcessRequest &request);
bool encodeForecastQuery(const CreditProcessRequest &request,uint64_t parentPid,std::string *wire);
bool decodeForecastQuery(const std::string &wire,CreditProcessRequest *request,uint64_t *parentPid);
class ForecastQueryProcessClient {
   public:
    explicit ForecastQueryProcessClient(std::filesystem::path helper):process_(std::move(helper)){}
    CreditProcessResult transact(const CreditProcessRequest &request,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
} // namespace om
