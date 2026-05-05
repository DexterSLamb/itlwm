//
//  IOSkywalkInterface.h
//  itlwm
//
//  Created by qcwap on 2023/6/7.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef _IO80211CONTROLLER_H
#define _IO80211CONTROLLER_H

#if defined(KERNEL) && defined(__cplusplus)

#include <Availability.h>
#include <libkern/version.h>

// This is necessary, because even the latest Xcode does not support properly targeting 11.0.
#ifndef __IO80211_TARGET
#error "Please define __IO80211_TARGET to the requested version"
#endif

#if VERSION_MAJOR > 8
#define _MODERN_BPF
#endif

#include <sys/kpi_mbuf.h>

#include <IOKit/network/IOEthernetController.h>

#include <sys/param.h>
#include <net/bpf.h>

#include "apple80211_ioctl.h"
#include "IO80211SkywalkInterface.h"
#include "IO80211WorkLoop.h"
#include "IO80211WorkQueue.h"
#include "CCStream.h"
#include "CCDataPipe.h"
#include "CCLogPipe.h"
#include "CCLogStream.h"

#define AUTH_TIMEOUT            15    // seconds

/*! @enum LinkSpeed.
 @abstract ???.
 @discussion ???.
 @constant LINK_SPEED_80211A 54 Mbps
 @constant LINK_SPEED_80211B 11 Mbps.
 @constant LINK_SPEED_80211G 54 Mbps.
 */
enum {
    LINK_SPEED_80211A    = 54000000ul,        // 54 Mbps
    LINK_SPEED_80211B    = 11000000ul,        // 11 Mbps
    LINK_SPEED_80211G    = 54000000ul,        // 54 Mbps
    LINK_SPEED_80211N    = 300000000ul,        // 300 Mbps (MCS index 15, 400ns GI, 40 MHz channel)
};

enum IO80211CountryCodeOp
{
    kIO80211CountryCodeReset,                // Reset country code to world wide default, and start
    // searching for 802.11d beacon
};
typedef enum IO80211CountryCodeOp IO80211CountryCodeOp;

enum IO80211SystemPowerState
{
    kIO80211SystemPowerStateUnknown,
    kIO80211SystemPowerStateAwake,
    kIO80211SystemPowerStateSleeping,
};
typedef enum IO80211SystemPowerState IO80211SystemPowerState;

enum IO80211FeatureCode
{
    kIO80211Feature80211n = 1,
};
typedef enum IO80211FeatureCode IO80211FeatureCode;


class IOSkywalkInterface;
class IO80211ScanManager;

enum scanSource
{
    SOURCE_1,
};

enum joinStatus
{
    STATUS_1,
};

class IO80211Controller;
class IO80211Interface;
class IO82110WorkLoop;
class IO80211VirtualInterface;
class IO80211ControllerMonitor;
class CCLogPipe;
class CCIOReporterLogStream;
class CCLogStream;
class IO80211VirtualInterface;
class IO80211RangingManager;
class IO80211FlowQueue;
class IO80211FlowQueueLegacy;
class FlowIdMetadata;
class IOReporter;
class IO80211InfraInterface;
extern void IO80211VirtualInterfaceNamerRetain();


struct apple80211_hostap_state;

struct apple80211_awdl_sync_channel_sequence;
struct ieee80211_ht_capability_ie;
struct apple80211_channel_switch_announcement;
struct apple80211_beacon_period_data;
struct apple80211_power_debug_sub_info;
struct apple80211_stat_report;
struct apple80211_frame_counters;
struct apple80211_leaky_ap_event;
struct apple80211_chip_stats;
struct apple80211_extended_stats;
struct apple80211_ampdu_stat_report;
struct apple80211_btCoex_report;
struct apple80211_cca_report;
class CCPipe;
struct apple80211_lteCoex_report;

