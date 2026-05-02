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
#include "Airport/IO80211FaultReporter.h"
#include "Airport/CCLogStream.h"

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
    
    virtual IO80211WorkQueue *getWorkQueue() override;
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
    
    virtual void *getFaultReporterFromDriver() override;

// Sequoia 新增的 6 个非 PV vmethod (debugStateInit, getPLATFORM_CONFIG,
// allocIO80211RecursiveLock, getActionFramePoolCapacity, getPostOffice,
// CreatePostOffice) 在 Apple's IO80211Family.kext IO80211Controller 里有真实
// 实现 (KDK nm 显示 T 符号). 我们 derived AirportItlwm **不要 override** 这些,
// 否则 Apple's super::start() 调用时拿到的是我们的 nullptr/空 stub, 内部
// dereference 失败 -> super::start 返回 false -> 整个 start 在 line 229 退出.
// 不 override 这些方法不会影响 vtable 计数, 因为非 PV slot 已被父类填充.

#if __IO80211_TARGET < __MAC_15_0
    virtual SInt32 apple80211_ioctl(IO80211SkywalkInterface *,unsigned long,void *, bool, bool) override;
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *) override;
    virtual SInt32 apple80211SkywalkRequest(UInt,int,IO80211SkywalkInterface *,void *,void *) override;
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
    // Slot 426 in Sequoia is a CCLogStream* getter (NOT handleCardSpecific —
    // that name was a wrong guess from old itlwm header). Apple's
    // IO80211ScanManager::commonInit at file 0x18d023 calls
    // controller->vtable[0xd40] (= slot 426) and stores the return as
    // CCLogStream* in scanIvars[0xe8]. ScanManager::createReportersAndLegend
    // later does fLogStream->shouldLog(1), which reads
    // ((CCLogStream*)x)->ivars[0x90][0x58] — if x is anything but a real
    // CCLogStream produced by CCStream::withPipeAndName + OSDynamicCast,
    // those offsets dereference into garbage and panic at corecapture+0x1b22b
    // (CR2=0x9 from the cmp dword [rax+8]).
    // We construct driverLogStream in initCCLogs (CCStream::withPipeAndName
    // on driverLogPipe with stream_type=0 → OSDynamicCast<CCLogStream>).
    virtual void *getControllerGlobalLogger() override {
        return driverLogStream;
    };
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
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *interface,apple80211_debug_command *data) override {
        XYLog("%s\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    
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
    // Sequoia: Apple's getFaultReporterFromDriver vtable slot must return an
    // IO80211FaultReporter* (not a CCStream*). PeerManager later calls
    // CCFaultReporter::registerCallbacks via vtable[0x120] on this pointer;
    // returning a CCStream caused the vtable slot to land on the wrong method
    // -> NULL+0x38 release deref panic. Wrap driverFaultReporter via
    // CCFaultReporter::withStreamWorkloop -> IO80211FaultReporter::allocWithParams.
    IO80211FaultReporter *io80211FaultReporter;
    // Sequoia: Apple's getControllerGlobalLogger (slot 426) must return a real
    // CCLogStream* obtained via CCStream::withPipeAndName + OSDynamicCast<CCLogStream>.
    CCLogStream *driverLogStream;
#endif
};

#endif /* AirportItlwmV2_hpp */
