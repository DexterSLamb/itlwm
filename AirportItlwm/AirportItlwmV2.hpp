//
//  AirportItlwmV2.hpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef AirportItlwmV2_hpp
#define AirportItlwmV2_hpp

#include "Apple80211.h"

#include "IOKit/network/IOGatedOutputQueue.h"
#include <libkern/c++/OSString.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <IOKit/IOFilterInterruptEventSource.h>

#include "ItlIwm.hpp"
#include "ItlIwx.hpp"
#include "ItlIwn.hpp"

#include "AirportItlwmEthernetInterface.hpp"
#include "Airport/CCDataStream.h"
#include "Airport/CCFaultReporter.h"
#include "Airport/CCLogStream.h"
#if __IO80211_TARGET >= __MAC_15_0
// IOSkywalkPacketBufferPool already pulled in via Airport/Apple80211.h above.
// We use minimal local headers for the Tx/Rx queue classes to avoid the
// MacKernelSDK <IOKit/skywalk/...> headers, which would re-declare
// IOSkywalkPacketBufferPool with different include guards (collision).
#include "Airport/IOSkywalkTxSubmissionQueue.h"
#include "Airport/IOSkywalkRxCompletionQueue.h"
#endif

enum
{
    kPowerStateOff = 0,
    kPowerStateOn,
    kPowerStateCount
};

#define kWatchDogTimerPeriod 1000

extern "C" {
const char *convertApple80211IOCTLToString(signed int cmd);
}

class AirportItlwm : public IO80211Controller {
    OSDeclareDefaultStructors(AirportItlwm)
#define IOCTL(REQ_TYPE, REQ, DATA_TYPE) \
if (REQ_TYPE == SIOCGA80211) { \
ret = get##REQ(interface, (struct DATA_TYPE* )data); \
} else { \
ret = set##REQ(interface, (struct DATA_TYPE* )data); \
}
    
#define IOCTL_GET(REQ_TYPE, REQ, DATA_TYPE) \
if (REQ_TYPE == SIOCGA80211) { \
ret = get##REQ(interface, (struct DATA_TYPE* )data); \
}
#define IOCTL_SET(REQ_TYPE, REQ, DATA_TYPE) \
if (REQ_TYPE == SIOCSA80211) { \
ret = set##REQ(interface, (struct DATA_TYPE* )data); \
}
#define FUNC_IOCTL(REQ, DATA_TYPE) \
FUNC_IOCTL_GET(REQ, DATA_TYPE) \
FUNC_IOCTL_SET(REQ, DATA_TYPE)
#define FUNC_IOCTL_GET(REQ, DATA_TYPE) \
IOReturn get##REQ(OSObject *object, struct DATA_TYPE *data);
#define FUNC_IOCTL_SET(REQ, DATA_TYPE) \
IOReturn set##REQ(OSObject *object, struct DATA_TYPE *data);
    
public:
    virtual bool init(OSDictionary *properties) override;
    virtual void free() override;
    virtual IOService* probe(IOService* provider, SInt32* score) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
#if __IO80211_TARGET < __MAC_15_0
    virtual IOReturn enable(IO80211SkywalkInterface *netif) override;
    virtual IOReturn disable(IO80211SkywalkInterface *netif) override;
#endif
    virtual IOReturn setHardwareAddress(const void *addr, UInt32 addrBytes) override;
    virtual IOReturn getHardwareAddress(IOEthernetAddress* addrP) override;
    virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;
    virtual IOReturn setPromiscuousMode(IOEnetPromiscuousMode mode) override;
    virtual IOReturn setMulticastMode(IOEnetMulticastMode mode) override;
    virtual IOReturn setMulticastList(IOEthernetAddress* addr, UInt32 len) override;
    virtual UInt32 getFeatures() const override;
    virtual const OSString * newVendorString() const override;
    virtual const OSString * newModelString() const override;
    virtual IOReturn selectMedium(const IONetworkMedium *medium) override;
#if __IO80211_TARGET >= __MAC_15_0
    virtual IO80211WorkQueue *createWorkQueue() override;
#else
    virtual bool createWorkQueue() override;
#endif
    virtual IONetworkInterface * createInterface() override;
    virtual bool configureInterface(IONetworkInterface *netif) override;
    virtual UInt32 outputPacket(mbuf_t, void * param) override;
#ifdef __PRIVATE_SPI__
    virtual IOReturn outputStart(IONetworkInterface *interface, IOOptionBits options) override;
    virtual IOReturn networkInterfaceNotification(
                        IONetworkInterface * interface,
                        uint32_t              type,
                        void *                  argument ) override;
#endif
    virtual bool setLinkStatus(
                               UInt32                  status,
                               const IONetworkMedium * activeMedium = 0,
                               UInt64                  speed        = 0,
                               OSData *                data         = 0) override;
    static IOReturn setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);
    void updateLQMIfChanged();

    static IOReturn tsleepHandler(OSObject* owner, void* arg0 = 0, void* arg1 = 0, void* arg2 = 0, void* arg3 = 0);
    static void eventHandler(struct ieee80211com *, int, void *);
    IOReturn enableAdapter(IONetworkInterface *netif);
    void disableAdapter(IONetworkInterface *netif);
    bool initCCLogs();
    
