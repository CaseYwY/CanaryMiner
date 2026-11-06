// Separate translation unit verifies one shared inline fence across static-library-style boundaries.
#include "product/native_process_admission.h"
uint64_t peerNativeProcessEpoch(){return om::nativeProcessEpoch();}
bool peerNativeProcessUnresolved(){return om::nativeProcessUnresolved();}
void peerNativeProcessFault(om::NativeProcessAdmission &owner){owner.markUnresolved();}
void peerNativeProcessSettle(om::NativeProcessAdmission &owner){owner.settled();}
