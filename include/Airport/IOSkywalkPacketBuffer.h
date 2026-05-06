//
//  IOSkywalkPacketBuffer.h
//  AirportItlwm Sequoia 15 Skywalk wiring (Stage 1+)
//
//  Minimal local declaration. Mangled symbols verified T-exported in BootKC 15.7.5.
//

#ifndef _AIRPORT_IOSKYWALKPACKETBUFFER_H
#define _AIRPORT_IOSKYWALKPACKETBUFFER_H

#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>

class IOSkywalkMemorySegment;

class IOSkywalkPacketBuffer : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkPacketBuffer)

public:
    // T: __ZNK21IOSkywalkPacketBuffer16getMemorySegmentEv
    virtual IOSkywalkMemorySegment *getMemorySegment(void) const;
    // T: __ZNK21IOSkywalkPacketBuffer22getMemorySegmentOffsetEv
    virtual UInt64  getMemorySegmentOffset(void) const;
    // T: __ZNK21IOSkywalkPacketBuffer10getDataOffEv
    virtual UInt32  getDataOff(void) const;
    // T: __ZN21IOSkywalkPacketBuffer13setDataLengthEj
    virtual IOReturn setDataLength(UInt32 length);
};

#endif /* _AIRPORT_IOSKYWALKPACKETBUFFER_H */
