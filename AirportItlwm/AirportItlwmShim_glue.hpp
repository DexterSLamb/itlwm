//
//  AirportItlwmShim_glue.hpp
//
//  Function pointers and resolver for the Apple kext symbols that are not
//  exported on Sonoma 14.4+ / Sequoia 15.x. Populated at AirportItlwm::start
//  by reading IOResources properties published by AirportItlwmShim.kext
//  (a small Lilu plugin that runs solveSymbol against the running kernel
//  image).
//
//  Why not just call the symbols directly? Because kxld checks every direct
//  symbol reference (including those reached through inherited vtable slots
//  that we didn't override) against the loaded Apple kext's export table.
//  When the symbol is gone from the export table the entire kext fails to
//  load. Routing through a function pointer obtained at runtime hides the
//  reference from the kxld static-link check.
//

#ifndef AirportItlwmShim_glue_hpp
#define AirportItlwmShim_glue_hpp

#if __IO80211_TARGET >= __MAC_15_0

#include <IOKit/IOService.h>

class IOSkywalkPacketBufferPool;
class IOSkywalkTxSubmissionQueue;
class IOSkywalkRxCompletionQueue;
class IOSkywalkPacket;
class IO80211Controller;

// Sequoia 15.7.5 ground-truth: actions return UInt32 (not IOReturn).
// Sequoia 15.7.5 ground-truth: pointer arg is `IOSkywalkPacket * const *`
// (PKP) not `const IOSkywalkPacket **` (PPK). Verified via Apple BootKC nm.
typedef unsigned int (*TxSubmissionAction)(OSObject *owner,
                                           IOSkywalkTxSubmissionQueue *queue,
                                           IOSkywalkPacket * const *packets,
                                           uint32_t count, void *refcon);
typedef unsigned int (*RxCompletionAction)(OSObject *owner,
                                           IOSkywalkRxCompletionQueue *queue,
                                           IOSkywalkPacket **packets,
                                           uint32_t count, void *refcon);

typedef IOSkywalkTxSubmissionQueue *(*TxWithPoolFn)(
    IOSkywalkPacketBufferPool *pool, uint32_t txringSize, uint32_t prio,
    OSObject *owner, TxSubmissionAction action, void *refcon, uint32_t flags);

typedef IOSkywalkRxCompletionQueue *(*RxWithPoolFn)(
    IOSkywalkPacketBufferPool *pool, uint32_t rxringSize, uint32_t prio,
    OSObject *owner, RxCompletionAction action, void *refcon, uint32_t flags);

typedef void (*PostMessageFn)(IO80211Controller *self, uint32_t msg,
                              void *data, unsigned long dataLen,
                              uint32_t arg4, void *arg5);

extern TxWithPoolFn  gShimTxWithPool;
extern RxWithPoolFn  gShimRxWithPool;
extern PostMessageFn gShimPostMessage;

// Pull resolved pointers out of IOResources. Returns true if Tx + Rx
// withPool both resolved (the minimum required to start the Sequoia path).
// postMessage is best-effort: if absent we still return true and the
// AirportItlwm::postMessage override falls through to a no-op.
//
// Caller is expected to waitForService(IOResourceMatching("AirportItlwm-Shim-Ready"))
// before calling this function.
bool resolveSequoiaShimSymbols(void);

#endif /* __IO80211_TARGET >= __MAC_15_0 */

#endif /* AirportItlwmShim_glue_hpp */