//typedef int scanSource;
//typedef int joinStatus;
//typedef int CCStreamLogLevel;
typedef IOReturn (*IOCTL_FUNC)(IO80211Controller*, IO80211Interface*, IO80211VirtualInterface*, apple80211req*, bool);
extern IOCTL_FUNC gGetHandlerTable[];
extern IOCTL_FUNC gSetHandlerTable[];

class IO80211InterfaceAVCAdvisory;

// Sequoia 15.7+ adds new vtable methods that take these types.
// We don't need to know the layout—forward declarations only,
// since IO80211Controller declares them as method parameters
// (compiles against the pointer type, not the full struct).
struct apple80211_platform_config;

#if __IO80211_TARGET < __MAC_15_0
// =============================================================================
// Sonoma 14.4 path - 原始 class 定义, 严格保留 Sonoma 14.x 兼容性, 不要改这块
// =============================================================================
class IO80211Controller : public IOEthernetController {
    OSDeclareAbstractStructors(IO80211Controller)

public:

    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool init(OSDictionary *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn configureReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn updateReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *) APPLE_KEXT_OVERRIDE;
    virtual IOWorkLoop* getWorkLoop(void) const APPLE_KEXT_OVERRIDE;
    virtual const char* stringFromReturn(int) APPLE_KEXT_OVERRIDE;
    virtual int errnoFromReturn(int) APPLE_KEXT_OVERRIDE;
    virtual UInt32 getFeatures() const APPLE_KEXT_OVERRIDE;
    virtual const OSString * newVendorString() const APPLE_KEXT_OVERRIDE;
    virtual const OSString * newModelString() const APPLE_KEXT_OVERRIDE;
    virtual bool createWorkLoop() APPLE_KEXT_OVERRIDE;
    virtual IOReturn getHardwareAddress(IOEthernetAddress *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setHardwareAddress(const IOEthernetAddress * addrP) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;
    virtual bool isCommandProhibited(int) = 0;
    virtual bool createWorkQueue();
    virtual IO80211WorkQueue *getWorkQueue();
    virtual void requestPacketTx(void*, UInt);
    virtual IOCommandGate *getIO80211CommandGate();
    virtual IOReturn getHardwareAddressForInterface(IOEthernetAddress *);
    virtual bool useAppleRSNSupplicant(IO80211VirtualInterface *);
    virtual IO80211SkywalkInterface* getPrimarySkywalkInterface(void);
    virtual int bpfOutputPacket(OSObject *,UInt,mbuf_t m);
    virtual SInt32 monitorModeSetEnabled(bool, UInt);
    virtual SInt32 apple80211_ioctl(IO80211SkywalkInterface *,unsigned long,void *, bool, bool);
    virtual SInt32 apple80211VirtualRequest(UInt,int,IO80211VirtualInterface *,void *);
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *);
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *,void *);

    virtual SInt32 handleCardSpecific(IO80211SkywalkInterface *,unsigned long,void *,bool) = 0;

    virtual UInt32 hardwareOutputQueueDepth();
    virtual SInt32 performCountryCodeOperation(IO80211CountryCodeOp);

    virtual void dataLinkLayerAttachComplete();
    virtual SInt32 enableFeature(IO80211FeatureCode, void*) = 0;

    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *,apple80211_capability_data *) = 0;
    virtual IOReturn getPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;
    virtual IOReturn setPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *,apple80211_debug_command *) = 0;

    virtual SInt32 setVirtualHardwareAddress(IO80211VirtualInterface *,ether_addr *);
    virtual SInt32 enableVirtualInterface(IO80211VirtualInterface *);
    virtual SInt32 disableVirtualInterface(IO80211VirtualInterface *);
    virtual bool requiresExplicitMBufRelease();
    virtual bool flowIdSupported() {
        return false;
    }
    virtual IO80211FlowQueueLegacy* requestFlowQueue(FlowIdMetadata const*);
    virtual void releaseFlowQueue(IO80211FlowQueue *);
    virtual bool getLogPipes(CCPipe**, CCPipe**, CCPipe**);
    virtual void enableFeatureForLoggingFlags(unsigned long long) {};
    virtual IOReturn requestQueueSizeAndTimeout(unsigned short *, unsigned short *) { return kIOReturnIOError; };
    virtual IOReturn enablePacketTimestamping(void) {
        return kIOReturnUnsupported;
    }
    virtual IOReturn disablePacketTimestamping(void) {
        return kIOReturnUnsupported;
    }

    virtual UInt getPacketTSCounter();
    virtual void *getDriverTextLog();

    virtual UInt32 selfDiagnosticsReport(int,char const*,UInt);

    virtual void *getFaultReporterFromDriver();

    virtual UInt32 getDataQueueDepth(OSObject *);
    virtual bool isAssociatedToMovingNetwork(void) { return false; }
    virtual bool wasDynSARInFailSafeMode(void) { return false; }
    virtual void updateAdvisoryScoresIfNeed(void);
    virtual UInt64 getAVCAdvisoryInfo(IO80211InterfaceAVCAdvisory *);
    virtual SInt32 apple80211_ioctl_get(IO80211SkywalkInterface *,void *, bool, bool);
    virtual SInt32 apple80211_ioctl_set(IO80211SkywalkInterface *,void *, bool, bool);
    virtual bool attachInterface(OSObject *,IOService *);
    virtual SInt32 apple80211_ioctl_get(IO80211VirtualInterface *,void *,bool,bool);
    virtual SInt32 apple80211_ioctl_set(IO80211VirtualInterface *,void *,bool,bool);
    virtual void detachInterface(OSObject *,bool);
    virtual IO80211VirtualInterface* createVirtualInterface(ether_addr *,UInt);
    virtual bool attachVirtualInterface(IO80211VirtualInterface **,ether_addr *,UInt,bool);
    virtual bool detachVirtualInterface(IO80211VirtualInterface *,bool);
    virtual IOReturn enable(IO80211SkywalkInterface *);
    virtual IOReturn disable(IO80211SkywalkInterface *);

    OSMetaClassDeclareReservedUnused( IO80211Controller,  0);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  1);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  2);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  3);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  4);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  5);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  6);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  7);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  8);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  9);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 10);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 11);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 12);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 13);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 14);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 15);

    virtual void postMessage(UInt,void *,unsigned long,UInt,void *);
    virtual IOReturn setMulticastList(ether_addr const*, UInt);

