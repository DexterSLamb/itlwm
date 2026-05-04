//
//  AirportItlwmShim.cpp
//  AirportItlwmShim — Lilu plugin that resolves un-exported Apple kext symbols
//  for AirportItlwm on macOS Sequoia 15.x and republishes them via IOResources
//  properties. The companion AirportItlwm.kext picks them up at start() time.
//
//  Symbols resolved (15.7.5 BootKC ground truth):
//    1. IOSkywalkTxSubmissionQueue::withPool(pool, txringSize, prio, owner,
//       action, refcon, flags)                  — IOSkywalkFamily
//    2. IOSkywalkRxCompletionQueue::withPool(pool, rxringSize, prio, owner,
//       action, refcon, flags)                  — IOSkywalkFamily
//    3. IO80211Controller::postMessage(uint, void*, ulong, uint, void*) — IO80211Family
//
//  These three symbols are not part of Apple's kxld export tables on Sonoma 14.4+,
//  so a kext that references them directly fails the kxld export check at load.
//  By moving the references behind runtime symbol resolution, we keep the main
//  driver loadable while preserving the actual call sites (now indirect via
//  function pointer).
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSBoolean.h>

// ============================================================================
// KextInfo descriptors
// ============================================================================

static const char *kIOSkywalkPath[] = {
    "/System/Library/Extensions/IOSkywalkFamily.kext/Contents/MacOS/IOSkywalkFamily"
};

static const char *kIO80211Path[] = {
    "/System/Library/Extensions/IO80211FamilyV2.kext/Contents/MacOS/IO80211FamilyV2",
    "/System/Library/Extensions/IO80211Family.kext/Contents/MacOS/IO80211Family"
};

// IONetworkingFamily — for runtime trace of performCommand/executeCommand/executeCommandAction
// Used only when boot-arg -aitlwmtrace is set; this is a pure debug aid.
static const char *kIONetPath[] = {
    "/System/Library/Extensions/IONetworkingFamily.kext/Contents/MacOS/IONetworkingFamily"
};

// {id, paths, pathNum, sys[Loaded,Reloadable,Disabled,FSOnly,FSFallback,Reserved], user, loadIndex}
// We mark FSOnly + FSFallback so we don't depend on prelinkedkernel and can
// fallback to filesystem if needed.
static KernelPatcher::KextInfo gSkywalkKext {
    "com.apple.iokit.IOSkywalkFamily",
    kIOSkywalkPath, 1,
    {true, true, false, false, false, false},
    {},
    KernelPatcher::KextInfo::Unloaded
};

// IO80211Family on Sequoia is delivered as IO80211FamilyV2 binary inside the
// IO80211FamilyV2.kext bundle but the bundle id is still com.apple.iokit.IO80211Family.
// We provide both candidate paths and let Lilu pick whichever exists.
static KernelPatcher::KextInfo gIO80211Kext {
    "com.apple.iokit.IO80211Family",
    kIO80211Path, 2,
    {true, true, false, false, false, false},
    {},
    KernelPatcher::KextInfo::Unloaded
};

static KernelPatcher::KextInfo gIONetKext {
    "com.apple.iokit.IONetworkingFamily",
    kIONetPath, 1,
    {true, true, false, false, false, false},
    {},
    KernelPatcher::KextInfo::Unloaded
};

// ============================================================================
// Trace mode (boot-arg -aitlwmtrace)
// ============================================================================
// When enabled, hook IOEthernetInterface::performCommand,
// IONetworkController::executeCommand, IONetworkController::executeCommandAction
// to kprintf their arguments. Used to diagnose Sequoia panic where
// packet[+0x10] (action callback in command_packet) becomes IO80211InfraProtocol::gMetaClass
// instead of the real IOEthernetInterface::performGatedCommand. The
// kprintf output is preserved in the kernel panic log buffer (last ~16KB),
// so we can read it from /Library/Logs/DiagnosticReports/Kernel-*.panic
// after the panic.

