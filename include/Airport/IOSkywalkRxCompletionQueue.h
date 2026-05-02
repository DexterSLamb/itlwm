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
};

#endif /* _AIRPORT_IOSKYWALKRXCOMPLETIONQUEUE_H */
