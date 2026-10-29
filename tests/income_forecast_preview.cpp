// CPU fixture preview through the ordinary terminal renderer. Only its own temporary synthetic ledger is opened.
#include "income_forecast_fixture.h"
#include "product/dashboard.h"
#include <chrono>
#include <iostream>
using namespace om;
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const std::string mode=argv[1];unsigned width=0;
    for(const char *c=argv[2];*c;++c){if(*c<'0'||*c>'9'||width>180)return 2;width=width*10+static_cast<unsigned>(*c-'0');}
    if(width<40||width>180)return 2;
    if(mode!="complete"&&mode!="partial"&&mode!="missing-fx"&&mode!="no-electricity"&&mode!="missing-work"&&mode!="stale")return 2;
    const auto root=std::filesystem::temp_directory_path()/("canary-income-preview-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(root))return 3;
    {
        income_fixture::Fixture f(root/"synthetic.db");auto &in=f.input;
        if(mode=="partial"){auto &t=in.today.totals;t.completeness=CreditCompleteness::Partial;t.completePostedNet.reset();t.coveredMs=0;}
        if(mode=="missing-fx")in.fxQuote.reset();
        if(mode=="no-electricity")in.electricity.configured=false;
        if(mode=="missing-work")in.remaining.work.intervals.clear();
        if(mode=="stale")in.nowMonoMs+=120001;
        DashModel model;model.version="SYNTHETIC INCOME FORECAST";model.connection="Stopped";model.finances=incomeForecastDashboardView(incomeForecast(in));
        DashView view;view.columns=static_cast<int>(width);view.rows=0;view.color=ColorLevel::None;
        std::cout<<renderDashboard(model,view);
    }
    std::filesystem::remove_all(root);return 0;
}