#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia 15.7.5: getWorkQueue is now CONST. Source RE evidence:
    // 15.7.5-IO80211Controller-vtable.txt slot 398 = __ZNK17IO80211Controller12getWorkQueueEv
    virtual IO80211WorkQueue *getWorkQueue() const override;
#else
    virtual IO80211WorkQueue *getWorkQueue() override;
#endif
    virtual bool requiresExplicitMBufRelease() override {
        return false;
    }
    virtual bool flowIdSupported() override {
        return false;
    }
    virtual SInt32 monitorModeSetEnabled(bool, UInt) override {
        return kIOReturnSuccess;
    }
    virtual IOReturn requestQueueSizeAndTimeout(unsigned short *queue, unsigned short *timeout) override {
        XYLog("%s\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    
    virtual bool getLogPipes(CCPipe**, CCPipe**, CCPipe**) override;

#if __IO80211_TARGET < __MAC_15_0
    virtual void *getFaultReporterFromDriver() override;
    virtual SInt32 apple80211_ioctl(IO80211SkywalkInterface *,unsigned long,void *, bool, bool) override;
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *) override;
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *,void *) override;
#else
    // Sequoia 15.7.5: parent IO80211Controller has none of:
    //   - apple80211_ioctl_get/set on Skywalk/Virtual interfaces
    //   - apple80211_ioctl(IO80211SkywalkInterface*, ...)
    //   - apple80211{Virtual,Skywalk}Request
    //   - getFaultReporterFromDriver (now a PV at slot 434, used differently)
    // These methods don't exist as virtual slots in Apple's 15.7.5 IO80211Controller
    // vtable, so we don't override them in our derived class. The IO80211InfraInterface
    // path handles the IOCTL routing in 15.7.5.