protected:
    uint8_t  filler[0x128];
};
#else // __IO80211_TARGET >= __MAC_15_0
// =============================================================================
// Sequoia 15.7.5 path — IO80211Controller vtable layout reflecting Apple's
// actual binary BootKernelExtensions.kc (md5 e4cc39724e8a291a68467d492ed86d08).
//
// IMPORTANT: previous revisions of this header used Sonoma 14.8.5 BootKC data
// labelled as Sequoia (filename mismatch). Real Sequoia 15.7.5 has fewer
// virtuals — it removed apple80211_ioctl, apple80211{Virtual,Skywalk}Request,
// apple80211_ioctl_get/set on Skywalk/Virtual interfaces, enable/disable
// (IO80211SkywalkInterface*), useAppleRSNSupplicant, getHardwareAddressForInterface,
// setVirtualHardwareAddress, isAssociatedToMovingNetwork, the postMessage(uint, ...)
// 5-arg variant — and added debugStateInit, getPLATFORM_CONFIG,
// allocIO80211RecursiveLock, getActionFramePoolCapacity, getPostOffice,
// CreatePostOffice. A bunch of slots in the 410-434 range that were concrete
// methods in 14.x are now ___cxa_pure_virtual padding. See
// research/sequoia-port/diff/15.7.5-IO80211Controller-vtable-REAL.txt for the
// complete extraction.
//
// IOEthernetController extension at slots 331-332: Apple inserted
// allocatePacketNoWait + setHardwareAssists in 15.7. The MacKernelSDK header
// is patched (patches/0001-IONetworkController-sequoia-slots-6-7.patch) so
// these get correct mangled names.
//
// Slot 396 (___cxa_pure_virtual padding): MacKernelSDK's IOEthernetController
// stops one slot earlier than Apple's; we add a placeholder concrete vmethod
// to keep AirportItlwm's vtable aligned with Apple's IO80211Controller.
// =============================================================================
class IO80211Controller : public IOEthernetController {
    OSDeclareAbstractStructors(IO80211Controller)

public:

