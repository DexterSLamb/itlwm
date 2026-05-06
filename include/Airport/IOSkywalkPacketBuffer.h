//
//  IOSkywalkPacketBuffer.h
//  AirportItlwm Sequoia 15 Skywalk wiring
//
//  Non-virtual member function declarations matching T-exported symbols.
//

#ifndef _AIRPORT_IOSKYWALKPACKETBUFFER_H
#define _AIRPORT_IOSKYWALKPACKETBUFFER_H

#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>

class IOSkywalkMemorySegment;

class IOSkywalkPacketBuffer : public OSObject {
public:
    // T: __ZNK21IOSkywalkPacketBuffer16getMemorySegmentEv
    IOSkywalkMemorySegment * getMemorySegment() const;
    // T: __ZNK21IOSkywalkPacketBuffer22getMemorySegmentOffsetEv
    UInt64 getMemorySegmentOffset() const;
    // T: __ZNK21IOSkywalkPacketBuffer10getDataOffEv
    UInt32 getDataOff() const;
    // T: __ZN21IOSkywalkPacketBuffer13setDataLengthEj
    IOReturn setDataLength(UInt32 length);
    // T: __ZNK21IOSkywalkPacketBuffer13getDataLengthEv
    UInt32 getDataLength() const;
};

#endif /* _AIRPORT_IOSKYWALKPACKETBUFFER_H */
