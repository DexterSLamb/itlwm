//
//  CCFaultReporter.h
//  itlwm
//
//  Reverse-engineered from CoreCapture.kext (macOS 26 Tahoe).
//  Object size: 0x90 bytes. Sibling of CCStream (same parent).
//

#ifndef CCFaultReporter_h
#define CCFaultReporter_h

#include <IOKit/IOService.h>

class CCDataStream;

class CCFaultReporter : public IOService {
    OSDeclareAbstractStructors(CCFaultReporter)

public:
    // Factory: creates a CCFaultReporter wrapping the given CCDataStream.
    // Symbol: __ZN15CCFaultReporter18withStreamWorkloopEP12CCDataStreamP10IOWorkLoop
    static CCFaultReporter *withStreamWorkloop(CCDataStream *stream, IOWorkLoop *workloop);
};

#endif /* CCFaultReporter_h */
