//
//  IO80211SkywalkInterface.h
//  IO80211Family
//
//  Created by 钟先耀 on 2019/10/18.
//  Copyright © 2019 钟先耀. All rights reserved.
//

#ifndef _IO80211SKYWALK_H
#define _IO80211SKYWALK_H

#include <Availability.h>
#include "IOSkywalkEthernetInterface.h"

// This is necessary, because even the latest Xcode does not support properly targeting 11.0.
#ifndef __IO80211_TARGET
#error "Please define __IO80211_TARGET to the requested version"
#endif

class TxSubmissionDequeueStats;
class TxCompletionEnqueueStats;
class IO80211NetworkPacket;
class PacketSkywalkScratch;
// Sequoia 15.7.5 mangling: Apple's IO80211SkywalkInterface methods take
// `IO80211FlowQueueHash` mangled as struct ("20IO80211FlowQueueHash"), not the
// typedef'd UInt64 alias ("y"). Use a single-member struct so ABI is identical
// (one 8-byte register) but mangling matches Apple's BootKC export.
#if __IO80211_TARGET >= __MAC_15_0
struct IO80211FlowQueueHash { UInt64 hash; };
#else
typedef UInt64 IO80211FlowQueueHash;
#endif
class IO80211Peer;
class CCPipe;
class IO80211APIUserClient;
struct apple80211_wme_ac;
struct apple80211_interface_availability;
struct apple80211_cca_report;
struct apple80211_stat_report;
struct apple80211_chip_counters_tx;
struct apple80211_chip_counters_rx;
struct apple80211_chip_error_counters_tx;
struct apple80211_ManagementInformationBasedot11_counters;
struct apple80211_lteCoex_report;
struct apple80211_frame_counters;
struct userPrintCtx;
struct apple80211_lqm_summary;
struct apple80211_infra_specific_stats;
struct apple80211_data_path_interface_stats;
struct apple80211_data_path_peer_stats;
struct apple80211_latency_all_ac;
class IOMemoryDescriptor;
class IO80211PeerManager;
class IO80211Controller;

struct TxPacketRequest {
    uint16_t    unk1;       // 0
    uint16_t    t;       // 2
    uint16_t    mU;       // 4
    uint16_t    mM;       // 6
    uint16_t    pkt_cnt;
    uint16_t    unk2;
    uint16_t    unk3;
    uint16_t    unk4;
    uint32_t    pad;
    mbuf_t      bufs[8];    // 18
    uint32_t    reqTx;
};

static_assert(sizeof(struct TxPacketRequest) == 0x60, "TxPacketRequest size error");

class IO80211SkywalkInterface : public IOSkywalkEthernetInterface {
    OSDeclareAbstractStructors(IO80211SkywalkInterface)

public:
    
    virtual bool init() APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual IOReturn configureReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn updateReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPowerState(
        unsigned long powerStateOrdinal,
        IOService *   whatDevice ) APPLE_KEXT_OVERRIDE;
    virtual unsigned long maxCapabilityForDomainState( IOPMPowerFlags domainState ) APPLE_KEXT_OVERRIDE;
    virtual unsigned long initialPowerStateForDomainState( IOPMPowerFlags domainState ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn enable(UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn disable(UInt) APPLE_KEXT_OVERRIDE;
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams *,sockaddr_dl **) APPLE_KEXT_OVERRIDE;
    virtual bool prepareBSDInterface(ifnet_t, UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn processBSDCommand(ifnet_t, UInt, void *) APPLE_KEXT_OVERRIDE;
    virtual SInt32 setInterfaceEnable(bool) APPLE_KEXT_OVERRIDE;
    virtual SInt32 setRunningState(bool) APPLE_KEXT_OVERRIDE;
    virtual IOReturn handleChosenMedia(UInt) APPLE_KEXT_OVERRIDE;
    virtual void *getSupportedMediaArray(UInt *,UInt *) APPLE_KEXT_OVERRIDE;
    virtual UInt32 getFeatureFlags(void) APPLE_KEXT_OVERRIDE;
    virtual const char *classNameOverride(void) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPromiscuousModeEnable(bool, UInt) APPLE_KEXT_OVERRIDE;
    virtual void *createPeerManager(void);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth slot 355: createPeer.
    // Earlier headers omitted this and shifted postMessage..forwardInfraRelayPackets
    // by -1.
    virtual void *createPeer(unsigned char const*, IO80211PeerManager *);
#endif
    virtual void postMessage(UInt,void *,unsigned long,bool);
    virtual IOReturn reportDataPathEvents(UInt,void *,unsigned long,bool);
    virtual IOReturn recordOutputPackets(TxSubmissionDequeueStats *,TxSubmissionDequeueStats *);
    virtual IOReturn recordOutputPacket(apple80211_wme_ac,int,int);
    virtual void logTxPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,apple80211_wme_ac,bool);
#if __IO80211_TARGET >= __MAC_15_0
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool,bool);
#else
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool);
#endif
    virtual IOReturn recordCompletionPackets(TxCompletionEnqueueStats *,TxCompletionEnqueueStats *);
