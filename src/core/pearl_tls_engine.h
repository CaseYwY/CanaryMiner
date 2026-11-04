// Private memory-only native engine seam. Raw I/O, cancellation, deadlines and application publication stay shared.
#pragma once
#include "pearl_tls_stream.h"
namespace om {
enum class PearlTlsEngineStatus { Complete, Bytes, Progress, NeedRead, NeedWrite, Closed, Error };
struct PearlTlsEngineStep {PearlTlsEngineStatus status=PearlTlsEngineStatus::Error;std::size_t bytes=0;};
class PearlTlsEngine {
   public:
    virtual ~PearlTlsEngine()=default;
    virtual PearlTlsEngineStep handshake()=0;
    virtual PearlTlsEngineStep send(const char *,std::size_t)=0;
    virtual PearlTlsEngineStep receive(char *,std::size_t)=0;
    virtual std::size_t inputCapacity()const=0;
    virtual bool input(const char *,std::size_t)=0;
    virtual bool endInput()=0;
    virtual std::size_t outputSize()const=0;
    virtual std::size_t takeOutput(char *,std::size_t)=0;
    virtual bool retryBufferedInput()const=0; // progress is possible before asking the raw channel for more input
    virtual bool bufferedRead()const=0; // conservative hint, including incomplete/control input
    virtual std::size_t maximumPlaintext()const{return 16384;}
    virtual bool verified()const{return true;}
    virtual bool tick(uint64_t){return true;}
    virtual void outputFlushed(){}
    virtual PearlTlsError error()const=0;
};
std::unique_ptr<PearlTlsEngine> createPearlOpenSslEngine(const PearlTlsPolicy &,PearlTlsError *);
std::unique_ptr<PearlTlsEngine> createPearlSchannelEngine(const PearlTlsPolicy &,const std::atomic<bool> &,PearlTlsError *);
} // namespace om