    // --- IOEthernetController / IONetworkController / IOService overrides ---
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool init(OSDictionary *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn configureReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn updateReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *) APPLE_KEXT_OVERRIDE;
    virtual IOWorkLoop* getWorkLoop(void) const APPLE_KEXT_OVERRIDE;
    virtual const char* stringFromReturn(int) APPLE_KEXT_OVERRIDE;
    virtual int errnoFromReturn(int) APPLE_KEXT_OVERRIDE;
    virtual UInt32 getFeatures() const APPLE_KEXT_OVERRIDE;
    virtual const OSString * newVendorString() const APPLE_KEXT_OVERRIDE;
    virtual const OSString * newModelString() const APPLE_KEXT_OVERRIDE;
    virtual bool createWorkLoop() APPLE_KEXT_OVERRIDE;
    virtual IOReturn getHardwareAddress(IOEthernetAddress *) APPLE_KEXT_OVERRIDE;  // slot 358
    // slot 359: Apple's parent IOEthernetController owns
    // setHardwareAddress(IOEthernetAddress*) (no IO80211Controller override).
    // We DO NOT redeclare it here — letting the parent vtable entry
    // (__ZN20IOEthernetController18setHardwareAddressEPK17IOEthernetAddress)
    // bind through MacKernelSDK's IOEthernetController.h declaration.
    virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;             // slot 360
    virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;           // slot 362

    // SLOT 396 placeholder — empirical re-audit (post-commit ddd8d2f) of the
    // built binary against 15.7.5 BootKC IO80211Controller showed
    // MacKernelSDK's IOEthernetController vtable actually reaches Apple's
    // exact slot 395 (last entry _RESERVEDIOEthernetController31). Apple's
    // IO80211Controller adds **only ONE** ___cxa_pure_virtual padding slot
    // at index 396 before its first concrete method (createWorkQueue at 397).
    // The previous fix added 3 placeholders, pushing getDriverTextLog from
    // slot 432 to slot 434 (off by +2). We now keep ONE placeholder so
    // index 396 lines up with Apple's PV slot, and createWorkQueue lands at
    // 397 as Apple expects. Returns void* nullptr — Apple's slot 396 is PV
    // (Apple never calls it), but should anything dispatch on it the nullptr
    // return is safe for both pointer and integer interpretations.
    virtual void *_seq_eth_ext_slot396_placeholder() { return nullptr; }                              // slot 396 [PV padding]

    // --- IO80211Controller's own new vmethods, slot order matches 15.7.5 ---