#if __IO80211_TARGET >= __MAC_15_0
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *,bool);
#else
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *);
#endif
    virtual IOReturn forwardInfraRelayPackets(IO80211NetworkPacket*, ether_header*);
    virtual void logSkywalkTxReqPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,bool);
    virtual SInt64 pendingPackets(unsigned char);
    virtual SInt64 packetSpace(unsigned char);
    virtual bool isChipInterfaceReady(void);
    virtual bool isDebounceOnGoing(void);
    virtual bool setLinkState(IO80211LinkState,UInt,bool debounceTimeout = 30,UInt code = 0);
    virtual IO80211LinkState linkState(void);
    virtual void setScanningState(UInt,bool,apple80211_scan_data *,int);
    virtual void setDataPathState(bool);
    virtual void *getScanManager(void);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth slot 375: getController.
    virtual IO80211Controller *getController(void);
#endif
    virtual void updateLinkParameters(apple80211_interface_availability *);
    virtual void updateInterfaceCoexRiskPct(unsigned long long);
    virtual void setLQM(unsigned long long);
    virtual void updateLinkStatus(void);
    virtual void updateLinkStatusGated(void);
    virtual void setInterfaceExtendedCCA(apple80211_channel,apple80211_cca_report *);
    virtual void setInterfaceCCA(apple80211_channel,int);
    virtual void setInterfaceNF(apple80211_channel,long long);
    virtual void setInterfaceOFDMDesense(apple80211_channel,long long);
    virtual void removePacketQueue(IO80211FlowQueueHash *);
    virtual void setDebugFlags(unsigned long long,UInt);
    virtual SInt64 debugFlags(void);
    virtual void setInterfaceChipCounters(apple80211_stat_report *,apple80211_chip_counters_tx *,apple80211_chip_error_counters_tx *,apple80211_chip_counters_rx *);
    virtual void setInterfaceMIBdot11(apple80211_stat_report *,apple80211_ManagementInformationBasedot11_counters *);
    virtual void setFrameStats(apple80211_stat_report *,apple80211_frame_counters *);
#if __IO80211_TARGET >= __MAC_14_4
    virtual void setInfraSpecificFrameStats(apple80211_stat_report *,apple80211_infra_specific_stats *);
#endif
    virtual SInt64 getWmeTxCounters(unsigned long long *);
#if __IO80211_TARGET < __MAC_15_0
    // 15.7.5 ground truth: setEnabledBySystem/enabledBySystem/willRoam moved
    // up the hierarchy to IO80211InfraInterface (slots 392-394). Keep them
    // here only for the Sonoma 14.x path. Source:
    // research/sequoia-port/diff/15.7.5-IO80211InfraInterface-vtable.txt
    virtual void setEnabledBySystem(bool);
    virtual bool enabledBySystem(void);
    virtual bool willRoam(ether_addr *,UInt);
#endif
    virtual void setPeerManagerLogFlag(UInt,UInt,UInt);
    virtual void setWoWEnabled(bool);
    virtual bool wowEnabled(void);
    virtual void printDataPath(userPrintCtx *);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth slot 397: getDataQueueDepth.
    virtual UInt64 getDataQueueDepth(void);