static bool gTraceEnabled = false;

static mach_vm_address_t gOrig_performCommand        = 0;
static mach_vm_address_t gOrig_executeCommand        = 0;
static mach_vm_address_t gOrig_executeCommandAction  = 0;

// IOEthernetInterface::performCommand
typedef int32_t (*performCommand_t)(void *self, void *ctrl, unsigned long cmd, void *arg0, void *arg1);
static int32_t my_performCommand(void *self, void *ctrl, unsigned long cmd, void *arg0, void *arg1)
{
    kprintf("[aitrace] performCommand ENTRY this=%p ctrl=%p cmd=0x%lx arg0=%p arg1=%p\n",
            self, ctrl, cmd, arg0, arg1);
    int32_t r = reinterpret_cast<performCommand_t>(gOrig_performCommand)(self, ctrl, cmd, arg0, arg1);
    kprintf("[aitrace] performCommand RETURN cmd=0x%lx ret=%d\n", cmd, r);
    return r;
}

// IONetworkController::executeCommand(OSObject*, Action, void*, void*, void*, void*, void*)
typedef int (*executeCommand_t)(void *self, void *client, void *action,
                                void *target, void *p0, void *p1, void *p2, void *p3);
static int my_executeCommand(void *self, void *client, void *action,
                             void *target, void *p0, void *p1, void *p2, void *p3)
{
    kprintf("[aitrace] executeCommand ENTRY this=%p client=%p action=%p target=%p p0=%p p1=%p p2=%p p3=%p\n",
            self, client, action, target, p0, p1, p2, p3);
    int r = reinterpret_cast<executeCommand_t>(gOrig_executeCommand)(self, client, action, target, p0, p1, p2, p3);
    kprintf("[aitrace] executeCommand RETURN action=%p ret=%d\n", action, r);
    return r;
}

// IONetworkController::executeCommandAction(OSObject*, void*, void*, void*, void*)
// arg0 = packet (the struct executeCommand built on its stack)
typedef int (*executeCommandAction_t)(void *owner, void *packet, void *a1, void *a2, void *a3);
static int my_executeCommandAction(void *owner, void *packet, void *a1, void *a2, void *a3)
{
    if (packet) {
        // Dump packet contents — especially [+0x10] = action func ptr that
        // gets called next. If it's gMetaClass, we have our smoking gun.
        uint64_t *p = static_cast<uint64_t *>(packet);
        kprintf("[aitrace] ECA ENTRY owner=%p packet=%p\n", owner, packet);
        kprintf("[aitrace] ECA packet[0..7] = %016llx %016llx %016llx %016llx\n",
                p[0], p[1], p[2], p[3]);
        kprintf("[aitrace] ECA packet[+0x10]=%016llx (this is the action that's about to be called)\n",
                p[2]);
    } else {
        kprintf("[aitrace] ECA ENTRY owner=%p packet=NULL (FALLBACK path)\n", owner);
    }
    int r = reinterpret_cast<executeCommandAction_t>(gOrig_executeCommandAction)(owner, packet, a1, a2, a3);
    kprintf("[aitrace] ECA RETURN ret=%d\n", r);
    return r;
}

// ============================================================================
// AirportItlwmShim — plugin object holding state across callbacks
// ============================================================================

class AirportItlwmShimPlugin {
public:
    bool init();
    void deinit();

private:
    // Per-symbol resolved address. Each kext load callback resolves whichever
    // symbols belong to the just-loaded kext. We publish to IOResources after
    // every resolve so order of kext load doesn't matter.
    mach_vm_address_t txWithPool   = 0;
    mach_vm_address_t rxWithPool   = 0;
    mach_vm_address_t postMessage  = 0;

    bool published = false;

    void onKextLoad(KernelPatcher &kp, size_t idx, mach_vm_address_t addr, size_t size);
    void publishOne(const char *key, mach_vm_address_t addr);
    void publishReadyIfDone();
};

