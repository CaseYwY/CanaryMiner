// CPU-only scheduling-delay fixture: use the actual test helper and protocol, never a production mode or environment hook.
#define CANARY_ENERGY_HISTORY_WORKER_TESTING 1
#define main energyHistoryFixtureMain
#include "../src/product/energy_history_worker.cpp"
#undef main
int main(int argc,char **argv){
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    return energyHistoryFixtureMain(argc,argv);
}
