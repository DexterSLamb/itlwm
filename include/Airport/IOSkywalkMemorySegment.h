//
//  IOSkywalkMemorySegment.h
//  AirportItlwm Sequoia 15 Skywalk wiring (Stage 1+)
//

#ifndef _AIRPORT_IOSKYWALKMEMORYSEGMENT_H
#define _AIRPORT_IOSKYWALKMEMORYSEGMENT_H

#include <libkern/c++/OSObject.h>

class IOSkywalkMemorySegment : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkMemorySegment)

public:
    // T: __ZNK22IOSkywalkMemorySegment17getVirtualAddressEv
    virtual UInt64 getVirtualAddress(void) const;
};

#endif /* _AIRPORT_IOSKYWALKMEMORYSEGMENT_H */
