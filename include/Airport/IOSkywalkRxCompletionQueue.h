//
//  IOSkywalkRxCompletionQueue.h
//  itlwm
//
//  Minimal local declaration; see IOSkywalkTxSubmissionQueue.h for the
//  reasoning (avoids colliding with MacKernelSDK's pool definition).
//

#ifndef _AIRPORT_IOSKYWALKRXCOMPLETIONQUEUE_H
#define _AIRPORT_IOSKYWALKRXCOMPLETIONQUEUE_H

#include <IOKit/IOEventSource.h>

class IOSkywalkPacket;
class IOSkywalkPacketBufferPool;
class IOSkywalkPacketQueue;
class IOSkywalkRxCompletionQueue;

// Sequoia 15.7.5 ground truth: callback returns unsigned int (mangled "j"),
// not IOReturn (mangled "i"). See IOSkywalkTxSubmissionQueue.h for rationale.
typedef unsigned int (*IOSkywalkRxCompletionQueueAction)(OSObject *owner, IOSkywalkRxCompletionQueue *, IOSkywalkPacket **, UInt32, void *);

class IOSkywalkRxCompletionQueue : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkRxCompletionQueue)

public:
    static IOSkywalkRxCompletionQueue * withPool(
        IOSkywalkPacketBufferPool *pool,
        UInt32 capacity,
        UInt32 queueId,
        OSObject *owner,
        IOSkywalkRxCompletionQueueAction action,
        void *refCon,
        IOOptionBits options);

    // T: __ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj
    // Push received packets to the framework. count = packets in array;
    // flags 0 normal, 0x1 sync wakeup. Returns kIOReturnSuccess (0) or
    // 0xe00002c2 (bad arg) / 0xe00002d5 (queue stopped) /
    // 0xe00002d7 (not enabled) / 0xe00002d8 (no consumer attached).
    // NON-virtual — direct symbol call, no vtable alignment risk.
    IOReturn enqueuePackets(IOSkywalkPacket * const *packets,
                            UInt32 count,
                            UInt32 flags);
};

#endif /* _AIRPORT_IOSKYWALKRXCOMPLETIONQUEUE_H */
