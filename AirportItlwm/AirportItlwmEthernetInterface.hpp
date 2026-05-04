//
//  AirportItlwmEthernetInterface.hpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef AirportItlwmEthernetInterface_hpp
#define AirportItlwmEthernetInterface_hpp

extern "C" {
#include <net/bpf.h>
}
#include "Airport/Apple80211.h"
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>
#include <sys/kernel_types.h>
#include <IOKit/network/IOEthernetInterface.h>

class AirportItlwmEthernetInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(AirportItlwmEthernetInterface)
    
public:
    virtual IOReturn attachToDataLinkLayer( IOOptionBits options,
                                            void *       parameter ) override;
    
    virtual void     detachFromDataLinkLayer( IOOptionBits options,
                                              void *       parameter ) override;
    
    virtual bool initWithSkywalkInterfaceAndProvider(IONetworkController *controller, IO80211SkywalkInterface *interface);
    
    virtual bool setLinkState(IO80211LinkState state);
    
    static errno_t bpfOutputPacket(ifnet_t interface, u_int32_t data_link_type,
                                  mbuf_t packet);
    
    static errno_t bpfTap(ifnet_t interface, u_int32_t data_link_type,
                          bpf_tap_mode direction);
    
    virtual UInt32   inputPacket(
                                 mbuf_t          packet,
                                 UInt32          length  = 0,
                                 IOOptionBits    options = 0,
                                 void *          param   = 0 ) override;
    
    virtual IOService * getProvider( void ) const override;

#if __IO80211_TARGET >= __MAC_15_0
    // Static dispatcher to invoke prepareBSDInterface on the IO80211WorkQueue
    // thread (Sequoia requires this; Sonoma path doesn't need it).
    static IOReturn prepareBSDInterfaceGated(OSObject *target, void *interfaceArg,
                                             void *ifnetArg, void *, void *);

    // Sequoia 15.x: bypass Apple's BSD performCommand → executeCommand →
    // executeCommandAction chain entirely. That path is zombie code on
    // Sequoia (Apple's own Skywalk-based WiFi driver AppleBCMWLAN doesn't
    // use it; BSD ifnet ioctl handling moved to Skywalk's own attach path
    // via IOSkywalkLegacyEthernetInterface).
    //
    // When configd's BSD ifnet_ioctl reached our IOEthernetInterface (we
    // exposed one for Sonoma compat), Apple's IONetworkController::executeCommand
    // built a packet on its stack, packed action callback to packet[+0x10],
    // dispatched via IOCommandGate::runAction. By the time
    // executeCommandAction read packet[+0x10], the value was no longer the
    // real action callback — it was IO80211InfraProtocol::gMetaClass (a
    // DATA address near the vtable region). Calling that data address
    // triggered Kernel NX page fault.
    //
    // Returning kIOReturnUnsupported here short-circuits the broken Apple
    // dispatch entirely. ifconfig SIOCxxx commands won't be honored, but
    // the WiFi driver itself stays loaded and Skywalk attach can complete
    // without the BSD ioctl chain causing a panic.
    //
    // This is initial validation of the hypothesis. Follow-up D2 will
    // implement specific SIOCxxx handlers ourselves; D3 will move to
    // IOSkywalkLegacyEthernetInterface as the proper architectural fix.
    virtual SInt32 performCommand(IONetworkController *controller,
                                  unsigned long cmd,
                                  void *arg0,
                                  void *arg1) override;
#endif

private:
    IO80211SkywalkInterface *interface;
    bool isAttach;
};

#endif /* AirportItlwmEthernetInterface_hpp */
