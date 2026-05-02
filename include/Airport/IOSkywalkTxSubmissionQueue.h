//
//  IOSkywalkTxSubmissionQueue.h
//  itlwm
//
//  Minimal local declaration to avoid colliding with MacKernelSDK's
//  IOKit/skywalk/IOSkywalkTxSubmissionQueue.h, whose transitive include
//  of MacKernelSDK's IOSkywalkPacketBufferPool.h conflicts with our local
//  Airport/IOSkywalkPacketBufferPool.h. We only need enough to:
//   - declare an ivar of type IOSkywalkTxSubmissionQueue *
//   - call IOSkywalkTxSubmissionQueue::withPool(...) in start()
//

#ifndef _AIRPORT_IOSKYWALKTXSUBMISSIONQUEUE_H
#define _AIRPORT_IOSKYWALKTXSUBMISSIONQUEUE_H

#include <IOKit/IOEventSource.h>

class IOSkywalkPacket;
class IOSkywalkPacketBufferPool;
class IOSkywalkPacketQueue;
class IOSkywalkTxSubmissionQueue;

typedef IOReturn (*IOSkywalkQueryFreeSpaceHandler)(OSObject *owner, IOSkywalkTxSubmissionQueue *queue, UInt32 *outSpace);
typedef IOReturn (*IOSkywalkTxSubmissionQueueAction)(OSObject *owner, IOSkywalkTxSubmissionQueue *queue, const IOSkywalkPacket **, UInt32, void *);

// IOSkywalkPacketQueue inherits IOEventSource per Apple's binary; here
// we only use the leaf class as a black box (passed by pointer).
class IOSkywalkTxSubmissionQueue : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkTxSubmissionQueue)

public:
    static IOSkywalkTxSubmissionQueue * withPool(
        IOSkywalkPacketBufferPool *pool,
        UInt32 capacity,
        UInt32 queueId,
        OSObject *owner,
        IOSkywalkTxSubmissionQueueAction action,
        void *refCon,
        IOOptionBits options);
};

#endif /* _AIRPORT_IOSKYWALKTXSUBMISSIONQUEUE_H */
