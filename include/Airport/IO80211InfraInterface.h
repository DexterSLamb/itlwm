//
//  IO80211InfraInterface.h
//  itlwm
//
//  Created by qcwap on 2023/6/12.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef IO80211InfraInterface_h
#define IO80211InfraInterface_h

struct apple80211_wcl_advisory_info;
struct apple80211_wcl_tx_rx_latency;

class IO80211InfraInterface : public IO80211SkywalkInterface {
    OSDeclareAbstractStructors(IO80211InfraInterface)
    
public:
    virtual bool init() APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual IOReturn configureReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn updateReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *) APPLE_KEXT_OVERRIDE;
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams *,sockaddr_dl **) APPLE_KEXT_OVERRIDE;
    virtual bool prepareBSDInterface(ifnet_t, UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn processBSDCommand(ifnet_t, UInt, void *) APPLE_KEXT_OVERRIDE;
    virtual SInt32 setInterfaceEnable(bool) APPLE_KEXT_OVERRIDE;
    virtual UInt getHardwareAssists(void) APPLE_KEXT_OVERRIDE;
    virtual bool bpfTap(UInt,UInt) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET < __MAC_15_0
    virtual void getHardwareAddress(ether_addr *) APPLE_KEXT_OVERRIDE;
    virtual void setHardwareAddress(ether_addr *) APPLE_KEXT_OVERRIDE;
#endif
    virtual void postMessage(UInt,void *,unsigned long,bool) APPLE_KEXT_OVERRIDE;
    virtual IOReturn recordOutputPackets(TxSubmissionDequeueStats *,TxSubmissionDequeueStats *) APPLE_KEXT_OVERRIDE;
    virtual void logTxPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,apple80211_wme_ac,bool) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_15_0
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool,bool) APPLE_KEXT_OVERRIDE;
#else
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool) APPLE_KEXT_OVERRIDE;
#endif
    virtual IOReturn recordCompletionPackets(TxCompletionEnqueueStats *,TxCompletionEnqueueStats *) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_15_0
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *,bool) APPLE_KEXT_OVERRIDE;
#else
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *) APPLE_KEXT_OVERRIDE;
#endif
    virtual SInt64 pendingPackets(unsigned char) APPLE_KEXT_OVERRIDE;
    virtual SInt64 packetSpace(unsigned char) APPLE_KEXT_OVERRIDE;
    virtual bool isDebounceOnGoing(void) APPLE_KEXT_OVERRIDE;
    virtual bool setLinkState(IO80211LinkState,UInt,bool debounceTimeout = 30,UInt code = 0) APPLE_KEXT_OVERRIDE;
    virtual IO80211LinkState linkState(void) APPLE_KEXT_OVERRIDE;
    virtual void setScanningState(UInt,bool,apple80211_scan_data *,int) APPLE_KEXT_OVERRIDE;
    virtual void setDataPathState(bool) APPLE_KEXT_OVERRIDE;
    virtual void *getScanManager(void) APPLE_KEXT_OVERRIDE;
    virtual void updateLinkParameters(apple80211_interface_availability *) APPLE_KEXT_OVERRIDE;
    virtual void updateInterfaceCoexRiskPct(unsigned long long) APPLE_KEXT_OVERRIDE;
    virtual void setLQM(unsigned long long) APPLE_KEXT_OVERRIDE;
    virtual void updateLinkStatus(void) APPLE_KEXT_OVERRIDE;
    virtual void updateLinkStatusGated(void) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceExtendedCCA(apple80211_channel,apple80211_cca_report *) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceCCA(apple80211_channel,int) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceNF(apple80211_channel,long long) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceOFDMDesense(apple80211_channel,long long) APPLE_KEXT_OVERRIDE;
    virtual void setDebugFlags(unsigned long long,UInt) APPLE_KEXT_OVERRIDE;
    virtual SInt64 debugFlags(void) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceChipCounters(apple80211_stat_report *,apple80211_chip_counters_tx *,apple80211_chip_error_counters_tx *,apple80211_chip_counters_rx *) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceMIBdot11(apple80211_stat_report *,apple80211_ManagementInformationBasedot11_counters *) APPLE_KEXT_OVERRIDE;
    virtual void setFrameStats(apple80211_stat_report *,apple80211_frame_counters *) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_14_4
    virtual void setInfraSpecificFrameStats(apple80211_stat_report *,apple80211_infra_specific_stats *) APPLE_KEXT_OVERRIDE;
