//
//  IOSkywalkPacket.h
//  AirportItlwm Sequoia 15 Skywalk wiring (Stage 1+)
//
//  Minimal local declaration to avoid MacKernelSDK header collisions.
//  Mangled symbols verified T-exported in BootKC 15.7.5.
//

#ifndef _AIRPORT_IOSKYWALKPACKET_H
#define _AIRPORT_IOSKYWALKPACKET_H

#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>

class IOSkywalkPacketBuffer;
class IOSkywalkPacketQueue;

class IOSkywalkPacket : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkPacket)

public:
    // T: __ZNK15IOSkywalkPacket16getPacketBuffersEPP21IOSkywalkPacketBufferj
    // (returns count of buffers actually filled into the array)
    virtual UInt32  getPacketBuffers(IOSkywalkPacketBuffer **buffers, UInt32 maxBuffers);
    // T: __ZNK15IOSkywalkPacket20getPacketBufferCountEv
    virtual UInt32  getPacketBufferCount();
    // T: __ZN15IOSkywalkPacket13setDataLengthEj
    virtual IOReturn setDataLength(UInt32 length);
    // T: __ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj
    virtual IOReturn prepare(IOSkywalkPacketQueue *queue, UInt64 offset, IOOptionBits options);
    // T: __ZN15IOSkywalkPacket17completeWithQueueEP20IOSkywalkPacketQueuejj
    virtual IOReturn completeWithQueue(IOSkywalkPacketQueue *queue, UInt32 direction, UInt32 options);
};

#endif /* _AIRPORT_IOSKYWALKPACKET_H */