static AirportItlwmShimPlugin ADDPR(plugin);

bool AirportItlwmShimPlugin::init()
{
    DBGLOG("aishim", "init starting");

    // Prefer non-Force variant so a registration failure (e.g. SIP-related
    // kext-list refresh races) is reported rather than panicking.
    auto err = lilu.onKextLoad(&gSkywalkKext, 1,
        [](void *user, KernelPatcher &kp, size_t idx, mach_vm_address_t addr, size_t size) {
            static_cast<AirportItlwmShimPlugin *>(user)->onKextLoad(kp, idx, addr, size);
        }, this);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("aishim", "onKextLoad(skywalk) failed: %d", err);
        return false;
    }

    err = lilu.onKextLoad(&gIO80211Kext, 1,
        [](void *user, KernelPatcher &kp, size_t idx, mach_vm_address_t addr, size_t size) {
            static_cast<AirportItlwmShimPlugin *>(user)->onKextLoad(kp, idx, addr, size);
        }, this);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("aishim", "onKextLoad(io80211) failed: %d", err);
        return false;
    }

    // Conditionally register IONetworkingFamily for trace hooks
    if (gTraceEnabled) {
        kprintf("[aitrace] -aitlwmtrace enabled, registering IONetworkingFamily hooks\n");
        err = lilu.onKextLoad(&gIONetKext, 1,
            [](void *user, KernelPatcher &kp, size_t idx, mach_vm_address_t addr, size_t size) {
                static_cast<AirportItlwmShimPlugin *>(user)->onKextLoad(kp, idx, addr, size);
            }, this);
        if (err != LiluAPI::Error::NoError) {
            SYSLOG("aishim", "onKextLoad(ionet) failed: %d", err);
            // non-fatal
        }
    }

    return true;
}

void AirportItlwmShimPlugin::deinit() {}

void AirportItlwmShimPlugin::publishOne(const char *key, mach_vm_address_t addr)
{
    auto res = IOService::getResourceService();
    if (!res) {
        SYSLOG("aishim", "no IOResources!");
        return;
    }
    uint64_t ptr = static_cast<uint64_t>(addr);
    auto data = OSData::withBytes(&ptr, sizeof(ptr));
    if (data) {
        res->setProperty(key, data);
        data->release();
        DBGLOG("aishim", "published %s = 0x%llx", key, ptr);
    }
}

void AirportItlwmShimPlugin::publishReadyIfDone()
{
    // We require Tx + Rx withPool. postMessage is preferred but not strictly
    // required (the Sonoma legacy postMessage path may stay valid via vtable).
    if (txWithPool && rxWithPool && !published) {
        auto res = IOService::getResourceService();
        if (res) {
            res->setProperty("AirportItlwm-Shim-Ready", kOSBooleanTrue);
            // Re-publish through the IOResources matching pump so waitForService
            // wakes up driver clients.
            res->registerService();
        }
        published = true;
        DBGLOG("aishim", "Shim-Ready published");
    }
}

