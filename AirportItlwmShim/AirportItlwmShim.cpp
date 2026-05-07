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

// Stage 1 diagnostic: hook apple80211getSUPPORTED_CHANNELS wrapper to discover
// (a) whether the wrapper is on the actual airportd dispatch path, (b) what
// class the iface belongs to (our SkywalkInterface vs Apple-managed wrap),
// (c) what's at vtable byte offsets 0xcc0 (Apple guard), 0xeb8 (Apple's expected
// SUPPORTED_CHANNELS slot), 0xf00 (our actual SUPPORTED_CHANNELS slot) on the
// runtime iface. No functional change — just log and forward to the original.
static mach_vm_address_t gOrig_supchan = 0;

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

// Stage 1 v2 diagnostic hook for apple80211getSUPPORTED_CHANNELS.
// Reports observations via IOResources properties (NOT kprintf — kprintf doesn't
// reach unified log on Sequoia 15 in our config). Properties are readable via
// `ioreg -l | grep AirportItlwm-supchan`.
typedef IOReturn (*supchan_t)(void *iface, void *data);
static volatile uint32_t gSupchanLogCount = 0;
static const uint32_t kSupchanLogMax = 8;

// v4 lifecycle markers: set bit in g_lifecycle_bits when stage runs, then flush
// to IOResources whenever a publish helper is called. Bits:
//   0  init-entered
//   1  skywalk-reg-ok
//   2  io80211-reg-ok
//   3  onPatcherLoad-reg-ok
//   4  init-completed
//   5  onKextLoad-skywalk-fired
//   6  onKextLoad-io80211-fired
//   7  onPatcherLoad-callback-fired (= patchAirportItlwmVtable entry)
//   8  pAiv-aiIdx-resolved
//   9  pAiv-vtAddr-resolved
//  10  pAiv-controllerSlots-attempted
//  11  pAiv-io80211Idx-resolved
//  12  pAiv-supSym-resolved
//  13  pAiv-supRoute-installed
//  14  pAiv-completed
static volatile uint64_t g_lifecycle_bits = 0;
static inline void setLifeBit(int b) {
    __c11_atomic_fetch_or(reinterpret_cast<volatile _Atomic uint64_t *>(&g_lifecycle_bits), 1ULL << b, __ATOMIC_RELAXED);
}

// Helper to publish a uint64 to IOResources as OSData (8 bytes). Always also
// flushes the lifecycle bitmap so we know how far Shim got even if a later
// publish failed.
static void supchanPublishU64(const char *key, uint64_t val) {
    auto res = IOService::getResourceService();
    if (!res) return;
    auto data = OSData::withBytes(&val, sizeof(val));
    if (data) { res->setProperty(key, data); data->release(); }
    uint64_t bits = g_lifecycle_bits;
    auto bdata = OSData::withBytes(&bits, sizeof(bits));
    if (bdata) { res->setProperty("AirportItlwm-Shim-life-bits", bdata); bdata->release(); }
}
// Helper to publish a C string to IOResources.
static void supchanPublishStr(const char *key, const char *val) {
    auto res = IOService::getResourceService();
    if (!res || !val) return;
    auto str = OSString::withCString(val);
    if (str) { res->setProperty(key, str); str->release(); }
    uint64_t bits = g_lifecycle_bits;
    auto bdata = OSData::withBytes(&bits, sizeof(bits));
    if (bdata) { res->setProperty("AirportItlwm-Shim-life-bits", bdata); bdata->release(); }
}

static IOReturn my_supchan(void *iface, void *data)
{
    uint32_t n = __c11_atomic_fetch_add(reinterpret_cast<volatile _Atomic uint32_t *>(&gSupchanLogCount), 1, __ATOMIC_RELAXED);
    // Always update the fires counter so we know the hook ran.
    supchanPublishU64("AirportItlwm-supchan-fires", n + 1);
    // Log first kSupchanLogMax invocations with full detail.
    if (n < kSupchanLogMax) {
        char keybuf[64];
        snprintf(keybuf, sizeof(keybuf), "AirportItlwm-supchan-%u-iface", n);
        supchanPublishU64(keybuf, reinterpret_cast<uint64_t>(iface));

        if (iface) {
            const char *cls = "(no-meta)";
            OSObject *obj = static_cast<OSObject *>(iface);
            const OSMetaClass *mc = obj->getMetaClass();
            if (mc && mc->getClassName()) cls = mc->getClassName();
            snprintf(keybuf, sizeof(keybuf), "AirportItlwm-supchan-%u-class", n);
            supchanPublishStr(keybuf, cls);

            void **vt = *reinterpret_cast<void ***>(iface);
            if (vt) {
                uint64_t v_cc0 = *reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(vt) + 0xcc0);
                uint64_t v_eb8 = *reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(vt) + 0xeb8);
                uint64_t v_f00 = *reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(vt) + 0xf00);
                snprintf(keybuf, sizeof(keybuf), "AirportItlwm-supchan-%u-vt-cc0", n);
                supchanPublishU64(keybuf, v_cc0);
                snprintf(keybuf, sizeof(keybuf), "AirportItlwm-supchan-%u-vt-eb8", n);
                supchanPublishU64(keybuf, v_eb8);
                snprintf(keybuf, sizeof(keybuf), "AirportItlwm-supchan-%u-vt-f00", n);
                supchanPublishU64(keybuf, v_f00);
            }
        }
    }
    if (gOrig_supchan)
        return reinterpret_cast<supchan_t>(gOrig_supchan)(iface, data);
    return kIOReturnUnsupported;
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
    void patchAirportItlwmVtable(KernelPatcher &kp);
    void publishOne(const char *key, mach_vm_address_t addr);
    void publishReadyIfDone();
};