#endif
    virtual bool findOrCreateFlowQueue(IO80211FlowQueueHash);
    virtual UInt64 findOrCreateFlowQueueWithCache(IO80211FlowQueueHash,bool *);
    virtual UInt64 findExistingFlowQueue(IO80211FlowQueueHash);
    virtual void removePacketQueue(IO80211FlowQueueHash const*);
    virtual void flushPacketQueues(void);
    virtual void cachePeer(ether_addr *,UInt *);
    virtual bool shouldLog(unsigned long long);
    virtual void vlogDebug(unsigned long long,char const*,va_list);
    virtual void vlogDebugBPF(unsigned long long,char const*,va_list);
    virtual UInt64 createLinkQualityMonitor(IO80211Peer *,IOService *);
    virtual void releaseLinkQualityMonitor(IO80211Peer *);
    virtual void *getP2PSkywalkPeerMgr(void);
    virtual bool isCommandProhibited(int);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth: slot 411 owns findPeer in IO80211SkywalkInterface
    // (research/sequoia-port/diff/15.7.5-IO80211SkywalkInterface-vtable-REAL.txt).
    // Earlier headers omitted this and shifted setNotificationProperty..getPacketPool
    // by -1, which broke OC vtable patching for several InfraInterface symbols.
    virtual void *findPeer(ether_addr &);
#endif
    virtual void setNotificationProperty(OSSymbol const*,OSObject const*);
    virtual void *getWorkerMatchingDict(OSString *);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth slot 414: init(IOService*, ether_addr*) — extra
    // ether_addr* argument was added in Sequoia.
    virtual bool init(IOService *, ether_addr *);
#else
    virtual bool init(IOService *);
#endif
    virtual bool isInterfaceEnabled(void);
    virtual ether_addr *getSelfMacAddr(void);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth slot 417: ___cxa_pure_virtual in base
    // IO80211SkywalkInterface; Apple's concrete subclass AppleBCMWLANSkywalkInterface
    // fills it with setMacAddress(ether_addr&) returning IOReturn.
    // Source: research/sequoia-port/kdk-vtable-dump/applebcmwlan-vs-base.txt
    //   ZTV byte 0xd08 slot 417: base = ___cxa_pure_virtual,
    //   Apple = AppleBCMWLANSkywalkInterface::setMacAddress(ether_addr&)
    // We provide a default impl returning kIOReturnUnsupported so the slot is
    // owned by us (no kxld pure-virtual symbol resolution) and any caller that
    // dispatches into this slot gets a defined non-success error rather than
    // garbage. Subclasses may override if they ever support runtime MAC change.
    virtual IOReturn setMacAddress(ether_addr &) { return kIOReturnUnsupported; }
#else
    virtual void setSelfMacAddr(ether_addr *);
#endif
    virtual void *getPacketPool(OSString *);
    // Sequoia 15.7.5: Apple's IO80211SkywalkInterface::getLogger() is const,
    // returns CCLogStream* read from ivars[0x110]+0x78 (set by Apple's start
    // via CCStream::withPipeAndName). Apple Glue::initWithOptions @0x1a17f
    // hard-checks this returns non-NULL or fails, leading to freeResources
    // CR2=0 panic on uninitialized list head.
    // Returning NULL from our override (previous behavior) was the bug; we
    // now provide a default `nullptr` impl in the abstract base, AND require
    // the concrete subclass (AirportItlwmSkywalkInterface) to override with
    // the controller's driverLogStream that initCCLogs created.
    // Slot 419 (ZTV byte 0xd18, vptr byte 0xd08).
#if __IO80211_TARGET >= __MAC_15_0
    virtual void *getLogger() const { return nullptr; }
#else
    virtual void *getLogger(void);
#endif
    virtual IOReturn handleSIOCSIFADDR(void);
    virtual IOReturn debugHandler(apple80211_debug_command *);
    virtual void statsDump(void);
    virtual void powerOnNotification(void);
    virtual void powerOffNotification(void);
    virtual UInt64 getTxQueueDepth(void);
    virtual UInt64 getRxQueueCapacity(void);
    virtual void updateRxCounter(unsigned long long);
    virtual void *getMultiCastQueue(void);
#if __IO80211_TARGET < __MAC_15_0
    // Sonoma-only: getCurrentBssid was a real IO80211SkywalkInterface
    // vmethod prior to Sequoia. In 15.7.5 the slot got recycled to
    // getAssocState — see research/sequoia-port/diff/15.7.5-IO80211SkywalkInterface-vtable-REAL.txt
    // (slot 429 = __ZN23IO80211SkywalkInterface13getAssocStateEv, no getCurrentBssid).
    virtual void *getCurrentBssid(void);
