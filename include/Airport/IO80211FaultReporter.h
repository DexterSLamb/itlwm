//
//  IO80211FaultReporter.h
//  itlwm
//
//  Reverse-engineered from IO80211Family.kext (macOS 26 Tahoe).
//  Thin wrapper around CCFaultReporter, returned by getFaultReporterFromDriver().
//  Object size: 0x18 bytes (OSObject 0x10 + CCFaultReporter* at +0x10).
//

#ifndef IO80211FaultReporter_h
#define IO80211FaultReporter_h

#include <libkern/c++/OSObject.h>

class CCFaultReporter;

class IO80211FaultReporter : public OSObject {
    OSDeclareAbstractStructors(IO80211FaultReporter)

public:
    // Factory: allocates an IO80211FaultReporter wrapping the given CCFaultReporter.
    // Symbol: __ZN20IO80211FaultReporter15allocWithParamsEP15CCFaultReporter
    static IO80211FaultReporter *allocWithParams(CCFaultReporter *reporter);
};

#endif /* IO80211FaultReporter_h */
