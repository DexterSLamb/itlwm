//
//  AirportItlwmEthernetInterface.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmEthernetInterface.hpp"
#if __IO80211_TARGET >= __MAC_15_0
// AirportItlwm declares getWorkQueue() and apple80211Request() used by
// the Sequoia path's runAction dispatch and ioctl handler.
#include "AirportItlwmV2.hpp"
#endif

#include <sys/_if_ether.h>
#include <net80211/ieee80211_var.h>

#define super IOEthernetInterface
OSDefineMetaClassAndStructors(AirportItlwmEthernetInterface, IOEthernetInterface);

bool AirportItlwmEthernetInterface::
initWithSkywalkInterfaceAndProvider(IONetworkController *controller, IO80211SkywalkInterface *interface)
{
    bool ret = super::init(controller);
    if (ret)
        this->interface = interface;
    this->isAttach = false;
    return ret;
}

IOReturn AirportItlwmEthernetInterface::
attachToDataLinkLayer( IOOptionBits options, void *parameter )
{
    XYLog("%s\n", __FUNCTION__);
    char infName[IFNAMSIZ];
    IOReturn ret = super::attachToDataLinkLayer(options, parameter);
    if (ret == kIOReturnSuccess && interface) {
        UInt8 builtIn = 0;
        IOEthernetAddress addr;
        interface->setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
        snprintf(infName, sizeof(infName), "%s%u", ifnet_name(getIfnet()), ifnet_unit(getIfnet()));
        interface->setProperty("IOInterfaceName", OSString::withCString(infName));
        interface->setProperty(kIOInterfaceUnit, OSNumber::withNumber(ifnet_unit(getIfnet()), 8));
        interface->setProperty(kIOInterfaceNamePrefix, OSString::withCString(ifnet_name(getIfnet())));
        if (OSDynamicCast(IOEthernetController, getController())->getHardwareAddress(&addr) == kIOReturnSuccess)
            setProperty(kIOMACAddress,  (void *) &addr,
                        kIOEthernetAddressSize);
        interface->registerService();
#if __IO80211_TARGET >= __MAC_15_0
        // Sequoia 15.x: IO80211InfraInterface::updateStaticProperties (called from
        // prepareBSDInterface) hard-asserts thread context via IO80211Glue::sendIOUCToWcl
        // @0x1b3ff: must run on the WCL workqueue thread or panic("trying to send on
        // thread panic" @IO80211Glue.cpp:417). Apple's framework never invokes
        // prepareBSDInterface on its own (only subclass super:: delegation chains;
        // verified by grep of all vtable byte 0x8e8 dispatchers in IO80211Family —
        // zero matches). So we MUST call it ourselves, but we MUST dispatch onto
        // the workqueue thread. AirportItlwm exposes _fWorkloop (IO80211WorkQueue,
        // extends IOWorkLoop); its runAction synchronously executes the action on
        // the workloop thread, satisfying Glue's inGate() check.
        // KDK source: research/sequoia-port/kdk-extract/.../IO80211Family.kext
        AirportItlwm *ctrl = OSDynamicCast(AirportItlwm, getController());
        // IO80211WorkQueue extends IOWorkLoop. AirportItlwm::getWorkQueue()
        // returns the WCL workqueue (_fWorkloop), NOT IOService::getWorkLoop()
        // which would be the family workloop. Use the WCL one because that's
        // what Apple's Glue::sendIOUCToWcl asserts as required thread.
        IO80211WorkQueue *wl = ctrl ? ctrl->getWorkQueue() : NULL;
        if (wl) {
            wl->runAction(&AirportItlwmEthernetInterface::prepareBSDInterfaceGated,
                          this, interface, getIfnet());
        } else {
            // Fallback: best-effort direct call (will likely panic but better than
            // silently skipping interface init)
            interface->prepareBSDInterface(getIfnet(), 0);
        }
#else
        // Sonoma 14.x: no workqueue thread assertion in IO80211Glue (Glue is a
        // Sequoia-only class). Direct call is fine and matches original behavior.
        interface->prepareBSDInterface(getIfnet(), 0);
#endif
//        ret = bpf_attach(getIfnet(), DLT_RAW, 0x48, &AirportItlwmEthernetInterface::bpfOutputPacket, &AirportItlwmEthernetInterface::bpfTap);
    }
    isAttach = true;
    return ret;
}

#if __IO80211_TARGET >= __MAC_15_0
// Static dispatcher invoked on the IO80211WorkQueue thread by runAction.
// Calling interface->prepareBSDInterface here satisfies Apple's
// IO80211Glue::sendIOUCToWcl thread-context assertion (Apple's gate check
// at slot vtable[0x130]/[0x138] of IO80211WorkQueue).
IOReturn AirportItlwmEthernetInterface::
prepareBSDInterfaceGated(OSObject *target, void *interfaceArg,
                         void *ifnetArg, void *, void *)
{
    auto *interface = (IO80211SkywalkInterface *)interfaceArg;
    if (interface) {
        interface->prepareBSDInterface((__ifnet *)ifnetArg, 0);
    }
    return kIOReturnSuccess;
}

