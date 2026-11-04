#include "product/pool_tls_rpc.h"
#include "product_test_util.h"
using namespace om;
namespace {
using O=PoolTlsRpcOperation;using T=PearlPoolTransferStatus;using S=PearlTlsState;using E=PearlTlsError;using I=PearlPoolInterest;
PoolTlsRpcCommand open(){PoolTlsCheckRequest r{true,"localhost",1,1000,{},{}};PoolTlsRpcCommand c{1,1000,2000,O::Open,0,I::None,0,{}};
    CHECK(encodePoolTlsCheckRequest(r,1,&c.payload));return c;}
PoolTlsRpcReply reply(const PoolTlsRpcCommand &c){return {c.sequence,{T::Progress,0},S::VerifiedOpen,E::None,{}};}
std::string body(const std::string &wire){return wire.substr(wire.find('\n')+1);}
void framing(){
    auto c=open();std::string wire;CHECK(encodePoolTlsRpcCommand(c,&wire));
    for(std::size_t chunk:{1u,7u,4096u}){PoolTlsRpcFramer framer;unsigned calls=0;
        for(std::size_t at=0;at<wire.size();at+=chunk)CHECK(framer.feed(std::string_view(wire).substr(at,chunk),[&](std::string_view frame){
            PoolTlsRpcCommand got;CHECK(decodePoolTlsRpcCommand(frame,&got)&&got.payload==c.payload&&got.sequence==1);++calls;return true;}));
        CHECK(framer.complete()&&calls==1);}
    for(std::size_t cut=0;cut<wire.size();++cut){PoolTlsRpcFramer framer;
        CHECK(framer.feed(std::string_view(wire).substr(0,cut),[](std::string_view){return true;}));CHECK(framer.complete()==(cut==0));}
    for(const char *bad:{"01\nx","0\n","x\n","9999999\n","262401\n"}){PoolTlsRpcFramer f;
        CHECK(!f.feed(bad,[](std::string_view){return true;})&&f.failed()&&!f.complete());CHECK(!f.feed("1\nx",[](std::string_view){return true;}));}
    {PoolTlsRpcFramer f;CHECK(f.feed("9\n",[](std::string_view){return true;})&&!f.complete());}
    {PoolTlsRpcFramer f;CHECK(!f.feed(std::string(4097,'x'),[](std::string_view){return true;})&&f.failed());}
    {PoolTlsRpcFramer f;unsigned calls=0;std::string many;for(unsigned i=0;i<17;++i)many+="1\nx";
        CHECK(!f.feed(many,[&](std::string_view){++calls;return true;})&&calls==16&&f.failed());}
    {PoolTlsRpcFramer f;CHECK(!f.feed("1\nx",[](std::string_view){return false;})&&f.failed());}
}
void codec(){
    auto c=open();std::string wire;CHECK(encodePoolTlsRpcCommand(c,&wire));PoolTlsRpcCommand out;
    CHECK(decodePoolTlsRpcCommand(body(wire),&out)&&out.payload==c.payload);
    for(std::size_t cut=0;cut<body(wire).size();++cut){PoolTlsRpcCommand saved; saved.payload="unchanged";
        CHECK(!decodePoolTlsRpcCommand(std::string_view(body(wire)).substr(0,cut),&saved)&&saved.payload=="unchanged");}
    c={2,1000,2000,O::Write,0,I::None,0,std::string("\0\nexact\r",8)};
    CHECK(encodePoolTlsRpcCommand(c,&wire)&&decodePoolTlsRpcCommand(body(wire),&out)&&out.payload==c.payload);
    auto r=reply(c);r.transfer={T::Bytes,c.payload.size()};PoolTlsRpcReply got;
    CHECK(encodePoolTlsRpcReply(c,r,&wire)&&decodePoolTlsRpcReply(c,body(wire),&got)&&got.transfer.bytes==c.payload.size());
    r.transfer.bytes--;CHECK(!encodePoolTlsRpcReply(c,r,&wire));r=reply(c);r.transfer={T::WouldBlock,0,I::ReadWrite};
    CHECK(encodePoolTlsRpcReply(c,r,&wire)&&decodePoolTlsRpcReply(c,body(wire),&got)&&got.transfer.interest==I::ReadWrite);
    r.transfer.interest=I::None;CHECK(!encodePoolTlsRpcReply(c,r,&wire));r=reply(c);r.state=S::Failed;r.error=E::Cancelled;r.transfer={T::Error,0};
    CHECK(encodePoolTlsRpcReply(c,r,&wire)&&decodePoolTlsRpcReply(c,body(wire),&got)&&got.error==E::Cancelled);r.error=E::None;CHECK(!encodePoolTlsRpcReply(c,r,&wire));
    c={2,1000,2000,O::Read,16,I::None,0,{}};r=reply(c);r.transfer={T::Bytes,3};r.payload=std::string("x\0y",3);
    CHECK(encodePoolTlsRpcReply(c,r,&wire)&&decodePoolTlsRpcReply(c,body(wire),&got)&&got.payload==r.payload);
    r.state=S::Handshaking;CHECK(!encodePoolTlsRpcReply(c,r,&wire));r=reply(c);r.state=S::Closed;r.transfer={T::EndOfStream,0};CHECK(encodePoolTlsRpcReply(c,r,&wire));
    c={2,1000,2000,O::Wait,0,I::ReadWrite,25,{}};r=reply(c);CHECK(!encodePoolTlsRpcReply(c,r,&wire));
    for(auto ready:{PoolTlsRpcReady::Ready,PoolTlsRpcReady::Timeout}){r.ready=ready;CHECK(encodePoolTlsRpcReply(c,r,&wire)&&decodePoolTlsRpcReply(c,body(wire),&got)&&got.ready==ready);}
    for(unsigned mode=0;mode<8;++mode){auto bad=c;if(mode==0)bad.sequence=0;if(mode==1)bad.sequence=1;if(mode==2)bad.issuedMonoMs=0;if(mode==3)bad.deadlineMonoMs=bad.issuedMonoMs;
        if(mode==4)bad.deadlineMonoMs=61001;
        if(mode==5)bad.waitMs=26;
        if(mode==6)bad.interest=I::None;
        if(mode==7)bad.payload="junk";
        CHECK(!validPoolTlsRpcCommand(bad));}
    c={2,1000,2000,O::Write,0,I::None,0,std::string(16385,'x')};CHECK(!validPoolTlsRpcCommand(c));
    c.payload.resize(16384);CHECK(encodePoolTlsRpcCommand(c,&wire)&&decodePoolTlsRpcCommand(body(wire),&out)&&out.payload==c.payload);
    c={2,1000,2000,O::Read,16384,I::None,0,{}};r=reply(c);r.transfer={T::Bytes,16384};r.payload=std::string(16384,'x');r.bufferedRead=true;
    CHECK(encodePoolTlsRpcReply(c,r,&wire)&&decodePoolTlsRpcReply(c,body(wire),&got)&&got.bufferedRead&&got.payload==r.payload);
    for(unsigned mode=0;mode<5;++mode){auto bad=r;if(mode==0)bad.sequence=3;if(mode==1)bad.transfer.bytes=16385;if(mode==2)bad.state=S::Closed;
        if(mode==3)bad.ready=PoolTlsRpcReady::Ready;
        if(mode==4)bad.transfer.interest=I::Read;
        CHECK(!encodePoolTlsRpcReply(c,bad,&wire));}
    r=reply(c);r.state=S::Failed;r.error=E::Transport;r.transfer={T::Error,0};r.bufferedRead=true;CHECK(!encodePoolTlsRpcReply(c,r,&wire));
    r.bufferedRead=false;r.ready=PoolTlsRpcReady::Ready;CHECK(!encodePoolTlsRpcReply(c,r,&wire));
}
void gate(){
    const auto first=open();
    {PoolTlsRpcGate g;g.stop();CHECK(!g.issue(first,1000)&&g.stopped());}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000));CHECK(!g.issue(first,1001)&&g.stopped()&&g.pending()->payload==first.payload);}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000));CHECK(!g.settle(reply(first),2000)&&g.stopped()&&g.pending());}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000));CHECK(!g.settle(reply(first),999)&&g.stopped());}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000)&&g.settle(reply(first),1001));CHECK(!g.settle(reply(first),1002)&&g.stopped());}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000)&&g.settle(reply(first),1001));auto c=first;c.sequence=2;CHECK(!g.issue(c,1002)&&g.stopped());}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000)&&g.settle(reply(first),1001));PoolTlsRpcCommand c{2,1002,2000,O::Write,0,I::None,0,"stable"};
        CHECK(g.issue(c,1002));auto r=reply(c);r.transfer={T::WouldBlock,0,I::Read};r.state=S::Handshaking;
        CHECK(g.settle(r,1003)&&!g.stopped());c.sequence=3;c.issuedMonoMs=1004;c.operation=O::Abort;c.payload.clear();CHECK(g.issue(c,1004));r=reply(c);r.state=S::Closed;
        CHECK(g.settle(r,1005)&&g.stopped()&&!g.issue(first,1006));}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000));g.stop();CHECK(g.pending()->sequence==1&&!g.settle(reply(first),1001)&&!g.settled());}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000));auto stale=reply(first);stale.sequence=2;CHECK(!g.settle(stale,1001)&&g.stopped()&&g.pending()->sequence==1);}
    {PoolTlsRpcGate g;CHECK(g.issue(first,1000)&&g.settle(reply(first),1001));PoolTlsRpcCommand c{3,1002,2000,O::Read,16,I::None,0,{}};
        CHECK(!g.issue(c,1002)&&g.stopped());}
    for(bool read:{false,true}){PoolTlsRpcGate g;CHECK(g.issue(first,1000)&&g.settle(reply(first),1001));PoolTlsRpcCommand c{2,1002,2000,O::Write,0,I::None,0,"stable"};
        CHECK(g.issue(c,1002));auto r=reply(c);r.transfer={T::WouldBlock,0,I::Write};CHECK(g.settle(r,1003));
        c.sequence=3;c.issuedMonoMs=1004;if(read){c.operation=O::Read;c.maximum=16;c.payload.clear();}else c.payload="different";
        CHECK(!g.issue(c,1004)&&g.stopped());}
}
}
int main(){framing();codec();gate();return finishTests("private TLS duplex RPC (CPU; no process/network/mining)");}
