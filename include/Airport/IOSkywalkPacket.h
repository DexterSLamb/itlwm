//
//  IOSkywalkPacket.h
//  AirportItlwm Sequoia 15 Skywalk wiring
//
//  Declares IOSkywalkPacket as a class with NON-VIRTUAL member functions
//  matching T-exported mangled symbols in BootKC 15.7.5. Compiler emits
//  direct symbol references that kxld resolves at boot — no vtable
//  alignment concerns. We never instantiate this class; we only receive
//  pointers from Apple's framework via TX submission queue dequeue.
//

#ifndef _AIRPORT_IOSKYWALKPACKET_H
#define _AIRPORT_IOSKYWALKPACKET_H

#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>

class IOSkywalkPacketBuffer;
class IOSkywalkPacketQueue;

class IOSkywalkPacket : public OSObject {
public:
    // T: __ZNK15IOSkywalkPacket16getPacketBuffersEPP21IOSkywalkPacketBufferj
    UInt32 getPacketBuffers(IOSkywalkPacketBuffer **buffers,
                            UInt32 maxBuffers) const;
    // T: __ZNK15IOSkywalkPacket20getPacketBufferCountEv
    UInt32 getPacketBufferCount() const;
    // T: __ZN15IOSkywalkPacket13setDataLengthEj
    IOReturn setDataLength(UInt32 length);
    // T: __ZN15IOSkywalkPacket7prepareEP20IOSkywalkPacketQueueyj
    IOReturn prepare(IOSkywalkPacketQueue *queue, UInt64 offset,
                     IOOptionBits options);
    // T: __ZN15IOSkywalkPacket17completeWithQueueEP20IOSkywalkPacketQueuejj
    // Note: direction and options are both UInt32 in the export signature.
    IOReturn completeWithQueue(IOSkywalkPacketQueue *queue,
                               UInt32 direction, UInt32 options);
};

#endif /* _AIRPORT_IOSKYWALKPACKET_H */