#endif
    virtual SInt64 getWmeTxCounters(unsigned long long *) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET < __MAC_15_0
    // Sonoma-only: setEnabledBySystem / enabledBySystem / willRoam exist on
    // IO80211SkywalkInterface in Sonoma (overridden by InfraInterface). In
    // Sequoia 15.7.5 they were removed entirely from the IO80211 family
    // (verified absent via `nm` on the real Sequoia BootKC).
    virtual void setEnabledBySystem(bool) APPLE_KEXT_OVERRIDE;
    virtual bool enabledBySystem(void) APPLE_KEXT_OVERRIDE;
    virtual bool willRoam(ether_addr *,UInt) APPLE_KEXT_OVERRIDE;
#endif
    virtual void setPeerManagerLogFlag(UInt,UInt,UInt) APPLE_KEXT_OVERRIDE;
    virtual void setWoWEnabled(bool) APPLE_KEXT_OVERRIDE;
    virtual bool wowEnabled(void) APPLE_KEXT_OVERRIDE;
    virtual UInt64 createLinkQualityMonitor(IO80211Peer *,IOService *) APPLE_KEXT_OVERRIDE;
    virtual void releaseLinkQualityMonitor(IO80211Peer *) APPLE_KEXT_OVERRIDE;
    virtual int getAssocState(void) APPLE_KEXT_OVERRIDE;
    virtual void *getLQMSummary(apple80211_lqm_summary *) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_15_0
    // 15.7.5 ground truth (research/sequoia-port/diff/15.7.5-IO80211InfraInterface-vtable-REAL.txt):
    //   slot 462: setLinkStateInternal (4 args — Sonoma 14.8.5 has a 5-arg
    //             variant with apple80211_link_changed_event_data&, but
    //             Sequoia removed that overload)
    //   slot 463: setCurrentApAddress (NEW — not setCurrentBssid)
    //   slot 464: setWCL_ADVISORTY_INFO
    //   slot 465: getWCL_TX_RX_LATENCY
    //   slot 466: createLQMData
    // setPoweredOnByUser and setCurrentBssid do NOT exist in real 15.7.5
    // IO80211InfraInterface; the earlier "drift" doc was based on stale
    // Sonoma-mislabelled-as-Sequoia data and is wrong.
    virtual IOReturn setLinkStateInternal(IO80211LinkState,uint,bool,uint);
    virtual void setCurrentApAddress(ether_addr *);
    virtual void setWCL_ADVISORTY_INFO(apple80211_wcl_advisory_info *);
    virtual void *getWCL_TX_RX_LATENCY(apple80211_wcl_tx_rx_latency *);
    virtual void createLQMData(void);
#else
    virtual IOReturn setLinkStateInternal(IO80211LinkState,uint,bool,uint);
    virtual void setWCL_ADVISORTY_INFO(apple80211_wcl_advisory_info *);
    virtual void *getWCL_TX_RX_LATENCY(apple80211_wcl_tx_rx_latency *);
#endif
    // hwConfigNicProxyData — in real 15.7.5 IO80211InfraInterface overrides
    // IOSkywalkEthernetInterface's slot 342. We let the parent's declaration
    // hold the slot (no need to re-declare here for ABI matching).
    
public:
    char _data[0x120];
};

#endif /* IO80211InfraInterface_h */
