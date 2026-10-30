// Bounded readonly process-name observations. Private local IPC, never a game-experience or mining permit.
#pragma once
#include "game_guard.h"
#include "local_task_process.h"
namespace om {
enum class GameProcessAdapter { WindowsSessionImage, LinuxUidNativeImage };
struct GameProcessTarget {
    std::string gameKey,executable; // explicit ASCII basename, not a path, launcher, substring or command line
    GameProcessAdapter adapter=GameProcessAdapter::WindowsSessionImage;
};
enum class GameProbeStatus { Ok, Disabled, Unsupported, Incomplete, Capacity, Clock, Invalid, Entropy };
struct GameProbeRequest {
    bool enabled=false;
    uint64_t generation=0,revision=0;
    GameProcessTarget target;
    std::string privateScope; // empty only for the first flight; the helper provisions OS entropy
    GameGuardTime issued;
    unsigned scanBudgetMs=250,maxProcesses=32768;
};
struct GameProbeReply {
    GameProbeStatus status=GameProbeStatus::Incomplete;
    std::string privateScope,instanceSet; // salted digest of sorted PID + creation identities; never raw process details
    unsigned matches=0;
    GameGuardTime began,finished; // observation age starts at began, not receipt/finished
};
GameGuardTime gameDetectionTime();
bool validGameProcessTarget(const GameProcessTarget &target);
bool validGameProbeRequest(const GameProbeRequest &request);
bool encodeGameProbeRequest(const GameProbeRequest &,uint64_t parentPid,std::string *wire);
bool decodeGameProbeRequest(const std::string &wire,GameProbeRequest *,uint64_t *parentPid);
bool encodeGameProbeReply(const GameProbeRequest &,const GameProbeReply &,std::string *wire);
bool decodeGameProbeReply(const GameProbeRequest &,const std::string &wire,GameProbeReply *);
// Helper-side only: native inspection and entropy can block; caller must use the isolated client.
GameProbeReply executeGameProbe(const GameProbeRequest &request);
GameDetection gameProbeDetection(const GameProbeRequest &request,const GameProbeReply &reply);
struct GameProbeResult {LocalTaskStatus transport=LocalTaskStatus::Unavailable;std::optional<GameProbeReply> reply;};
class GameProbeClient {
   public:
    explicit GameProbeClient(std::filesystem::path helper):process_(std::move(helper)){}
    GameProbeResult transact(const GameProbeRequest &,const std::atomic<bool> &cancel,unsigned timeoutMs);
   private:
    LocalTaskProcess process_;
};
} // namespace om
