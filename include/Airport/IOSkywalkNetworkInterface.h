//
//  IOSkywalkNetworkInterface.h
//  itlwm
//
//  Created by qcwap on 2023/6/7.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef IOSkywalkNetworkInterface_h
#define IOSkywalkNetworkInterface_h

#include <net/if.h>

#include "IOSkywalkInterface.h"

typedef UInt if_link_status;
class IOSkywalkPacketQueue;
class IOSkywalkLogicalLink;
class IOSkywalkPacketBufferPool;

class IOSkywalkNetworkInterface : public IOSkywalkInterface {
    OSDeclareAbstractStructors( IOSkywalkNetworkInterface )
    
public:
    struct RegistrationInfo {
        uint8_t pad[304];
    } __attribute__((packed));
    struct IOSkywalkTSOOptions;
    
public:
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool init(OSDictionary *) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *) APPLE_KEXT_OVERRIDE;
    virtual void joinPMtree( IOService * driver ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setAggressiveness(
                                       unsigned long type,
                                       unsigned long newLevel ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn enable(UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn disable(UInt) APPLE_KEXT_OVERRIDE;
    // 15.7.5 ground truth (research/sequoia-port/diff/15.7.5-IO80211InfraProtocol-vtable-REAL.txt):
    // After the 4 IOSkywalkInterface RESERVED slots (280-283), Apple's slot 284-285
    // are registerNetworkInterfaceWithLogicalLink/deregisterLogicalLink BEFORE
    // initBSDInterfaceParameters (slot 286). Earlier header declared them at the
    // END of the class body, which placed them at slots 321-322 in our binary and
    // shifted everything 284..322 by -2 vs Apple. That makes vtable[N] dispatch
    // the wrong method for ~37 slots — silent runtime corruption risk.
#if __IO80211_TARGET >= __MAC_15_0
    virtual IOReturn registerNetworkInterfaceWithLogicalLink(IOSkywalkNetworkInterface::RegistrationInfo const*,IOSkywalkLogicalLink *,IOSkywalkPacketBufferPool *,IOSkywalkPacketBufferPool *,UInt);
    virtual IOReturn deregisterLogicalLink(void);
#endif
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams *,sockaddr_dl **) = 0;
    virtual bool prepareBSDInterface(ifnet_t,UInt);
    virtual void finalizeBSDInterface(ifnet_t,UInt);
    virtual ifnet_t getBSDInterface(void);
    virtual void setBSDName(char const*);
    virtual const char *getBSDName(void);
    virtual IOReturn processBSDCommand(ifnet_t,UInt,void *);
    virtual IOReturn processInterfaceCommand(ifdrv *);
    virtual IOReturn interfaceAdvisoryEnable(bool);
    virtual SInt32 setInterfaceEnable(bool);
    virtual SInt32 setRunningState(bool);
    virtual IOReturn handleChosenMedia(UInt);
    virtual void *getSupportedMediaArray(UInt *,UInt *);
    virtual void *getPacketTapInfo(UInt *,UInt *);
    virtual UInt getUnsentDataByteCount(UInt *,UInt *,UInt);
    virtual UInt32 getSupportedWakeFlags(UInt *);
    virtual void enableNetworkWake(UInt);
    virtual void calculateRingSizeForQueue(IOSkywalkPacketQueue const*,UInt *);
    virtual UInt getMaxTransferUnit(void);
    virtual void setMaxTransferUnit(UInt);
    virtual UInt getMinPacketSize(void);
    virtual UInt getHardwareAssists(void);
    virtual void setHardwareAssists(UInt,UInt);
    virtual void *getInterfaceFamily(void);
    virtual void *getInterfaceSubFamily(void);
    virtual UInt getInitialMedia(void);
    virtual UInt getFeatureFlags(void);
    virtual UInt getTxDataOffset(void);
    virtual UInt captureInterfaceState(UInt);
    virtual void restoreInterfaceState(UInt);
    virtual void setMTU(UInt);
    virtual bool bpfTap(UInt,UInt);
    virtual const char *getBSDNamePrefix(void);
    virtual UInt getBSDUnitNumber(void);
    virtual const char *classNameOverride(void);
    virtual void deferBSDAttach(bool);
    virtual void reportDetailedLinkStatus(if_link_status const*);
#if __IO80211_TARGET < __MAC_15_0
    // Sonoma 14.x kept register/deregister at the end. Preserve binary identity
    // for the 14.4 build path.
    virtual IOReturn registerNetworkInterfaceWithLogicalLink(IOSkywalkNetworkInterface::RegistrationInfo const*,IOSkywalkLogicalLink *,IOSkywalkPacketBufferPool *,IOSkywalkPacketBufferPool *,UInt);
    virtual IOReturn deregisterLogicalLink(void);
#endif
    virtual UInt getTSOOptions(IOSkywalkNetworkInterface::IOSkywalkTSOOptions *);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  0);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  1);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  2);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  3);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  4);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  5);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  6);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  7);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  8);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  9);
    
public:
    void reportLinkStatus(unsigned int, unsigned int);
    
public:
    void *vptr;
    struct ExpansionData
    {
        RegistrationInfo *fRegistrationInfo;
        ifnet_t fBSDInterface;
    };
    ExpansionData *mExpansionData;
    uint8_t pad[2 * 8];
};

static_assert(sizeof(IOSkywalkNetworkInterface) == 0xD0, "Invalid class size");

#endif /* IOSkywalkNetworkInterface_h */
