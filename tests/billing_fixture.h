#pragma once
#include "electricity_fixture.h"
#include "product/billing_ledger.h"
namespace billing_fixture {
using namespace om;
using namespace electricity_fixture;
inline BillingBinding binding(){BillingBinding b;b.cycleKey="fixture.cycle";b.plan=plan(T,T+2*hour);b.evidence=evidence();b.cycleBeginUtcMs=T;b.cycleEndUtcMs=T+2*hour;return b;}
inline BillingChunk chunk(unsigned sequence=1){BillingChunk b;b.sequence=sequence;b.id="fixture.chunk."+std::to_string(sequence);b.observedUtcMs=T+sequence*hour;b.trace=load("1000",T+(sequence-1)*hour,T+sequence*hour);return b;}
} // namespace billing_fixture
