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
            txWithPool = kp.solveSymbol(idx,
                "__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PPK15IOSkywalkPacketjPvES9_j");
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
        ADDPR(plugin).init();
    }
};