static AirportItlwmShimPlugin ADDPR(plugin);

bool AirportItlwmShimPlugin::init()
{
    DBGLOG("aishim", "init starting");
    setLifeBit(0);  // init-entered

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
    setLifeBit(1);  // skywalk-reg-ok

    err = lilu.onKextLoad(&gIO80211Kext, 1,
        [](void *user, KernelPatcher &kp, size_t idx, mach_vm_address_t addr, size_t size) {
            static_cast<AirportItlwmShimPlugin *>(user)->onKextLoad(kp, idx, addr, size);
        }, this);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("aishim", "onKextLoad(io80211) failed: %d", err);
        return false;
    }
    setLifeBit(2);  // io80211-reg-ok

    // Plan A core: vtable-patch AirportItlwm class slots after kxld layout.
    // OC injects AirportItlwm at boot before Lilu's onKextLoad fires, so use
    // onPatcherLoad to do the work when patcher is ready (OC kexts already
    // in patcher's kinfos by then).
    err = lilu.onPatcherLoad([](void *user, KernelPatcher &kp) {
        static_cast<AirportItlwmShimPlugin *>(user)->patchAirportItlwmVtable(kp);
    }, this);
    if (err != LiluAPI::Error::NoError) {
        SYSLOG("aishim", "onPatcherLoad failed: %d", err);
        return false;
    }
    setLifeBit(3);  // onPatcherLoad-reg-ok

    setLifeBit(4);  // init-completed

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

    if (idx == gSkywalkKext.loadIndex) setLifeBit(5);
    else if (idx == gIO80211Kext.loadIndex) setLifeBit(6);

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
        // (supchan hook moved to onPatcherLoad path — see installSupchanHook
        // in patchAirportItlwmVtable. The path-based gIO80211Kext callback
        // doesn't fire on Sequoia 15 because IO80211FamilyV2 is bundled into
        // BootKC and Lilu's path scanner doesn't see it.)
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