#endif

    bool createMediumTables(const IONetworkMedium **primary);
    void releaseAll();
    void watchdogAction(IOTimerEventSource *timer);
    
    virtual SInt32 enableFeature(IO80211FeatureCode, void*) override;
    virtual bool isCommandProhibited(int command) override {
//        if (!ml_at_interrupt_context())
//            XYLog("%s %s\n", __FUNCTION__, convertApple80211IOCTLToString(command));
        return false;
    };
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth: slot 436 = __ZN17IO80211Controller16getDriverTextLogEv
    // is the controller-wide log getter. Apple's IO80211ScanManager::commonInit
    // calls this and stores the return as CCLogStream* in scanIvars[0xe8].
    // createReportersAndLegend later does fLogStream->shouldLog(1) which
    // reads ((CCLogStream*)x)->ivars[0x90][0x58] — if x isn't a real CCLogStream
    // produced via CCStream::withPipeAndName + OSDynamicCast, those offsets
    // dereference into garbage (corecapture+0x1b22b panic, CR2=0x9).
    //
    // The previous implementation overrode "getControllerGlobalLogger" at
    // a guessed slot 426, but slot 426 in 15.7.5 is requiresExplicitMBufRelease.
    // We override getDriverTextLog (slot 436) instead — same purpose, correct slot.
    // Sequoia 15.7.5 — return REAL CCLogStream* (built via
    // CCLogStream::withPipeAndName subclass factory in initCCLogs). Both
    // slots 426 (getControllerGlobalLogger) and 432 (getDriverTextLog) need
    // a valid CCLogStream pointer:
    //   - slot 426: IO80211ControllerMonitor::initWithControllerAndProvider
    //     stores it at monitor->ivars[0x10][0xdb0] and null-checks. NULL →
    //     init fails → withControllerAndProvider returns NULL → createIOReporters
    //     fails → IO80211Controller::start returns false. This is what was
    //     blocking start (commit 21e5c21 trace_step = FAIL_super_start).
    //   - slot 432: IO80211ScanManager::commonInit + findAndAttachToFaultReporter
    //     consume this; null-check exists at the latter, but ScanManager
    //     deref's the result later for shouldLog calls.
    virtual void *getControllerGlobalLogger() override { return driverLogStream; }
    virtual void *getDriverTextLog() override { return driverLogStream; }

    // Sequoia 15.7.5: IO80211Controller::postMessage(uint, void*, ulong, uint, void*)
    // does NOT exist in the Apple binary (no vtable slot, no symbol). Removing
    // the override entirely. Real postMessage path in 15.7.5 uses the typed
    // overload IO80211Controller::postMessage(IO80211SkywalkInterface*, uint,
    // void*, ulong, bool) which is a non-virtual member function we don't need
    // to override either — Apple's framework calls it directly.
#else
    virtual SInt32 handleCardSpecific(IO80211SkywalkInterface *,unsigned long,void *,bool) override {
        XYLog("%s\n", __FUNCTION__);
        return 0;
    };
#endif
    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *interface,apple80211_version_data *data) override {
        XYLog("%s\n", __FUNCTION__);
        return getDRIVER_VERSION((OSObject *)interface, data);
    };
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *interface,apple80211_version_data *data) override {
        XYLog("%s\n", __FUNCTION__);
        return getHARDWARE_VERSION((OSObject *)interface, data);
    };
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *interface,apple80211_capability_data *data) override {
//        XYLog("%s\n", __FUNCTION__);
        return getCARD_CAPABILITIES((OSObject *)interface, data);
    }
    virtual IOReturn getPOWER(IO80211SkywalkInterface *interface,apple80211_power_data *data) override {
//        XYLog("%s\n", __FUNCTION__);
        return getPOWER((OSObject *)interface, data);
    }
    virtual IOReturn setPOWER(IO80211SkywalkInterface *interface,apple80211_power_data *data) override {
//        XYLog("%s\n", __FUNCTION__);
        return setPOWER((OSObject *)interface, data);
    }
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *interface,apple80211_country_code_data *data) override {
//        XYLog("%s\n", __FUNCTION__);
        return getCOUNTRY_CODE((OSObject *)interface, data);
    }
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *interface,apple80211_country_code_data *data) override {
//        XYLog("%s\n", __FUNCTION__);
        return setCOUNTRY_CODE((OSObject *)interface, data);
    }
