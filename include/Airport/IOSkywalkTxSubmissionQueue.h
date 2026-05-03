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
// Sequoia 15.7.5 ground truth: Apple exports the action callback returning
// unsigned int (mangled "j"), not IOReturn (mangled "i"). Using IOReturn
// generates a different mangled symbol for withPool() and kxld silently
// fails to bind it -> driver doesn't load (boot completes, no panic, no en0).
// Source: research/sequoia-port/diff/15.7.5-IOSkywalkTxSubmissionQueue-vtable.txt
// Sequoia 15.7.5 ground truth (corrected): Apple's actual mangled symbol for
// the packets-array argument is `PKP15IOSkywalkPacket` = `IOSkywalkPacket *
// const *` (pointer to const-pointer, i.e., the array pointers can't be
// reassigned but the packets they point to are mutable). Previously declared
// `const IOSkywalkPacket **` which mangles `PPK15IOSkywalkPacket` (P-P-K) —
// off by one K position. Lilu solveSymbol returned NULL for Tx withPool
// because the mangled name didn't match Apple's export. Verified via:
//   nm BootKC | grep "ZN26IOSkywalkTxSubmissionQueue8withPool" → PKP form
typedef unsigned int (*IOSkywalkTxSubmissionQueueAction)(OSObject *owner, IOSkywalkTxSubmissionQueue *queue, IOSkywalkPacket * const *, UInt32, void *);

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