#endif
    virtual int getAssocState(void);
    virtual void notifyQueueState(apple80211_wme_ac,unsigned short);
    virtual int getTxHeadroom(void);
    virtual void *getRxCompQueue(void);
    virtual void *getTxCompQueue(void);
    virtual void *getTxSubQueue(apple80211_wme_ac);
    virtual void *getTxPacketPool(void);
    virtual void *getRxPacketPool(void);
    virtual void enableDatapath(void);
    virtual void disableDatapath(void);
    virtual int getNumTxQueues(void);
    virtual void *getLQMSummary(apple80211_lqm_summary *);
    virtual int getEventPipeSize(void);
    virtual UInt64 createEventPipe(IO80211APIUserClient *);
    virtual void destroyEventPipe(IO80211APIUserClient *);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth (research/sequoia-port/diff/15.7.5-IO80211SkywalkInterface-vtable-REAL.txt):
    // setUserBufferInfo (slot 444) was inserted between destroyEventPipe (443)
    // and postMessageIOUC (445). The previous header omitted it, shifting
    // postMessageIOUC..getRingMD by -1 vs Apple.
    virtual IOReturn setUserBufferInfo(IOMemoryDescriptor *, unsigned long long);
#endif
    virtual void postMessageIOUC(char const*,UInt,void *,unsigned long);
    virtual bool isIOUCPipeOpened(void);
    virtual void *getRingMD(IO80211APIUserClient *,unsigned long long);
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth slots 448-461: methods that exist in Sequoia's
    // IO80211SkywalkInterface vtable but were never declared in our header.
    // Without these, AirportItlwmSkywalkInterface's vtable ends 14 slots
    // short of Apple's, breaking inheritance for IO80211InfraInterface
    // overrides at slots 462-466 and putting our IO80211InfraProtocol PV
    // pad slots at the wrong offsets.
    virtual void attachPeer(ether_addr *);
    virtual void detachPeer(ether_addr *);
    virtual void setDebugTrafficReport(bool);
    virtual IOReturn getDataPathInterfaceStats(apple80211_data_path_interface_stats *);
    virtual IOReturn getDataPathPeerStats(apple80211_data_path_peer_stats *);
    virtual unsigned long long getLastQueuePacketTime(ether_addr *);
    virtual unsigned long long getLastRxUnicastLinkActivityTime(ether_addr *);
    virtual void updateInterfaceDataStats(apple80211_data_path_interface_stats *);
    virtual void updatePeerDataStats(apple80211_data_path_peer_stats *);
    virtual void logTxLatency(unsigned char *, UInt, unsigned long long);
    virtual void logRxLatency(UInt, unsigned long long);
    virtual void getNClearTxRxLatency(apple80211_latency_all_ac *, apple80211_latency_all_ac *);
    virtual void getLastTxTimeStamp(unsigned long long &);
    virtual void getLastRxTimeStamp(unsigned long long &);
#endif

public:
    OSString *setInterfaceRole(UInt role);
    void *setInterfaceId(UInt id);
    int getInterfaceRole();
    
public:
#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia 15.7.4 KDK ground truth: IO80211SkywalkInterface size = 0x118 total
    // (parent IOSkywalkEthernetInterface = 0x110, +0x110 holds an 8-byte ivars
    // pointer). Original itlwm header had _data[0x118] -> total 0x110+0x118=0x228,
    // which oversizes the instance and shifts AirportItlwmSkywalkInterface ivars.
    // When IONetworkingFamily later stores callback function pointers into
    // ivars at fixed offsets, our oversized layout causes ifnet ioctl calls
    // to dereference into AirportItlwmSkywalkInterface's own data, jumping to
    // non-executable addresses (NX fault, e.g. into IO80211InfraProtocol::gMetaClass).
    char _data[0x8];
#else
    // Sonoma 14.x: original size from itlwm. The Sonoma stack has different
    // ivar handling; do NOT shrink for that target. Verified working with v2.4.0 fix.
    char _data[0x118];
#endif
};

#if __IO80211_TARGET >= __MAC_15_0
static_assert(sizeof(IO80211SkywalkInterface) == 0x118, "IO80211SkywalkInterface size mismatch with Sequoia KDK");
#endif

#endif /* _IO80211SKYWALK_H */