#if __IO80211_TARGET < __MAC_15_0
    // Sequoia 15.7.5: setGET_DEBUG_INFO removed from IO80211Controller vtable.
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *interface,apple80211_debug_command *data) override {
        XYLog("%s\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
#endif
    
    //scan
    static void fakeScanDone(OSObject *owner, IOTimerEventSource *sender);
    
    //-----------------------------------------------------------------------
    // Power management support.
    //-----------------------------------------------------------------------
    virtual IOReturn registerWithPolicyMaker( IOService * policyMaker ) override;
    virtual IOReturn setPowerState( unsigned long powerStateOrdinal,
                                    IOService *   policyMaker) override;
    virtual IOReturn setWakeOnMagicPacket( bool active ) override;
    void setPowerStateOff(void);
    void setPowerStateOn(void);
    void unregistPM();
    bool initPCIPowerManagment(IOPCIDevice *provider);

    FUNC_IOCTL_GET(CARD_CAPABILITIES, apple80211_capability_data)
    FUNC_IOCTL(POWER, apple80211_power_data)
    FUNC_IOCTL_GET(DRIVER_VERSION, apple80211_version_data)
    FUNC_IOCTL_GET(HARDWARE_VERSION, apple80211_version_data)
    FUNC_IOCTL(COUNTRY_CODE, apple80211_country_code_data)
    
public:
    IOInterruptEventSource* fInterrupt;
    IOTimerEventSource *watchdogTimer;
    IOPCIDevice *pciNub;
    IONetworkStats *fpNetStats;
    AirportItlwmEthernetInterface *bsdInterface;
    IO80211SkywalkInterface *fNetIf;
    IOWorkLoop *fWatchdogWorkLoop;
    ItlHalService *fHalService;
    unsigned long long fLastReportedLQM;
    
    //IO80211
    uint8_t power_state;
    struct ieee80211_node *fNextNodeToSend;
    bool fScanResultWrapping;
    IOTimerEventSource *scanSource;
    
    u_int32_t current_authtype_lower;
    u_int32_t current_authtype_upper;
    UInt64 currentSpeed;
    UInt32 currentStatus;
    bool disassocIsVoluntary;
    char geo_location_cc[3];
    
    //pm
    thread_call_t powerOnThreadCall;
    thread_call_t powerOffThreadCall;
    UInt32 pmPowerState;
    IOService *pmPolicyMaker;
    UInt8 pmPCICapPtr;
    bool magicPacketEnabled;
    bool magicPacketSupported;
    
    //AWDL
    uint8_t *syncFrameTemplate;
    uint32_t syncFrameTemplateLength;
    uint8_t awdlBSSID[6];
    uint32_t awdlSyncState;
    uint32_t awdlElectionId;
    uint32_t awdlPresenceMode;
    uint16_t awdlMasterChannel;
    uint16_t awdlSecondaryMasterChannel;
    uint8_t *roamProfile;
    struct apple80211_btc_profiles_data *btcProfile;
    struct apple80211_btc_config_data btcConfig;
    uint32_t btcMode;
    uint32_t btcOptions;
    bool awdlSyncEnable;
    
    CCPipe *driverLogPipe;
    CCPipe *driverDataPathPipe;
    CCPipe *driverSnapshotsPipe;
    
    CCStream *driverFaultReporter;
#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia 15.7.5 RE: IO80211FaultReporter no longer exists. Apple's
    // getFaultReporterFromDriver vtable slot expects raw CCFaultReporter*.
    // Created via CCFaultReporter::withStreamWorkloop(CCDataStream, IOWorkLoop).
    CCFaultReporter *ccFaultReporter;
    // Sequoia 15.7.5 slot 436 getDriverTextLog must return a real CCLogStream*
    // obtained via CCStream::withPipeAndName + OSDynamicCast<CCLogStream>.
    CCLogStream *driverLogStream;

    // Sequoia 15.x: Skywalk TX/RX pool + queue setup is REQUIRED for the
    // BSD ifnet to be created via IOSkywalkNetworkBSDClient. Allocated in
    // start(), released in releaseAll(). Stub callbacks (skywalkTxAction /
    // skywalkRxAction) just return count — actual datapath remains the
    // legacy IOEthernetInterface path; the pools/queues exist solely to
    // satisfy the framework's registerEthernetInterface contract.
    IOSkywalkPacketBufferPool   *fTxPool;
    IOSkywalkPacketBufferPool   *fRxPool;
    IOSkywalkTxSubmissionQueue  *fTxQueue;
    IOSkywalkRxCompletionQueue  *fRxQueue;
#endif
};

#endif /* AirportItlwmV2_hpp */
