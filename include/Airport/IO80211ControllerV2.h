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
// actual binary BootKernelExtensions-actual.kc (md5 ea9a509af28abc5c0c51217f83add983).
//
// Slot ordering verified against:
//   research/sequoia-port/diff/15.7.5-IO80211Controller-vtable.txt
//
// Key differences vs 15.7.4 KDK that the previous header was based on:
//   - slot 398: getWorkQueue is now CONST (debugStateInit removed in 15.7.5)
//   - slot 401: NEW getHardwareAddressForInterface
//   - slot 402: NEW useAppleRSNSupplicant
//   - slot 406: NEW apple80211_ioctl(IO80211SkywalkInterface*, ulong, void*, bool, bool)
//   - slot 407-409: NEW apple80211{Virtual,Skywalk}Request methods (×3)
//   - slot 410: PV padding (no func)
//   - slot 415-422: 8 PV slots (Apple removed several class methods, replaced with PVs)
//   - slot 423: NEW setVirtualHardwareAddress
//   - slot 426: requiresExplicitMBufRelease (not "getControllerGlobalLogger")
//   - slot 427: flowIdSupported (real Apple impl)
//   - slot 432: requestQueueSizeAndTimeout (was 428 in 15.7.4)
//   - slot 436: getDriverTextLog (was 432 in 15.7.4); this IS the CCLogStream getter
//   - slot 438: getFaultReporterFromDriver (was 434 in 15.7.4)
//   - slot 440: NEW isAssociatedToMovingNetwork
//   - slot 444-445: NEW apple80211_ioctl_get/set(IO80211SkywalkInterface*, ...)
//   - slot 447-448: NEW apple80211_ioctl_get/set(IO80211VirtualInterface*, ...)
//   - slot 453-454: NEW enable/disable(IO80211SkywalkInterface*)
//   - slot 471: postMessage (was 464 in 15.7.4 / current header)
//
// IOEthernetController extension at slots 331-332: Apple inserted
// allocatePacketNoWait + setHardwareAssists in 15.7. We can't add them to
// the parent IOEthernetController; instead we leave a placeholder gap
// (handled by MacKernelSDK header drift; vtable patcher tolerates this).
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
    // slot 359: Apple moved setHardwareAddress(IOEthernetAddress*) ownership
    // to IO80211Controller in 15.7.5 (was IOEthernetController in 14.x).
    // We MUST declare it here so the inherited vtable slot is bound by
    // mangled name __ZN17IO80211Controller18setHardwareAddressEPK17IOEthernetAddress
    // instead of __ZN20IOEthernetController... — OC's vtable patcher fails
    // when our slot's extern symbol is _RESERVED but Apple's parent slot is
    // a real method, AND the symbol class names diverge. AirportItlwm itself
    // overrides only the (void*, UInt32) variant; this slot inherits behavior
    // from IOEthernetController via plain virtual dispatch.
    virtual IOReturn setHardwareAddress(const IOEthernetAddress *) APPLE_KEXT_OVERRIDE;  // slot 359
    virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;             // slot 360
    virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;           // slot 362

    // SLOT 396 placeholder — see comment in 15.7.4 path above; same situation
    // in 15.7.5: Apple's vtable has a ___cxa_pure_virtual padding slot that
    // MacKernelSDK's IOEthernetController doesn't expose. Force one extra
    // vmethod here to keep AirportItlwm's vtable aligned with Apple.
    virtual void _seq_eth_ext_slot396_placeholder() {}                              // slot 396

    // --- IO80211Controller's own new vmethods, slot order matches 15.7.5 ---

    // slot 397: createWorkQueue MUST return IO80211WorkQueue* (not bool) —
    // Apple's IO80211Controller::start passes the return to
    // IO80211CommandGate::allocWithParams as the workqueue arg.
    virtual IO80211WorkQueue *createWorkQueue();                                    // slot 397
    virtual IO80211WorkQueue *getWorkQueue() const;                                 // slot 398 [now CONST in 15.7.5]
    virtual void requestPacketTx(void*, UInt);                                      // slot 399
    virtual IOCommandGate *getIO80211CommandGate() const;                           // slot 400 [now CONST in 15.7.5]
    virtual IOReturn getHardwareAddressForInterface(IOEthernetAddress *);           // slot 401 [NEW]
    virtual bool useAppleRSNSupplicant(IO80211VirtualInterface *);                  // slot 402 [NEW]
    virtual IO80211SkywalkInterface* getPrimarySkywalkInterface(void);              // slot 403
    virtual int bpfOutputPacket(OSObject *,UInt,mbuf_t m);                          // slot 404
    virtual SInt32 monitorModeSetEnabled(bool, UInt);                               // slot 405
    virtual SInt32 apple80211_ioctl(IO80211SkywalkInterface *,unsigned long,void *,bool,bool); // slot 406 [NEW]
    virtual SInt32 apple80211VirtualRequest(UInt,int,IO80211VirtualInterface *,void *);        // slot 407 [NEW]
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *);        // slot 408 [NEW]
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *,void *); // slot 409 [NEW overload]
    virtual bool isCommandProhibited(int) = 0;                                      // slot 410 [PV]
    virtual UInt32 hardwareOutputQueueDepth();                                      // slot 411
    virtual SInt32 performCountryCodeOperation(IO80211CountryCodeOp);               // slot 412
    virtual void dataLinkLayerAttachComplete();                                     // slot 413
    virtual SInt32 enableFeature(IO80211FeatureCode, void*);                        // slot 414

    // slots 415-422: Apple's vtable has 8 PV slots here in 15.7.5.
    // These were getDRIVER_VERSION..setGET_DEBUG_INFO in 15.7.4. In 15.7.5
    // they appear to be retained as PVs (driver-implemented IOCTL hooks).
    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;     // 415 [PV]
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;   // 416 [PV]
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *,apple80211_capability_data *) = 0;// 417 [PV]
    virtual IOReturn getPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;                // 418 [PV]
    virtual IOReturn setPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;                // 419 [PV]
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;  // 420 [PV]
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;  // 421 [PV]
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *,apple80211_debug_command *) = 0;    // 422 [PV]

    virtual SInt32 setVirtualHardwareAddress(IO80211VirtualInterface *,ether_addr *);                // 423 [NEW]
    virtual SInt32 enableVirtualInterface(IO80211VirtualInterface *);                                // 424
    virtual SInt32 disableVirtualInterface(IO80211VirtualInterface *);                               // 425

    // slot 426: requiresExplicitMBufRelease — Apple has its own impl in 15.7.5.
    // Earlier theory that this was "getControllerGlobalLogger" was wrong: that
    // was a 14.x slot inference; in 15.7.5 the global-logger getter is
    // getDriverTextLog at slot 436. Drivers may override to return false.
    virtual bool requiresExplicitMBufRelease();                                                       // 426
    virtual bool flowIdSupported();                                                                   // 427
    virtual IO80211FlowQueueLegacy* requestFlowQueue(FlowIdMetadata const*);                          // 428
    virtual void releaseFlowQueue(IO80211FlowQueue *);                                                // 429
    virtual bool getLogPipes(CCPipe**, CCPipe**, CCPipe**);                                           // 430
    virtual void enableFeatureForLoggingFlags(unsigned long long);                                    // 431
    virtual IOReturn requestQueueSizeAndTimeout(unsigned short *, unsigned short *);                  // 432
    virtual IOReturn enablePacketTimestamping(void);                                                  // 433
    virtual IOReturn disablePacketTimestamping(void);                                                 // 434
    virtual UInt getPacketTSCounter();                                                                // 435

    // slot 436: getDriverTextLog — this is the controller-wide log getter in
    // 15.7.5. IO80211ScanManager::commonInit calls this and stores the
    // result as CCLogStream* in scanIvars[0xe8]. Returning anything other
    // than a real CCLogStream produced via CCStream::withPipeAndName
    // → OSDynamicCast<CCLogStream> panics in createReportersAndLegend.
    virtual void *getDriverTextLog();                                                                 // 436
    virtual UInt32 selfDiagnosticsReport(int,char const*,UInt);                                       // 437

    // slot 438: getFaultReporterFromDriver — Apple stores this verbatim into
    // ivars+0x58 as CCFaultReporter*. Must return raw CCFaultReporter
    // produced via CCFaultReporter::withStreamWorkloop.
    virtual void *getFaultReporterFromDriver() = 0;                                                   // 438 [PV in Sequoia]

    virtual UInt32 getDataQueueDepth(OSObject *);                                                     // 439
    virtual bool isAssociatedToMovingNetwork(void);                                                   // 440 [NEW]
    virtual bool wasDynSARInFailSafeMode(void);                                                       // 441
    virtual void updateAdvisoryScoresIfNeed(void);                                                    // 442
    virtual UInt64 getAVCAdvisoryInfo(IO80211InterfaceAVCAdvisory *);                                 // 443
    virtual SInt32 apple80211_ioctl_get(IO80211SkywalkInterface *,void *, bool, bool);                // 444 [NEW]
    virtual SInt32 apple80211_ioctl_set(IO80211SkywalkInterface *,void *, bool, bool);                // 445 [NEW]
    virtual bool attachInterface(OSObject *,IOService *);                                             // 446
    virtual SInt32 apple80211_ioctl_get(IO80211VirtualInterface *,void *,bool,bool);                  // 447 [NEW]
    virtual SInt32 apple80211_ioctl_set(IO80211VirtualInterface *,void *,bool,bool);                  // 448 [NEW]
    virtual void detachInterface(OSObject *,bool);                                                    // 449
    virtual IO80211VirtualInterface* createVirtualInterface(ether_addr *,UInt);                       // 450
    virtual bool attachVirtualInterface(IO80211VirtualInterface **,ether_addr *,UInt,bool);           // 451
    virtual bool detachVirtualInterface(IO80211VirtualInterface *,bool);                              // 452
    virtual IOReturn enable(IO80211SkywalkInterface *);                                               // 453 [NEW]
    virtual IOReturn disable(IO80211SkywalkInterface *);                                              // 454 [NEW]

    OSMetaClassDeclareReservedUnused( IO80211Controller,  0);  // slot 455
    OSMetaClassDeclareReservedUnused( IO80211Controller,  1);  // slot 456
    OSMetaClassDeclareReservedUnused( IO80211Controller,  2);  // slot 457
    OSMetaClassDeclareReservedUnused( IO80211Controller,  3);  // slot 458
    OSMetaClassDeclareReservedUnused( IO80211Controller,  4);  // slot 459
    OSMetaClassDeclareReservedUnused( IO80211Controller,  5);  // slot 460
    OSMetaClassDeclareReservedUnused( IO80211Controller,  6);  // slot 461
    OSMetaClassDeclareReservedUnused( IO80211Controller,  7);  // slot 462
    OSMetaClassDeclareReservedUnused( IO80211Controller,  8);  // slot 463
    OSMetaClassDeclareReservedUnused( IO80211Controller,  9);  // slot 464
    OSMetaClassDeclareReservedUnused( IO80211Controller, 10);  // slot 465
    OSMetaClassDeclareReservedUnused( IO80211Controller, 11);  // slot 466
    OSMetaClassDeclareReservedUnused( IO80211Controller, 12);  // slot 467
    OSMetaClassDeclareReservedUnused( IO80211Controller, 13);  // slot 468
    OSMetaClassDeclareReservedUnused( IO80211Controller, 14);  // slot 469
    OSMetaClassDeclareReservedUnused( IO80211Controller, 15);  // slot 470

    virtual void postMessage(UInt,void *,unsigned long,UInt,void *);                                  // 471 [moved here]
    virtual IOReturn setMulticastList(ether_addr const*, UInt);                                       // 472

protected:
    uint8_t  filler[0x128];
};
#endif // __IO80211_TARGET >= __MAC_15_0

#endif /* defined(KERNEL) && defined(__cplusplus) */

#endif /* !_IO80211CONTROLLER_H */
