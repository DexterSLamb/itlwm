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
#include <Headers/kern_mach.hpp>

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

// AirportItlwm itself — we patch its vtable at load time. OC injects from EFI but
// the runtime kext bundle ID matches; Lilu locates by bundle id when path doesn't
// resolve. Provide a no-op path for symbol resolution to work.
static const char *kAirportItlwmPath[] = {
    "/Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm",
    "/System/Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm"
};

static KernelPatcher::KextInfo gAirportItlwmKext {
    "com.zxystd.AirportItlwm",
    kAirportItlwmPath, 2,
    {true, true, false, false, false, false},
    {},
    KernelPatcher::KextInfo::Unloaded
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

static mach_vm_address_t gOrig_executeCommandAction  = 0;

// Cap kprintf output to avoid hanging boot. We only need a handful of trace
// events to find the corruption point — the panic happens deterministically.
static volatile uint32_t gTraceCount = 0;
static const uint32_t kTraceMax = 200;

// IONetworkController::executeCommandAction(OSObject*, void*, void*, void*, void*)
// arg0 (rsi) = packet (struct executeCommand built on its stack, [+0x10] = action func ptr)
// We hook ONLY this function (not performCommand/executeCommand) because:
//   1. It runs only when actual dispatches happen — not per-ioctl
//   2. It's the function whose call site (call [r14+0x10]) NX-faults to
//      gMetaClass — directly upstream of the crash
//   3. Hooking the upstream functions caused boot loops (called too often)
typedef int (*executeCommandAction_t)(void *owner, void *packet, void *a1, void *a2, void *a3);
static int my_executeCommandAction(void *owner, void *packet, void *a1, void *a2, void *a3)
{
    uint32_t n = __c11_atomic_fetch_add(reinterpret_cast<volatile _Atomic uint32_t *>(&gTraceCount), 1, __ATOMIC_RELAXED);
    if (n < kTraceMax) {
        if (packet) {
            uint64_t *p = static_cast<uint64_t *>(packet);
            kprintf("[aitrace] #%u ECA owner=%p packet=%p p[0]=%llx p[1]=%llx p[2]=%llx p[3]=%llx\n",
                    n, owner, packet, p[0], p[1], p[2], p[3]);
            // The killer: p[2] = packet[+0x10] = action func ptr that gets called.
            // If it's IO80211InfraProtocol::gMetaClass we found our bug.
        } else {
            kprintf("[aitrace] #%u ECA owner=%p packet=NULL (fallback)\n", n, owner);
        }
    }
    return reinterpret_cast<executeCommandAction_t>(gOrig_executeCommandAction)(owner, packet, a1, a2, a3);
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

    // Plan A core: register for our own AirportItlwm load to vtable-patch
    // misaligned slots into Apple's expected positions (slot 410 = getCARD_CAPABILITIES).
    // Header-based alignment causes kxld to silently reject (vtable size mismatch
    // with Apple's IO80211Controller); runtime patching via Lilu sidesteps that.
    err = lilu.onKextLoad(&gAirportItlwmKext, 1,
        [](void *user, KernelPatcher &kp, size_t idx, mach_vm_address_t addr, size_t size) {
            static_cast<AirportItlwmShimPlugin *>(user)->onKextLoad(kp, idx, addr, size);
        }, this);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("aishim", "onKextLoad(AirportItlwm) failed: %d", err);
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
    } else if (idx == gAirportItlwmKext.loadIndex) {
        // Plan A vtable patch: align our overrides to Apple's expected slots
        // in IO80211Controller. Our compile lays them out off-by-2 (header
        // chain has 2 fewer virtuals than Apple's real binary). Fix at runtime.
        //
        // Apple expected slots (RE'd from KDK 15.7.4 IO80211Family wrapper call sites):
        //   slot 410 = getCARD_CAPABILITIES(SkywalkInterface*, capability_data*)
        //   slot 411 = getDRIVER_VERSION(SkywalkInterface*, version_data*)
        //   slot 412 = getHARDWARE_VERSION(SkywalkInterface*, version_data*)
        //   slot 414 = getPOWER(SkywalkInterface*, power_data*)
        //   slot 415 = setPOWER(SkywalkInterface*, power_data*)
        //   slot 416 = getCOUNTRY_CODE(SkywalkInterface*, country_code_data*)
        //   slot 417 = setCOUNTRY_CODE(SkywalkInterface*, country_code_data*)
        // (slot 413 unknown, leave inherited)
        auto vtableAddr = kp.solveSymbol(idx, "__ZTV12AirportItlwm");
        kp.clearError();
        if (!vtableAddr) {
            SYSLOG("aishim", "vtable _ZTV12AirportItlwm not found");
            publishReadyIfDone();
            return;
        }

        struct VtableSlot { uint32_t slot; const char *symbol; const char *name; };
        VtableSlot slots[] = {
            {410, "__ZN12AirportItlwm20getCARD_CAPABILITIESEP23IO80211SkywalkInterfaceP26apple80211_capability_data", "getCARD_CAPABILITIES"},
            {411, "__ZN12AirportItlwm17getDRIVER_VERSIONEP23IO80211SkywalkInterfaceP23apple80211_version_data",       "getDRIVER_VERSION"},
            {412, "__ZN12AirportItlwm19getHARDWARE_VERSIONEP23IO80211SkywalkInterfaceP23apple80211_version_data",     "getHARDWARE_VERSION"},
            {414, "__ZN12AirportItlwm8getPOWEREP23IO80211SkywalkInterfaceP21apple80211_power_data",                   "getPOWER"},
            {415, "__ZN12AirportItlwm8setPOWEREP23IO80211SkywalkInterfaceP21apple80211_power_data",                   "setPOWER"},
            {416, "__ZN12AirportItlwm15getCOUNTRY_CODEEP23IO80211SkywalkInterfaceP28apple80211_country_code_data",    "getCOUNTRY_CODE"},
            {417, "__ZN12AirportItlwm15setCOUNTRY_CODEEP23IO80211SkywalkInterfaceP28apple80211_country_code_data",    "setCOUNTRY_CODE"},
        };

        // ZTV layout: [offset_to_top, typeinfo_ptr, vfunc[0], vfunc[1], ...]
        // vfunc[N] is at ZTV byte offset (N+2)*8 = N*8 + 16.
        for (auto &s : slots) {
            auto fn = kp.solveSymbol(idx, s.symbol);
            kp.clearError();
            if (!fn) {
                SYSLOG("aishim", "vtable patch: symbol %s not found", s.name);
                continue;
            }
            mach_vm_address_t targetSlotAddr = vtableAddr + (s.slot * 8 + 16);
            if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) == KERN_SUCCESS) {
                *reinterpret_cast<uint64_t *>(targetSlotAddr) = static_cast<uint64_t>(fn);
                MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
                kprintf("[aishim] vtable[%u] = %s @ 0x%llx\n", s.slot, s.name, fn);
            } else {
                SYSLOG("aishim", "vtable patch slot %u: setKernelWriting failed", s.slot);
            }
        }
    } else if (gTraceEnabled && idx == gIONetKext.loadIndex) {
        // ONLY hook executeCommandAction. v1 also hooked performCommand and
        // executeCommand; that caused boot loop (called thousands of times
        // per second early in boot, kprintf overhead halts system).
        auto eca = kp.solveSymbol(idx,
            "__ZN19IONetworkController20executeCommandActionEP8OSObjectPvS2_S2_S2_");
        kp.clearError();
        if (eca) {
            gOrig_executeCommandAction = kp.routeFunction(eca,
                reinterpret_cast<mach_vm_address_t>(my_executeCommandAction),
                /*buildWrapper*/ true,
                /*kernelRoute*/ true);
            kp.clearError();
            if (gOrig_executeCommandAction)
                kprintf("[aitrace] ECA hook installed @ 0x%llx orig=0x%llx\n",
                        eca, gOrig_executeCommandAction);
            else
                SYSLOG("aishim", "ECA routeFunction failed");
        } else {
            SYSLOG("aishim", "ECA symbol not found");
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
