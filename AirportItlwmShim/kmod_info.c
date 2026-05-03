/*
 * kmod_info.c
 *
 * Emits the _kmod_info symbol and the _realmain / _antimain glue that
 * libkmod.a's c_start.o and c_stop.o reference. Without this object,
 * the produced kext has no _kmod_info symbol, which causes
 * OcAppleKernelLib's KextFindKmodAddress() to fail and OpenCore to
 * reject the kext during prelinked injection with EFI_INVALID_PARAMETER.
 *
 * Why we hand-roll this instead of relying on Xcode-generated machinery:
 * the Shim is built by a standalone shell script (no .xcodeproj), and
 * the standard Apple kext template ordinarily emits _kmod_info from a
 * compiler-recognised macro inside a kext target's main translation
 * unit. Defining it explicitly here keeps the build self-contained.
 *
 * Symbol names emitted (after C name mangling):
 *   _kmod_info          (kmod_info_t struct, populated via KMOD_EXPLICIT_DECL)
 *   _realmain           (function pointer to AirportItlwmShim_kern_start)
 *   _antimain           (function pointer to AirportItlwmShim_kern_stop)
 *   _kext_apple_cc      (compiler version stamp)
 *
 * The libkmod.a/c_start.o object provides _start, which references
 * both _kmod_info and _realmain. Likewise c_stop.o provides _stop and
 * references _antimain.
 */

#include <mach/kmod.h>

/*
 * Lilu's plugin_start.cpp defines these via the ADDPR() macro, which
 * concatenates PRODUCT_NAME ("AirportItlwmShim" passed via -D from
 * build.sh) with the suffix. So the actual C symbol names are
 * AirportItlwmShim_kern_start and AirportItlwmShim_kern_stop.
 */
extern kern_return_t AirportItlwmShim_kern_start(kmod_info_t *ki, void *data);
extern kern_return_t AirportItlwmShim_kern_stop(kmod_info_t *ki, void *data);

/*
 * KMOD_EXPLICIT_DECL expands to: kmod_info_t kmod_info = { ... };
 * We re-declare it with default visibility first so the -fvisibility=hidden
 * CFLAG doesn't mark it as a local-only symbol. NVMeFix's _kmod_info is
 * exposed as type 'D' (global data) in nm output; we want to match.
 */
__attribute__((visibility("default"))) extern kmod_info_t kmod_info;

KMOD_EXPLICIT_DECL(com.zxystd.AirportItlwmShim, "1.0.0",
                   AirportItlwmShim_kern_start,
                   AirportItlwmShim_kern_stop)

__private_extern__ kmod_start_func_t *_realmain = AirportItlwmShim_kern_start;
__private_extern__ kmod_stop_func_t  *_antimain = AirportItlwmShim_kern_stop;
__private_extern__ int                _kext_apple_cc = __APPLE_CC__;