void AirportItlwmShimPlugin::patchAirportItlwmVtable(KernelPatcher &kp)
{
    setLifeBit(7);  // onPatcherLoad-callback-fired
    supchanPublishStr("AirportItlwm-supchan-pPv-entered", "yes");

    // Find AirportItlwm kext (loaded by OC at boot, so already in patcher's kinfos).
    auto idx = kp.loadKinfo(&gAirportItlwmKext);
    kp.clearError();
    supchanPublishU64("AirportItlwm-supchan-pPv-aiIdx", idx);
    if (idx == 0) {
        supchanPublishStr("AirportItlwm-supchan-pPv-status", "ai-loadKinfo-failed");
        SYSLOG("aishim", "AirportItlwm kext not found in patcher kinfos");
        return;
    }
    setLifeBit(8);  // pAiv-aiIdx-resolved

    // CRITICAL: solveSymbol returns 0 unless the kinfo's symbol table has been
    // parsed via updateRunningInfo. Lilu's processKextLoadCallbacks does this
    // automatically before firing path-based onKextLoad callbacks, but our
    // loadKinfo path bypasses that. Force re-parse here.
    auto aiSize = kp.updateRunningInfo(idx, 0, 0, /*force*/ true);
    kp.clearError();
    supchanPublishU64("AirportItlwm-supchan-ai-runningSize", aiSize);
    kprintf("[aishim] AirportItlwm loadIndex=%zu\n", idx);

    // v5: install supchan hook FIRST, before controller vtable patch.
    // The vtable patch was the only thing failing in v4 (vtAddr-resolve-failed),
    // and it's INDEPENDENT of the supchan hook target. Run hook installation
    // first so it has a chance to succeed even if the vtable patch later fails.
    {
        auto io80211Idx = kp.loadKinfo(&gIO80211Kext);
        kp.clearError();
        supchanPublishU64("AirportItlwm-supchan-io80211-idx", io80211Idx);
        if (io80211Idx == 0) {
            supchanPublishStr("AirportItlwm-supchan-status", "io80211-loadKinfo-failed");
        } else {
            setLifeBit(11); // pAiv-io80211Idx-resolved
            // Same as above: force symbol-table parse before solveSymbol.
            auto io8Size = kp.updateRunningInfo(io80211Idx, 0, 0, /*force*/ true);
            kp.clearError();
            supchanPublishU64("AirportItlwm-supchan-io80211-runningSize", io8Size);
            auto wrapperSym = kp.solveSymbol(io80211Idx,
                "__Z31apple80211getSUPPORTED_CHANNELSP23IO80211SkywalkInterfaceP27apple80211_sup_channel_data");
            kp.clearError();
            supchanPublishU64("AirportItlwm-supchan-wrapper-sym", wrapperSym);
            if (!wrapperSym) {
                supchanPublishStr("AirportItlwm-supchan-status", "wrapper-symbol-not-resolved");
                // Fallback test: try a known-exported IO80211 symbol to see if
                // solveSymbol works at all on this kinfo.
                auto probe = kp.solveSymbol(io80211Idx, "__ZN17IO80211Controller11postMessageEjPvmjS0_");
                kp.clearError();
                supchanPublishU64("AirportItlwm-supchan-probe-postMessage", probe);
            } else {
                setLifeBit(12); // pAiv-supSym-resolved
                gOrig_supchan = kp.routeFunction(wrapperSym,
                    reinterpret_cast<mach_vm_address_t>(my_supchan),
                    /*buildWrapper*/ true, /*kernelRoute*/ true);
                kp.clearError();
                supchanPublishU64("AirportItlwm-supchan-orig", gOrig_supchan);
                if (gOrig_supchan) {
                    setLifeBit(13); // pAiv-supRoute-installed
                    supchanPublishStr("AirportItlwm-supchan-status", "installed");
                } else {
                    supchanPublishStr("AirportItlwm-supchan-status", "routeFunction-failed");
                }
            }
        }
    }

    auto vtableAddr = kp.solveSymbol(idx, "__ZTV12AirportItlwm");
    kp.clearError();
    supchanPublishU64("AirportItlwm-supchan-pPv-vtAddr", vtableAddr);
    if (!vtableAddr) {
        supchanPublishStr("AirportItlwm-supchan-pPv-status", "vtAddr-resolve-failed");
        // Probe alt AirportItlwm symbols to confirm whether solveSymbol works
        // at all for OUR kinfo on Sequoia 15. If these also return 0, we know
        // the kext's symbol table is post-load stripped and need a different
        // mechanism (e.g. read vtable ptr from a registered IOService instance).
        auto probe1 = kp.solveSymbol(idx, "__ZN12AirportItlwm5startEP9IOService");
        kp.clearError();
        supchanPublishU64("AirportItlwm-supchan-probe-aiStart", probe1);
        auto probe2 = kp.solveSymbol(idx, "__ZN12AirportItlwm15setCOUNTRY_CODEEP23IO80211SkywalkInterfaceP28apple80211_country_code_data");
        kp.clearError();
        supchanPublishU64("AirportItlwm-supchan-probe-aiSetCC", probe2);
        SYSLOG("aishim", "_ZTV12AirportItlwm not resolvable");
        // Don't return — keep going so pAiv-completed marker fires too.
        setLifeBit(14); // pAiv-completed
        return;
    }
    setLifeBit(9);  // pAiv-vtAddr-resolved

    // Plan A vtable patch: align our overrides to Apple's expected slots in
    // IO80211Controller. Our compile lays them out off-by-2 (header chain has
    // 2 fewer virtuals than Apple's real binary). Fix at runtime via memory
    // write, sidestepping kxld vtable validation.
    //
    // Apple expected slots (RE'd from KDK 15.7.4 IO80211Family wrapper call sites):
    //   slot 410 = getCARD_CAPABILITIES(SkywalkInterface*, capability_data*)
    //   slot 411 = getDRIVER_VERSION
    //   slot 412 = getHARDWARE_VERSION
    //   slot 414 = getPOWER, 415 = setPOWER
    //   slot 416 = getCOUNTRY_CODE, 417 = setCOUNTRY_CODE
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
            SYSLOG("aishim", "vtable patch: %s not found", s.name);
            continue;
        }
        mach_vm_address_t target = vtableAddr + (s.slot * 8 + 16);
        if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) == KERN_SUCCESS) {
            *reinterpret_cast<uint64_t *>(target) = static_cast<uint64_t>(fn);
            MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
            kprintf("[aishim] vtable[%u] = %s @ 0x%llx\n", s.slot, s.name, fn);
        } else {
            SYSLOG("aishim", "vtable[%u] setKernelWriting fail", s.slot);
        }
    }
    setLifeBit(10); // pAiv-controllerSlots-attempted
    setLifeBit(14); // pAiv-completed
    supchanPublishStr("AirportItlwm-supchan-pPv-completed", "yes");
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