    // slot 397: createWorkQueue MUST return IO80211WorkQueue* (not bool) —
    // Apple's IO80211Controller::start passes the return to
    // IO80211CommandGate::allocWithParams as the workqueue arg.
    virtual IO80211WorkQueue *createWorkQueue();                                    // slot 397
    virtual void debugStateInit();                                                  // slot 398 [present in 15.7.5]
    virtual IO80211WorkQueue *getWorkQueue() const;                                 // slot 399 [CONST in 15.7.5]
    virtual void requestPacketTx(void*, UInt);                                      // slot 400
    virtual IOCommandGate *getIO80211CommandGate() const;                           // slot 401 [CONST in 15.7.5]
    virtual IO80211SkywalkInterface* getPrimarySkywalkInterface(void);              // slot 402
    virtual int bpfOutputPacket(OSObject *,UInt,mbuf_t m);                          // slot 403
    virtual SInt32 monitorModeSetEnabled(bool, UInt);                               // slot 404
    // Apple's slot 405 is ___cxa_pure_virtual (PV padding for an unused
    // driver hook). Provide a no-op concrete impl so AirportItlwm's vtable
    // has SOMETHING at this slot; the slot index is what matters for OC's
    // vtable patcher to bind correctly to the parent vtable layout.
    virtual void *_seq_pad_slot405() { return nullptr; }                                              // slot 405 [PV padding]
    virtual UInt32 hardwareOutputQueueDepth();                                      // slot 406
    virtual SInt32 performCountryCodeOperation(IO80211CountryCodeOp);               // slot 407
    virtual void dataLinkLayerAttachComplete();                                     // slot 408
    virtual SInt32 enableFeature(IO80211FeatureCode, void*);                        // slot 409 [concrete in 15.7.5]
    virtual bool isCommandProhibited(int) = 0;                                      // slot 410 [PV]
    // slots 411-417: 7 PVs. Best mapping based on 14.4 layout (slots 415-422
    // were 8 IOCTL hooks: getDRIVER_VERSION..setGET_DEBUG_INFO). In 15.7.5 the
    // setGET_DEBUG_INFO entry was removed and getPLATFORM_CONFIG (concrete)
    // was inserted at the end (slot 418), shifting the IOCTL PVs up by 4 to
    // slots 411-417. The exact mapping matters for binary compat with drivers,
    // but since AirportItlwm is the only driver here, we just need the correct
    // PV count and slot positions — semantic mapping is best-effort.
    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;     // 411 [PV]
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;   // 412 [PV]
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *,apple80211_capability_data *) = 0;// 413 [PV]
    virtual IOReturn getPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;                // 414 [PV]
    virtual IOReturn setPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;                // 415 [PV]
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;  // 416 [PV]
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;  // 417 [PV]

    virtual IOReturn getPLATFORM_CONFIG(IO80211SkywalkInterface *, apple80211_platform_config *);    // 418 [NEW concrete]
    virtual SInt32 enableVirtualInterface(IO80211VirtualInterface *);                                // 419
    virtual SInt32 disableVirtualInterface(IO80211VirtualInterface *);                               // 420
    virtual bool requiresExplicitMBufRelease();                                                       // 421
    virtual bool flowIdSupported();                                                                   // 422
    virtual IO80211FlowQueueLegacy* requestFlowQueue(FlowIdMetadata const*);                          // 423
    virtual void releaseFlowQueue(IO80211FlowQueue *);                                                // 424
    virtual bool getLogPipes(CCPipe**, CCPipe**, CCPipe**);                                           // 425
    // Sequoia 15.7.5: slot 426 was thought to be PV padding but is actually
    // getControllerGlobalLogger() — IO80211ControllerMonitor::initWithControllerAndProvider
    // stores its return at monitor->ivars[0x10][0xdb0] and null-checks. NULL
    // → init fails → withControllerAndProvider returns NULL → createIOReporters
    // returns kIOReturnNoResources → IO80211Controller::start fails to start.
    // Subclasses must override and return a real CCLogStream* built via
    // CCLogStream::withPipeAndName (not the abstract CCStream factory).
    virtual void *getControllerGlobalLogger() { return nullptr; }                                                       // slot 426
    virtual void enableFeatureForLoggingFlags(unsigned long long);                                    // 427
    virtual IOReturn requestQueueSizeAndTimeout(unsigned short *, unsigned short *);                  // 428
    virtual IOReturn enablePacketTimestamping(void);                                                  // 429
    virtual IOReturn disablePacketTimestamping(void);                                                 // 430
    virtual UInt getPacketTSCounter();                                                                // 431

    // slot 432: getDriverTextLog — controller-wide log getter in 15.7.5.
    // IO80211ScanManager::commonInit calls this and stores the result as
    // CCLogStream* in scanIvars[0xe8]. createReportersAndLegend later does
    // fLogStream->shouldLog(1) — must be a real CCLogStream produced via
    // CCStream::withPipeAndName, not a raw CCLogPipe pointer.
    virtual void *getDriverTextLog();                                                                 // 432
    virtual UInt32 selfDiagnosticsReport(int,char const*,UInt);                                       // 433

    // Sequoia 15.7.5: slot 434 = getFaultReporterFromDriver. Apple's
    // IO80211Controller::findAndAttachToFaultReporter (called from start
    // after createIOReporters succeeds) does:
    //   ivars->_faultReporter = controller->vtable[slot 434]()
    //   if (!ivars->_faultReporter) panic("No ivars->_faultReporter" @line 3288)
    // Subclasses MUST override and return a real CCFaultReporter*.
    virtual void *getFaultReporterFromDriver() { return nullptr; }                                                      // slot 434
    virtual void allocIO80211RecursiveLock();                                                         // 435 [NEW]
    virtual UInt32 getDataQueueDepth(OSObject *);                                                     // 436
    virtual bool wasDynSARInFailSafeMode(void);                                                       // 437
    virtual void updateAdvisoryScoresIfNeed(void);                                                    // 438 [NEW]
    virtual UInt64 getAVCAdvisoryInfo(IO80211InterfaceAVCAdvisory *);                                 // 439
    virtual UInt32 getActionFramePoolCapacity(void);                                                  // 440 [NEW]
    virtual void *getPostOffice(void);                                                                // 441 [NEW]
    virtual void *CreatePostOffice(void);                                                             // 442 [NEW]
    virtual bool attachInterface(OSObject *,IOService *);                                             // 443
    virtual void detachInterface(OSObject *,bool);                                                    // 444
    virtual IO80211VirtualInterface* createVirtualInterface(ether_addr *,UInt);                       // 445
    virtual bool attachVirtualInterface(IO80211VirtualInterface **,ether_addr *,UInt,bool);           // 446
    virtual bool detachVirtualInterface(IO80211VirtualInterface *,bool);                              // 447

    OSMetaClassDeclareReservedUnused( IO80211Controller,  0);  // slot 448
    OSMetaClassDeclareReservedUnused( IO80211Controller,  1);  // slot 449
    OSMetaClassDeclareReservedUnused( IO80211Controller,  2);  // slot 450
    OSMetaClassDeclareReservedUnused( IO80211Controller,  3);  // slot 451
    OSMetaClassDeclareReservedUnused( IO80211Controller,  4);  // slot 452
    OSMetaClassDeclareReservedUnused( IO80211Controller,  5);  // slot 453
    OSMetaClassDeclareReservedUnused( IO80211Controller,  6);  // slot 454
    OSMetaClassDeclareReservedUnused( IO80211Controller,  7);  // slot 455
    OSMetaClassDeclareReservedUnused( IO80211Controller,  8);  // slot 456
    OSMetaClassDeclareReservedUnused( IO80211Controller,  9);  // slot 457
    OSMetaClassDeclareReservedUnused( IO80211Controller, 10);  // slot 458
    OSMetaClassDeclareReservedUnused( IO80211Controller, 11);  // slot 459
    OSMetaClassDeclareReservedUnused( IO80211Controller, 12);  // slot 460
    OSMetaClassDeclareReservedUnused( IO80211Controller, 13);  // slot 461
    OSMetaClassDeclareReservedUnused( IO80211Controller, 14);  // slot 462
    OSMetaClassDeclareReservedUnused( IO80211Controller, 15);  // slot 463

    virtual IOReturn setMulticastList(ether_addr const*, UInt);                                       // 464

protected:
    uint8_t  filler[0x128];
};
#endif // __IO80211_TARGET >= __MAC_15_0

#endif /* defined(KERNEL) && defined(__cplusplus) */

#endif /* !_IO80211CONTROLLER_H */
