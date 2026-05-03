//
//  IO80211FaultReporter.h
//  itlwm
//
//  Re-introduced 2026-05-04 after RE specialist confirmation that
//  IO80211FaultReporter exists in real Sequoia 15.7.5 BootKC (md5 e4cc3972).
//  An earlier RE pass mistakenly concluded the class was removed in 15.7.5
//  because it queried the wrong (Sonoma) BootKC file. Apple actually exports:
//
//    __ZTV20IO80211FaultReporter        @ 0xffffff80023d13f8
//    IO80211FaultReporter::allocWithParams(CCFaultReporter*)
//                                       @ exported T symbol
//    IO80211FaultReporter::triggerFault(...)
//
//  Required because IO80211PeerManager::initWithInterface dispatches
//  ccFaultReporter->vtable[byte 0x120] = registerCallbacks. On a raw
//  CCFaultReporter (vtable 36 = inherited IORegistryEntry::copyProperty),
//  that dispatches a wrong method -> kernel page fault on null+0x38.
//  IO80211FaultReporter overrides slot 36 with a trampoline to the real
//  CCFaultReporter::registerCallbacks body.
//

#ifndef IO80211FaultReporter_h
#define IO80211FaultReporter_h

#include <IOKit/IOService.h>

class CCFaultReporter;

class IO80211FaultReporter : public OSObject {
    OSDeclareDefaultStructors(IO80211FaultReporter)

public:
    static IO80211FaultReporter *allocWithParams(CCFaultReporter *cc);
};

#endif /* IO80211FaultReporter_h */
