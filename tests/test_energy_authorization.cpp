#include "product/energy_authorization_owner.h"
#include "product/energy_history_model.h"
#include "product/hash.h"
#include "product/local_store_lease.h"
#include "product/settings.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
using namespace om;
namespace {
using A=EnergyAuthorizationAction;
using S=EnergyAuthorizationStatus;
using E=EnergyEnrollment;
using O=EnergyAuthorizationOwnerState;
constexpr int64_t T=1800000000000LL;
const std::vector<std::string> devices={"GPU-aaaaaaaa-0000-0000-0000-000000000001","GPU-bbbbbbbb-0000-0000-0000-000000000002"};
std::atomic<bool> cancel{false};
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const std::filesystem::path &p,const std::string &s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<s;}
template<class P>bool until(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(7);while(std::chrono::steady_clock::now()<end){if(predicate())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return predicate();}
bool idle(EnergyAuthorizationOwner &o){return until([&]{const auto s=o.snapshot();return !s->pending&&s->state!=O::Busy;});}
EnergyAuthorizationRequest request(const std::filesystem::path &file,A action=A::Load,const std::string &token={}){
    EnergyAuthorizationRequest r;r.generation=1;r.ticket=1;r.file=file;r.action=action;r.expected=token;r.nowUtcMs=T+100000;
    if(action==A::Enroll||action==A::Provision)r.devices=devices;
    if(action==A::Enroll)r.limits.compaction=EnergyCompactionPolicy{2,32768};
    return r;
}
EnergyAuthorizationReply call(EnergyAuthorizationProcessClient &c,const EnergyAuthorizationRequest &r){
    auto result=c.transact(r,cancel,5000);CHECK(result.transport==LocalTaskStatus::Ok&&result.reply.has_value());
    return result.reply?*result.reply:EnergyAuthorizationReply{};
}
EnergyAuthorizationReply enroll(EnergyAuthorizationProcessClient &c,const std::filesystem::path &f){auto s=call(c,request(f,A::Enroll,"absent"));CHECK(s.status==S::Ok&&s.document&&s.document->enrollment==E::Ready);return s;}
EnergyAuthorizationReply enable(EnergyAuthorizationProcessClient &c,const std::filesystem::path &f,const EnergyAuthorizationReply &s){
    auto r=request(f,A::Save,s.token);r.policy=s.document->policy;r.limits=s.document->limits;r.policy.persistenceAuthorized=true;
    auto out=call(c,r);CHECK(out.status==S::Ok);return out;
}
EnergyAuthorizationCommand command(A a,const EnergyAuthorizationDocument *d=nullptr){EnergyAuthorizationCommand c;c.action=a;
    if(a==A::Enroll||a==A::Provision)c.devices=devices;
    if(a==A::Save&&d){c.policy=d->policy;c.limits=d->limits;}return c;
}
EnergyHistoryAdmission submit(EnergyAuthorizationOwner &o,const EnergyAuthorizationCommand &c){const auto s=o.snapshot();return o.submit(s->generation,s->revision,c);}
void layout(const EnergyAuthorizationPanel &p,const EnergyAuthorizationAccess &a){for(int width:{20,40,60,80,120})for(int rows:{12,16,24,40}){
    const auto rendered=p.render(width,rows,ColorLevel::None,a);size_t count=0,line=0;
    for(char c:rendered){if(c=='\n'){CHECK(line<static_cast<size_t>(width));line=0;++count;}else ++line;}CHECK(count<=static_cast<size_t>(rows));}}
int probe(int argc,char **argv){
    if(argc!=6)return 2;
    EnergyAuthorizationProcessClient c(std::filesystem::absolute(argv[2]));const auto file=std::filesystem::absolute(argv[3]);const std::string action=argv[4];
    auto load=c.transact(request(file),cancel,5000);if(!load.reply)return 3;
    A a=A::Load;
    if(action=="enroll")a=A::Enroll;else if(action=="enable")a=A::Save;else if(action=="provision")a=A::Provision;else if(action=="inspect")a=A::Inspect;else if(action=="confirm")a=A::Confirm;else if(action!="load")return 2;
    auto r=request(file,a,a==A::Load?"":load.reply->token);
    if(a==A::Save){if(!load.reply->document)return 3;r.policy=load.reply->document->policy;r.policy.persistenceAuthorized=true;r.limits=load.reply->document->limits;}
    r.nowUtcMs=std::stoll(argv[5]);const auto result=c.transact(r,cancel,5000);
    if(!result.reply){std::printf("unknown=%u\n",result.writeOutcomeUnknown?1:0);return 0;}
    const auto &s=*result.reply;std::printf("status=%u enrollment=%u issued=%u empty=%u\n",static_cast<unsigned>(s.status),s.document?static_cast<unsigned>(s.document->enrollment):9,s.sessionIssued?1:0,s.emptyStoreVerified?1:0);return 0;
}
}
int main(int argc,char **argv){
    if(argc>1&&std::string(argv[1])=="--probe")return probe(argc,argv);
    CHECK(argc==4);if(argc!=4)return finishTests("energy authorization");
    const auto helper=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]),history=std::filesystem::absolute(argv[3]);
    const auto root=std::filesystem::path(makeScratchDir("energy-authorization"));EnergyAuthorizationProcessClient c(helper),f(fixture);
    const auto file=root/"preferences.json";const auto initial=call(c,request(file));CHECK(initial.status==S::Missing&&initial.token=="absent"&&!initial.document->policy.persistenceAuthorized);
    CHECK(!std::filesystem::exists(file)&&!std::filesystem::exists(file.string()+".lock"));
    auto r=request(file,A::Save,"absent");r.policy.persistenceAuthorized=true;CHECK(call(c,r).status==S::Conflict&&!std::filesystem::exists(file));
    r.policy.persistenceAuthorized=false;r.policy.selectionMs=2000;r.policy.maximumGapMs=3000;
    const auto saved=call(c,r);CHECK(saved.status==S::Ok&&saved.document->revision==1&&saved.document->enrollment==E::None);
    CHECK(call(c,r).status==S::Conflict);const auto savedBytes=bytes(file);
    r=request(file,A::Enroll,saved.token);const auto registered=call(c,r);CHECK(registered.status==S::Ok&&registered.emptyStoreVerified);
    CHECK(registered.document->revision==3&&!registered.document->policy.persistenceAuthorized&&registered.document->lastSessionId.empty());
    const auto db=energyAuthorizationStorePath(file,*registered.document);CHECK(std::filesystem::exists(db)&&std::filesystem::exists(db.string()+".energy-recovery.json"));
    CHECK(bytes(file).find(devices[0])==std::string::npos&&bytes(file).find(devices[1])==std::string::npos);
    CHECK(call(c,request(file,A::Enroll,registered.token)).status==S::Conflict);
    CHECK(call(c,request(file,A::Provision,registered.token)).status==S::Disabled);
    const auto enabled=enable(c,file,registered);r=request(file,A::Save,enabled.token);r.policy=enabled.document->policy;r.limits=enabled.document->limits;r.limits.maxSessions--;
    const auto beforeLimitChange=bytes(file);CHECK(call(c,r).status==S::Conflict&&bytes(file)==beforeLimitChange);
    r=request(file,A::Provision,enabled.token);r.devices={devices[0]};CHECK(call(c,r).status==S::Selection);
    r.devices={devices[1],devices[0]};auto issued=call(c,r);CHECK(issued.status==S::Ok&&issued.sessionIssued);
    const auto firstId=issued.document->lastSessionId;CHECK(firstId.size()==40&&firstId.rfind("session.",0)==0);
    auto next=call(c,request(file,A::Provision,issued.token));CHECK(next.status==S::Ok&&next.sessionIssued&&next.document->lastSessionId!=firstId);
    CHECK(next.document->privateNamespace==registered.document->privateNamespace&&energy_history_detail::bindingText(next.document->binding)==energy_history_detail::bindingText(registered.document->binding));
    // Canonical schemas reject corruption, ambiguous numbers, unknown fields and changed immutable provenance.
    const auto good=bytes(file);EnergyAuthorizationDocument d;CHECK(decodeEnergyAuthorizationDocument(good,&d));std::string encoded;
    CHECK(encodeEnergyAuthorizationDocument(d,&encoded)&&encoded==good);CHECK(!decodeEnergyAuthorizationDocument(good+" ",&d));
    for(unsigned n=0;n<15;++n){auto bad=*next.document;
        if(n==0)bad.revision=0;
        if(n==1)bad.changedUtcMs=0;
        if(n==2)bad.enrollment=static_cast<E>(99);
        if(n==3)bad.privateNamespace="ordinal";
        if(n==4)bad.binding.evidence.scope="selection."+std::string(64,'0');
        if(n==5)bad.binding.channels[0]="channel."+std::string(64,'0');
        if(n==6)bad.binding.evidence.basis=PowerBasis::WholeSystemWall;
        if(n==7)bad.binding.evidence.adapter="fixture.capture";
        if(n==8)bad.binding.historyBeginUtcMs=bad.changedUtcMs+1;
        if(n==9)bad.lastSessionId="session-1";
        if(n==10)bad.policy.selectionMs=499;
        if(n==11)bad.policy.maximumGapMs=1;
        if(n==12)bad.policy.flushMs=0;
        if(n==13)bad.policy.bufferedReadings=129;
        if(n==14)bad.limits.compaction->maxSegments=32769;
        CHECK(!encodeEnergyAuthorizationDocument(bad,&encoded));}
    r=request(file,A::Provision,next.token);CHECK(encodeEnergyAuthorizationRequest(r,&encoded));EnergyAuthorizationRequest decoded;
    CHECK(decodeEnergyAuthorizationRequest(encoded,&decoded));CHECK(!decodeEnergyAuthorizationRequest(encoded+"x",&decoded));
    for(unsigned n=0;n<12;++n){auto bad=r;
        if(n==0)bad.generation=0;
        if(n==1)bad.ticket=0;
        if(n==2)bad.file="relative.json";
        if(n==3)bad.expected="";
        if(n==4)bad.nowUtcMs=0;
        if(n==5)bad.devices={};
        if(n==6)bad.devices={devices[0],devices[0]};
        if(n==7)bad.devices={"0"};
        if(n==8)bad.policy.persistenceAuthorized=true;
        if(n==9)bad.limits.maxSessions=1;
        if(n==10)bad.action=static_cast<A>(99);
        if(n==11)bad.devices.resize(65,devices[0]);
        CHECK(!encodeEnergyAuthorizationRequest(bad,&encoded));}
    auto reply=call(c,request(file));r=request(file);CHECK(encodeEnergyAuthorizationReply(r,reply,&encoded));EnergyAuthorizationReply replyDecoded;
    CHECK(decodeEnergyAuthorizationReply(r,encoded,&replyDecoded));r.ticket++;CHECK(!decodeEnergyAuthorizationReply(r,encoded,&replyDecoded));
    r=request(file);r.nowUtcMs++;CHECK(!decodeEnergyAuthorizationReply(r,encoded,&replyDecoded));
    r=request(file);reply.sessionIssued=true;CHECK(!encodeEnergyAuthorizationReply(r,reply,&encoded));reply.sessionIssued=false;reply.emptyStoreVerified=true;CHECK(!encodeEnergyAuthorizationReply(r,reply,&encoded));
    // Missing/corrupt documents are not overwritten; policy saves never repair a history store.
    const auto corrupt=root/"corrupt.json";put(corrupt,"{}");CHECK(call(c,request(corrupt)).status==S::Storage);
    CHECK(call(c,request(corrupt,A::Enroll,"absent")).status==S::Storage&&bytes(corrupt)=="{}");
    put(corrupt,std::string(energyAuthorizationLimit+1,' '));CHECK(call(c,request(corrupt)).status==S::Storage);
    r=request(file);r.nowUtcMs=T;CHECK(call(c,r).status==S::Clock&&bytes(file)==good);
    {LocalStoreLease lease;CHECK(lease.acquire(file.string()+".lock"));r=request(file,A::Save,next.token);r.policy=next.document->policy;r.limits=next.document->limits;
        CHECK(call(c,r).status==S::Storage&&bytes(file)==good);}
    // Two independent processes using one CAS can commit at most one revision.
    r=request(file,A::Save,next.token);r.policy=next.document->policy;r.policy.flushMs=100;r.limits=next.document->limits;
    EnergyAuthorizationProcessResult left,right;std::atomic<bool> go{false};
    std::thread a([&]{EnergyAuthorizationProcessClient p(helper);while(!go)std::this_thread::yield();left=p.transact(r,cancel,5000);});
    std::thread b([&]{EnergyAuthorizationProcessClient p(helper);while(!go)std::this_thread::yield();right=p.transact(r,cancel,5000);});go=true;a.join();b.join();
    CHECK(left.reply&&right.reply);CHECK((left.reply->status==S::Ok)+(right.reply->status==S::Ok)==1);
    CHECK(call(c,request(file)).document->revision==next.document->revision+1);
    // Maximum native selected-set envelope; no raw UUIDs persisted and no compaction implicitly enabled.
    auto maximum=request(root/"maximum.json",A::Enroll,"absent");maximum.limits={};maximum.devices.clear();
    for(unsigned n=0;n<64;++n){char uuid[64];std::snprintf(uuid,sizeof(uuid),"GPU-00000000-0000-0000-0000-%012u",n);maximum.devices.emplace_back(uuid);}
    CHECK(encodeEnergyAuthorizationRequest(maximum,&encoded)&&encoded.size()<energyAuthorizationLimit);
    const auto maxReply=call(c,maximum);CHECK(maxReply.status==S::Ok&&maxReply.document->binding.channels.size()==64&&!maxReply.document->limits.compaction);
    CHECK(encodeEnergyAuthorizationReply(maximum,maxReply,&encoded)&&encoded.size()<energyAuthorizationLimit);
    // Helper death on either side of database creation leaves the same immutable Pending reservation.
    for(const auto *name:{"fixture-exit-reserved.json","fixture-exit-created.json"}){
        const auto path=root/name;auto result=f.transact(request(path,A::Enroll,"absent"),cancel,5000);CHECK(result.writeOutcomeUnknown&&!result.reply);
        auto pending=call(c,request(path));CHECK(pending.document&&pending.document->enrollment==E::Pending&&!pending.document->policy.persistenceAuthorized);
        const auto original=bytes(path);const auto store=energyAuthorizationStorePath(path,*pending.document);
        CHECK(call(c,request(path,A::Enroll,pending.token)).status==S::Conflict&&bytes(path)==original);
        const bool created=std::string(name)=="fixture-exit-created.json";CHECK(std::filesystem::exists(store)==created);
        const auto inspection=call(c,request(path,A::Inspect,pending.token));CHECK(inspection.status==(created?S::Ok:S::NeedsReview)&&bytes(path)==original);
        const auto confirmation=call(c,request(path,A::Confirm,pending.token));CHECK(confirmation.status==(created?S::Ok:S::NeedsReview));
        CHECK(std::filesystem::exists(store)==created);if(created)CHECK(confirmation.document->enrollment==E::Ready&&!confirmation.document->policy.persistenceAuthorized);
    }
    CHECK(call(f,request(root/"fixture-entropy-fail.json",A::Enroll,"absent")).status==S::Entropy);CHECK(!std::filesystem::exists(root/"fixture-entropy-fail.json"));
    // Even a matching binding cannot confirm a Pending store that has acquired observations outside enrollment.
    {const auto folder=root/"nonempty";std::filesystem::create_directories(folder);const auto path=folder/"fixture-exit-created.json";
        const auto interrupted=f.transact(request(path,A::Enroll,"absent"),cancel,5000);CHECK(interrupted.writeOutcomeUnknown);
        const auto pending=call(c,request(path));const auto &doc=*pending.document;const auto original=bytes(path);
        EnergyHistoryRequest q;q.generation=q.ticket=1;q.file=energyAuthorizationStorePath(path,doc);q.binding=doc.binding;q.limits=doc.limits;q.nowUtcMs=T+100000;q.recovery=EnergyRecoveryMode::Submit;
        EnergyHistoryBatch batch;batch.sequence=1;batch.id="energy.1";batch.sessionId="session.00000000000000000000000000000001";
        batch.start=EnergyHistorySession{batch.sessionId,q.nowUtcMs,0,5000};batch.observedAtMs=q.nowUtcMs;batch.readings={{0,q.nowUtcMs,0,0,100000}};q.batch=batch;
        CHECK(executeEnergyHistoryRequest(q).applied==EnergyHistoryStatus::Ok);
        CHECK(call(c,request(path,A::Inspect,pending.token)).status==S::NeedsReview);CHECK(call(c,request(path,A::Confirm,pending.token)).status==S::NeedsReview&&bytes(path)==original);}
    // A valid authorization document does not permit implicit recreation of a missing history/journal.
    {const auto path=root/"missing-store.json";const auto e=enable(c,path,enroll(c,path));const auto store=energyAuthorizationStorePath(path,*e.document);
        const auto original=bytes(path);std::filesystem::remove(store.string()+".energy-recovery.json");
        CHECK(call(c,request(path,A::Provision,e.token)).status==S::NeedsReview&&bytes(path)==original);
        CHECK(!std::filesystem::exists(store.string()+".energy-recovery.json"));}
    // Single-use async provisioning freezes its plan and hands it to the existing capture bridge.
    const auto ownerFile=root/"owner.json";auto registeredOwner=enroll(c,ownerFile);registeredOwner=enable(c,ownerFile,registeredOwner);
    std::atomic<int64_t> clock{T+100000};EnergyAuthorizationOwner owner(helper,ownerFile,false,5000,[&]{return clock.load();});CHECK(owner.start(7)&&idle(owner));
    auto s=owner.snapshot();CHECK(!owner.takePlan(7,s->revision));CHECK(owner.submit(6,s->revision,command(A::Provision))==EnergyHistoryAdmission::StaleGeneration);
    CHECK(submit(owner,command(A::Provision))==EnergyHistoryAdmission::Queued&&idle(owner));s=owner.snapshot();CHECK(s->planAvailable&&s->provisionAttempted);
    CHECK(!owner.takePlan(7,s->revision-1));auto plan=owner.takePlan(7,s->revision);CHECK(plan&&plan->plan.mode==EnergyHistoryOpen::Existing);
    CHECK(!owner.takePlan(7,owner.snapshot()->revision));CHECK(submit(owner,command(A::Provision))==EnergyHistoryAdmission::NotReady);
    const auto frozen=plan->plan;auto disable=command(A::Save,&*owner.snapshot()->reply->document);disable.policy.persistenceAuthorized=false;
    CHECK(submit(owner,disable)==EnergyHistoryAdmission::Queued&&idle(owner));CHECK(frozen.policy.persistenceAuthorized&&!owner.snapshot()->reply->document->policy.persistenceAuthorized);
    auto capture=makeEnergyCapture(history,plan->file,plan->plan,5000,[&]{return clock.load()+10000;});CHECK(capture->start(1,devices));
    CHECK(until([&]{capture->tick(T+100000,0);return capture->snapshot()->state==EnergyCaptureState::Collecting;}));
    CHECK(capture->submit(1,{0,T+100000,0,100000})==EnergyCaptureAdmission::Buffered);
    CHECK(capture->submit(1,{0,T+101000,1000,0})==EnergyCaptureAdmission::Buffered);
    CHECK(until([&]{capture->tick(T+102000,2000);return capture->snapshot()->owner->reply&&capture->snapshot()->owner->reply->checkpoint.sequence==1;}));capture->stop();
    owner.requestStop();CHECK(!owner.takePlan(7,owner.snapshot()->revision));owner.stop();CHECK(!owner.start(8));
    {EnergyAuthorizationOwner stopped(helper,root/"never.json");stopped.requestStop();CHECK(!stopped.start(1));CHECK(!std::filesystem::exists(root/"never.json"));}
    {EnergyAuthorizationOwner disabled(helper,file,true,5000,[&]{return clock.load();});CHECK(disabled.start(1)&&idle(disabled));const auto before=bytes(file);
        CHECK(submit(disabled,command(A::Provision))==EnergyHistoryAdmission::NotReady&&bytes(file)==before);disabled.stop();}
    // Review identity is atomically bound. A concurrent duplicate admission never reaches a second helper.
    {EnergyAuthorizationOwner o(helper,file,false,5000,[&]{return clock.load();});CHECK(o.start(1)&&idle(o));const auto snap=o.snapshot();
        EnergyHistoryAdmission x,y;auto edit=command(A::Save,&*snap->reply->document);edit.policy.flushMs=200;
        std::thread one([&]{x=o.submit(1,snap->revision,edit);});std::thread two([&]{y=o.submit(1,snap->revision,edit);});one.join();two.join();
        CHECK((x==EnergyHistoryAdmission::Queued)+(y==EnergyHistoryAdmission::Queued)==1);CHECK(idle(o));
        clock=T;CHECK(submit(o,command(A::Load))==EnergyHistoryAdmission::Invalid);CHECK(o.snapshot()->state==O::Faulted);o.stop();clock=T+100000;}
    // Lost acknowledgement cannot issue a usable session, and Load cannot turn a recorded lastSessionId into a plan.
    {const auto path=root/"fixture-exit-issued.json";auto initialDoc=enroll(c,path);enable(c,path,initialDoc);
        EnergyAuthorizationOwner o(fixture,path,false,5000,[&]{return clock.load();});CHECK(o.start(1)&&idle(o));CHECK(submit(o,command(A::Provision))==EnergyHistoryAdmission::Queued&&idle(o));
        s=o.snapshot();CHECK(s->state==O::NeedsReview&&s->uncertain&&!s->planAvailable);const auto original=s->uncertain;
        CHECK(o.acknowledgeInspection(1,s->revision)==EnergyHistoryAdmission::NotReady);CHECK(submit(o,command(A::Provision))==EnergyHistoryAdmission::NotReady);
        CHECK(submit(o,command(A::Load))==EnergyHistoryAdmission::Queued&&idle(o));s=o.snapshot();CHECK(s->uncertain==original&&s->inspectionAvailable&&!s->planAvailable);
        CHECK(o.acknowledgeInspection(1,s->revision-1)==EnergyHistoryAdmission::ReviewChanged);CHECK(o.acknowledgeInspection(1,s->revision)==EnergyHistoryAdmission::Queued);
        CHECK(!o.takePlan(1,o.snapshot()->revision)&&submit(o,command(A::Provision))==EnergyHistoryAdmission::NotReady);const auto lostId=o.snapshot()->reply->document->lastSessionId;o.stop();
        EnergyAuthorizationOwner fresh(helper,path,false,5000,[&]{return clock.load();});CHECK(fresh.start(1)&&idle(fresh));CHECK(!fresh.takePlan(1,fresh.snapshot()->revision));
        CHECK(submit(fresh,command(A::Provision))==EnergyHistoryAdmission::Queued&&idle(fresh));const auto freshPlan=fresh.takePlan(1,fresh.snapshot()->revision);CHECK(freshPlan&&freshPlan->plan.sessionId!=lostId);fresh.stop();}
    // Stop stays memory-only while a helper is sleeping after a write. No late response revives a plan or owner.
    {const auto path=root/"fixture-stall-after.json";EnergyAuthorizationOwner o(fixture,path,false,5000,[&]{return clock.load();});CHECK(o.start(1)&&idle(o));
        CHECK(submit(o,command(A::Save))==EnergyHistoryAdmission::Queued);CHECK(until([&]{return std::filesystem::exists(path.string()+".fixture-ready");}));
        const auto start=std::chrono::steady_clock::now();o.requestStop();const auto revision=o.snapshot()->revision;
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));CHECK(o.snapshot()->state==O::Stopped&&o.snapshot()->uncertain&&!o.snapshot()->planAvailable);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));CHECK(o.snapshot()->revision==revision);o.stop();CHECK(!o.start(2));
        CHECK(call(c,request(path)).status==S::Ok);}
    {const auto path=root/"fixture-stall-before.json";auto result=f.transact(request(path,A::Save,"absent"),cancel,100);
        CHECK(result.transport==LocalTaskStatus::Timeout&&result.writeOutcomeUnknown&&!std::filesystem::exists(path));}
    {const auto path=root/"fixture-bad-ack.json";auto result=f.transact(request(path,A::Save,"absent"),cancel,5000);
        CHECK(result.writeOutcomeUnknown&&!result.reply&&call(c,request(path)).status==S::Ok);}
    // The native panel edits only drafts, separates queued/confirmed outcomes, and binds review to the owner revision.
    {const auto path=root/"panel.json";EnergyAuthorizationOwner o(helper,path,false,5000,[&]{return clock.load();});CHECK(o.start(1)&&idle(o));
        EnergyAuthorizationAccess access;access.snapshot=[&]{return o.snapshot();};access.submit=[&](uint64_t g,uint64_t rev,const EnergyAuthorizationCommand &cmd){return o.submit(g,rev,cmd);};
        access.acknowledge=[&](uint64_t g,uint64_t rev){return o.acknowledgeInspection(g,rev);};access.selectedDevices=devices;
        EnergyAuthorizationPanel p;p.open(o.snapshot());layout(p,access);CHECK(p.render(120,80,ColorLevel::None,access).find(devices[0])==std::string::npos);
        p.field=1;p.key('\r',access);p.key('2',access);p.key('0',access);p.key('0',access);p.key('0',access);CHECK(p.key('q',access)==EnergyAuthorizationPanelResult::Quit);
        p.key('\r',access);CHECK(p.dirty()&&p.draftPolicy.selectionMs==2000&&!std::filesystem::exists(path));p.key('v',access);layout(p,access);
        p.key(27,access);CHECK(!std::filesystem::exists(path));p.key(27,access);CHECK(p.page==EnergyAuthorizationPage::Leave);p.key(27,access);p.key('d',access);CHECK(!p.dirty());
        p.key('e',access);CHECK(p.page==EnergyAuthorizationPage::Review);p.key('\r',access);CHECK(!std::filesystem::exists(path));
        p.key('i',access);CHECK(p.render(120,80,ColorLevel::None,access).find(devices[0])!=std::string::npos);p.key(' ',access);p.key('\r',access);
        CHECK(p.message.find("QUEUED")!=std::string::npos);CHECK(idle(o));p.update(o.snapshot());CHECK(p.message.find("CONFIRMED")!=std::string::npos&&!p.dirty());
        const auto original=bytes(path);CHECK(!o.snapshot()->reply->document->policy.persistenceAuthorized&&!o.snapshot()->planAvailable);
        p.field=6;p.key('\r',access);CHECK(p.page==EnergyAuthorizationPage::Browse&&p.message.find("fixed")!=std::string::npos);
        p.field=0;p.key(' ',access);CHECK(p.draftPolicy.persistenceAuthorized);p.key('v',access);p.key('\r',access);CHECK(idle(o));p.update(o.snapshot());
        CHECK(o.snapshot()->reply->document->policy.persistenceAuthorized&&!o.snapshot()->planAvailable);
        p.reveal=false;CHECK(p.render(120,80,ColorLevel::None,access).find(o.snapshot()->reply->document->privateNamespace)==std::string::npos);layout(p,access);
        // An external owner command invalidates a pending review instead of overwriting its newly saved values.
        p.key(' ',access);p.key('v',access);CHECK(p.page==EnergyAuthorizationPage::Review);
        auto edit=command(A::Save,&*o.snapshot()->reply->document);edit.policy.flushMs=4321;CHECK(submit(o,edit)==EnergyHistoryAdmission::Queued&&idle(o));p.update(o.snapshot());
        CHECK(p.page==EnergyAuthorizationPage::Browse&&p.message.find("cancelled")!=std::string::npos);p.key('v',access);CHECK(p.page==EnergyAuthorizationPage::Browse);
        p.key('d',access);CHECK(!p.dirty()&&p.draftPolicy.flushMs==4321&&bytes(path)!=original);
        p.key('i',access);p.key(kKeyPageDown,access);CHECK(p.manualScroll);p.key(kKeyDown,access);CHECK(!p.manualScroll);o.stop();
        SettingsPanel settings;settings.open({});settings.group=2;settings.field=3;settings.groupsFocused=false;settings.key('\r',{});CHECK(settings.energyAuthorization.visible);
        CHECK(settings.render(100,40,ColorLevel::None).find("UNAVAILABLE")!=std::string::npos);CHECK(settings.key(3,{})==SettingsResult::Quit);
        settings.open({});settings.group=8;settings.field=4;settings.groupsFocused=false;settings.key('\r',{});CHECK(settings.energyAuthorization.visible);}
    // Missing helper and pre-cancellation never claim a write was attempted.
    {EnergyAuthorizationProcessClient missing(root/"not-installed");auto result=missing.transact(request(root/"missing.json",A::Save,"absent"),cancel,100);CHECK(!result.writeOutcomeUnknown&&!result.reply);}
    cancel=true;const auto cancelled=c.transact(request(root/"cancelled.json",A::Save,"absent"),cancel,5000);CHECK(cancelled.transport==LocalTaskStatus::Cancelled&&!cancelled.writeOutcomeUnknown);cancel=false;
    CHECK(savedBytes!=good);std::filesystem::remove_all(root);return finishTests("energy authorization");
}