// Sequoia: short-circuit broken Apple BSD performCommand chain BUT
// allow apple80211 ioctls through to our existing handler so airportd
// can talk to us as WiFi hardware.
//
// Apple's IOEthernetInterface::performCommand on Sequoia is zombie
// code — its executeCommand → executeCommandAction → packet[+0x10]
// dispatch ends up calling IO80211InfraProtocol::gMetaClass (a DATA
// address) causing Kernel NX panic.
//
// We handle two ioctl families ourselves:
//   - SIOCSA80211 / SIOCGA80211: apple80211 ioctls used by airportd
//     (associate, scan, get RSSI, set channel, ~50+ subtypes). Our
//     existing AirportSTAIOCTL.cpp handler processes these via
//     AirportItlwm::apple80211Request.
//   - Everything else (BSD generic SIOCxxx like SIOCSIFFLAGS, address
//     setting, etc.): return Unsupported. ifconfig direct manipulation
//     won't work but airportd-driven WiFi connection will.
SInt32 AirportItlwmEthernetInterface::
performCommand(IONetworkController *controller, unsigned long cmd,
               void *arg0, void *arg1)
{
    if (cmd == SIOCSA80211 || cmd == SIOCGA80211) {
        // arg0 is struct apple80211req* (BSD ioctl convention)
        struct apple80211req *req = static_cast<struct apple80211req *>(arg0);
        if (!req) {
            return kIOReturnBadArgument;
        }
        AirportItlwm *ctrl = OSDynamicCast(AirportItlwm, controller);
        if (!ctrl) {
            return kIOReturnBadArgument;
        }
        // apple80211Request is the existing handler in AirportSTAIOCTL.cpp
        // (Sonoma path used a different invocation but the handler itself
        // is OS-version-agnostic; processes ~50 SIOC apple80211 subtypes).
        return ctrl->apple80211Request(static_cast<unsigned int>(cmd),
                                       req->req_type,
                                       /*IO80211Interface* */ nullptr,
                                       req);
    }
    // Generic BSD ioctl path is broken on Sequoia; reject.
    return kIOReturnUnsupported;
}
#endif

void AirportItlwmEthernetInterface::
detachFromDataLinkLayer(IOOptionBits options, void *parameter)
{
    super::detachFromDataLinkLayer(options, parameter);
    isAttach = false;
}

/**
 Add another hack to fake that the provider is IOSkywalkNetworkInterface, to avoid skywalkfamily instance cast panic.
 */
IOService *AirportItlwmEthernetInterface::
getProvider() const
{
    return isAttach ? this->interface : super::getProvider();
}

errno_t AirportItlwmEthernetInterface::
bpfOutputPacket(ifnet_t interface, u_int32_t data_link_type, mbuf_t packet)
{
    XYLog("%s data_link_type: %d\n", __FUNCTION__, data_link_type);
    AirportItlwmEthernetInterface *networkInterface = (AirportItlwmEthernetInterface *)ifnet_softc(interface);
    return networkInterface->enqueueOutputPacket(packet);
}

errno_t AirportItlwmEthernetInterface::
bpfTap(ifnet_t interface, u_int32_t data_link_type, bpf_tap_mode direction)
{
    XYLog("%s data_link_type: %d direction: %d\n", __FUNCTION__, data_link_type, direction);
    return 0;
}

bool AirportItlwmEthernetInterface::
setLinkState(IO80211LinkState state)
{
    if (state == kIO80211NetworkLinkUp) {
        ifnet_set_flags(getIfnet(), ifnet_flags(getIfnet()) | (IFF_UP | IFF_RUNNING), (IFF_UP | IFF_RUNNING));
    } else {
        ifnet_set_flags(getIfnet(), ifnet_flags(getIfnet()) & ~(IFF_UP | IFF_RUNNING), 0);
    }
    return true;
}

extern const char* hexdump(uint8_t *buf, size_t len);

UInt32 AirportItlwmEthernetInterface::
inputPacket(mbuf_t packet, UInt32 length, IOOptionBits options, void *param)
{
    ether_header_t *eh;
    size_t len = mbuf_len(packet);
    
    eh = (ether_header_t *)mbuf_data(packet);
    if (len >= sizeof(ether_header_t) && eh->ether_type == htons(ETHERTYPE_PAE)) { // EAPOL packet
        const char* dump = hexdump((uint8_t*)mbuf_data(packet), len);
        XYLog("input EAPOL packet, len: %zu, data: %s\n", len, dump ? dump : "Failed to allocate memory");
        if (dump)
            IOFree((void*)dump, 3 * len + 1);
    }
    return IOEthernetInterface::inputPacket(packet, length, options, param);
}