void AirportItlwmShimPlugin::onKextLoad(KernelPatcher &kp, size_t idx,
                                  mach_vm_address_t addr, size_t size)
{
    (void)addr; (void)size;

    if (idx == gSkywalkKext.loadIndex) {
        if (!txWithPool) {
            // Sequoia 15.7.5 corrected mangling: PKP not PPK (the `K` qualifies
            // the inner pointer in `IOSkywalkPacket * const *`, not the
            // pointee's pointee). nm BootKC verified.
            txWithPool = kp.solveSymbol(idx,
                "__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j");
            if (txWithPool)
                publishOne("AirportItlwm-IOSkywalkTxSubmissionQueue-withPool", txWithPool);
            else
                SYSLOG("aishim", "Tx withPool unresolved");
            // clear sticky error from kp if symbol missing
            kp.clearError();
        }
        if (!rxWithPool) {
            rxWithPool = kp.solveSymbol(idx,
                "__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
            if (rxWithPool)
                publishOne("AirportItlwm-IOSkywalkRxCompletionQueue-withPool", rxWithPool);
            else
                SYSLOG("aishim", "Rx withPool unresolved");
            kp.clearError();
        }
    } else if (idx == gIO80211Kext.loadIndex) {
        if (!postMessage) {
            postMessage = kp.solveSymbol(idx,
                "__ZN17IO80211Controller11postMessageEjPvmjS0_");
            if (postMessage)
                publishOne("AirportItlwm-IO80211Controller-postMessage", postMessage);
            else
                SYSLOG("aishim", "postMessage unresolved (non-fatal)");
            kp.clearError();
        }
    } else if (gTraceEnabled && idx == gIONetKext.loadIndex) {
        // Install trace hooks on the three key dispatch points
        auto pc = kp.solveSymbol(idx,
            "__ZN19IOEthernetInterface14performCommandEP19IONetworkControllermPvS2_");
        kp.clearError();
        if (pc) {
            gOrig_performCommand = kp.routeFunction(pc,
                reinterpret_cast<mach_vm_address_t>(my_performCommand),
                /*buildWrapper*/ true,
                /*kernelRoute*/ true);
            kp.clearError();
            if (gOrig_performCommand)
                kprintf("[aitrace] performCommand hook installed @ 0x%llx orig=0x%llx\n",
                        pc, gOrig_performCommand);
            else
                SYSLOG("aishim", "performCommand routeFunction failed");
        } else {
            SYSLOG("aishim", "performCommand symbol not found");
        }

        auto ec = kp.solveSymbol(idx,
            "__ZN19IONetworkController14executeCommandEP8OSObjectPFiPvS2_S2_S2_S2_ES2_S2_S2_S2_S2_");
        kp.clearError();
        if (ec) {
            gOrig_executeCommand = kp.routeFunction(ec,
                reinterpret_cast<mach_vm_address_t>(my_executeCommand),
                true, true);
            kp.clearError();
            if (gOrig_executeCommand)
                kprintf("[aitrace] executeCommand hook installed @ 0x%llx orig=0x%llx\n",
                        ec, gOrig_executeCommand);
            else
                SYSLOG("aishim", "executeCommand routeFunction failed");
        } else {
            SYSLOG("aishim", "executeCommand symbol not found");
        }

        auto eca = kp.solveSymbol(idx,
            "__ZN19IONetworkController20executeCommandActionEP8OSObjectPvS2_S2_S2_");
        kp.clearError();
        if (eca) {
            gOrig_executeCommandAction = kp.routeFunction(eca,
                reinterpret_cast<mach_vm_address_t>(my_executeCommandAction),
                true, true);
            kp.clearError();
            if (gOrig_executeCommandAction)
                kprintf("[aitrace] executeCommandAction hook installed @ 0x%llx orig=0x%llx\n",
                        eca, gOrig_executeCommandAction);
            else
                SYSLOG("aishim", "executeCommandAction routeFunction failed");
        } else {
            SYSLOG("aishim", "executeCommandAction symbol not found");
        }
    }

    publishReadyIfDone();
}

// ============================================================================
// Boot-arg arrays + plugin configuration
// ============================================================================

static const char *bootargOff[]   = { "-ailitlshimoff" };
static const char *bootargDebug[] = { "-ailitlshimdbg" };
static const char *bootargBeta[]  = { "-ailitlshimbeta" };

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowSafeMode,
    bootargOff,   arrsize(bootargOff),
    bootargDebug, arrsize(bootargDebug),
    bootargBeta,  arrsize(bootargBeta),
    KernelVersion::Sequoia,
    KernelVersion::Sequoia,
    []() {
        // Parse -aitlwmtrace boot-arg before init so onKextLoad knows
        // whether to register IONetworkingFamily hooks.
        gTraceEnabled = checkKernelArgument("-aitlwmtrace");
        ADDPR(plugin).init();
    }
};
