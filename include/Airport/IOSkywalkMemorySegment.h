//
//  IOSkywalkMemorySegment.h
//  AirportItlwm Sequoia 15 Skywalk wiring
//
//  Non-virtual member declaration matching T-exported symbol.
//

#ifndef _AIRPORT_IOSKYWALKMEMORYSEGMENT_H
#define _AIRPORT_IOSKYWALKMEMORYSEGMENT_H

#include <libkern/c++/OSObject.h>

class IOSkywalkMemorySegment : public OSObject {
public:
    // T: __ZNK22IOSkywalkMemorySegment17getVirtualAddressEv
    UInt64 getVirtualAddress() const;
};

#endif /* _AIRPORT_IOSKYWALKMEMORYSEGMENT_H */
