//
//  AirportItlwmV2.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmV2.hpp"
#include <IOKit/IOUserClient.h>
#include <sys/_netstat.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>

#include "AirportItlwmSkywalkInterface.hpp"
#include "IOPCIEDeviceWrapper.hpp"
#include "AirportItlwmShim_glue.hpp"

#if __IO80211_TARGET >= __MAC_15_0
// Path B: BSD KPI 自建 ifnet, 完全绕开 IO80211Family. 详见 docs/011 (TBD).
extern "C" {
#include <net/kpi_interface.h>
#include <net/if_ether.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <sys/sockio.h>
}
// macOS SDK 不暴露 IFT_IEEE80211 (FreeBSD/NetBSD 历史值 71).
#ifndef IFT_IEEE80211
#define IFT_IEEE80211 0x47    /* 71 */
#endif

// Apple 私有 KPI 来自 xnu/bsd/net/kpi_interface.h (xnu-11417.140.69 source).
// SDK header 没暴露 ifnet_allocate_extended, 但 kernel binary 有 export
// (nm 验证: T _ifnet_allocate_extended). extern "C" declare + kxld load 时
// 链接 kernel symbol. Apple 自家 dext / kext 都通过这条路设 subfamily.
//
// __getIfListCopy acceptance criteria (反编译 IO80211.framework 验证):
//   ioctl(SIOCGIFTYPE) 返 0 + ifr.type == IFT_ETHER (6) + sub at +24 == 3 → accept
//   OR ioctl(SIOCGIFMEDIA) 返 0 + (ifm_current & 0xe0) == 0x80 → accept
// 我们走第一条 — 设 type=IFT_ETHER + subfamily=IFNET_SUBFAMILY_WIFI(3).

typedef u_int32_t ifnet_subfamily_t;
typedef u_int32_t ifnet_family_t;
typedef errno_t (*ifnet_pre_enqueue_func)(ifnet_t, mbuf_t);
typedef errno_t (*ifnet_start_func)(ifnet_t);
typedef errno_t (*ifnet_ctl_func)(ifnet_t, int, void *);
typedef errno_t (*ifnet_input_poll_func)(ifnet_t, u_int32_t, u_int32_t,
                                          mbuf_t *, mbuf_t *, u_int32_t *,
                                          u_int32_t *);
typedef errno_t (*ifnet_framer_extended_func)(ifnet_t, mbuf_t *,
                                               const struct sockaddr *,
                                               const char *, const char *,
                                               u_int32_t *, u_int32_t *);
typedef void (*ifnet_free_func)(ifnet_t);

#define IFNET_INIT_VERSION_2     2
#define IFNET_INIT_LEGACY        0x1
#define IFNET_SUBFAMILY_WIFI     3
#define IFNET_FAMILY_ETHERNET_2  2

// xnu/bsd/net/kpi_interface.h struct ifnet_init_eparams (xnu-11417.140.69)
// 字段顺序必须严格匹配, 大小必须 ≥ 0x158 (344 bytes; kernel checks).
struct macos_ifnet_init_eparams {
    u_int32_t ver;
    u_int32_t len;
    u_int32_t flags;
    /* uniqueid_len 紧贴 uniqueid 之前还是之后? xnu source: name 在 unit 之前 */
    const void *uniqueid;
    u_int32_t uniqueid_len;
    const char *name;
    u_int32_t unit;
    ifnet_family_t family;
    u_int32_t type;
    u_int32_t sndq_maxlen;
    ifnet_output_func output;
    ifnet_pre_enqueue_func pre_enqueue;
    ifnet_start_func start;
    ifnet_ctl_func output_ctl;
    u_int32_t output_sched_model;
    u_int32_t output_target_qdelay;
    u_int64_t output_bw;
    u_int64_t output_bw_max;
    u_int64_t output_lt;
    u_int64_t output_lt_max;
    u_int16_t start_delay_qlen;
    u_int16_t start_delay_timeout;
    u_int32_t _reserved_p[3];
    ifnet_input_poll_func input_poll;
    ifnet_ctl_func input_ctl;
    u_int32_t rcvq_maxlen;
    u_int32_t __reserved_p;
    u_int64_t input_bw;
    u_int64_t input_bw_max;
    u_int64_t input_lt;
    u_int64_t input_lt_max;
    u_int64_t ___reserved_p[2];
    ifnet_demux_func demux;
    ifnet_add_proto_func add_proto;
    ifnet_del_proto_func del_proto;
    ifnet_check_multi check_multi;
    ifnet_framer_func framer;
    void *softc;
    ifnet_ioctl_func ioctl;
    ifnet_set_bpf_tap set_bpf_tap;
    ifnet_detached_func detach;
    ifnet_event_func event;
    const void *broadcast_addr;
    u_int32_t broadcast_len;
    ifnet_framer_extended_func framer_extended;
    ifnet_subfamily_t subfamily;
    u_int16_t tx_headroom;
    u_int16_t tx_trailer;
    u_int32_t rx_mit_ival;
    u_int32_t ____reserved_p;
    ifnet_free_func free_func;
};

extern "C" {
errno_t ifnet_allocate_extended(const struct macos_ifnet_init_eparams *init,
                                 ifnet_t *interface);
// kernel public KPI for crossing user/kernel address boundary.
// SDK header 用 __builtin_object_size 做 fortify-source wrapper, 但我们
// 这里要直接拿 raw symbol — undef 掉 macro, 让 extern decl 链 kernel _copyin.
#undef copyin
#undef copyout
int copyin(uint64_t uaddr, void *kaddr, size_t len);
int copyout(const void *kaddr, uint64_t uaddr, size_t len);
}
#endif

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportItlwm, IO80211Controller);
OSDefineMetaClassAndStructors(CTimeout, OSObject)

IO80211WorkQueue *_fWorkloop;
IOCommandGate *_fCommandGate;

#if __IO80211_TARGET >= __MAC_15_0
// Function pointers populated from IOResources properties published by
// AirportItlwmShim.kext (Lilu plugin). See AirportItlwmShim_glue.hpp.
TxWithPoolFn  gShimTxWithPool  = nullptr;
RxWithPoolFn  gShimRxWithPool  = nullptr;
PostMessageFn gShimPostMessage = nullptr;

bool resolveSequoiaShimSymbols(void)
{
    IOService *res = IOService::getResourceService();
    if (!res) return false;

    auto pull = [&](const char *key) -> uint64_t {
        OSObject *o = res->getProperty(key);
        OSData *d = OSDynamicCast(OSData, o);
        if (!d || d->getLength() != sizeof(uint64_t)) return 0;
        uint64_t v = 0;
        memcpy(&v, d->getBytesNoCopy(), sizeof(v));
        return v;
    };

    gShimTxWithPool  = (TxWithPoolFn) pull("AirportItlwm-IOSkywalkTxSubmissionQueue-withPool");
    gShimRxWithPool  = (RxWithPoolFn) pull("AirportItlwm-IOSkywalkRxCompletionQueue-withPool");
    gShimPostMessage = (PostMessageFn)pull("AirportItlwm-IO80211Controller-postMessage");

    return gShimTxWithPool != nullptr && gShimRxWithPool != nullptr;
}
#endif

#if __IO80211_TARGET >= __MAC_15_0
// Stub Skywalk TX submission action: required by withPool() but the
// actual datapath remains the legacy IOEthernetInterface path. Returning
// kIOReturnSuccess (0) signals "we processed all `count` packets". The
// queue still drains correctly because Apple's skywalk dispatcher
// recycles packets after this callback regardless of return value.
// Sequoia 15.7.5 ground truth: callbacks return unsigned int, not IOReturn.
// kxld binds withPool() by mangled symbol — IOReturn vs unsigned int produces
// different mangling ("PFi..." vs "PFj..."), and Apple only exports the "j"
// variant. Returning IOReturn left withPool unresolved -> driver silently
// not loaded.
static unsigned int
skywalkTxAction(OSObject *owner, IOSkywalkTxSubmissionQueue *queue,
                IOSkywalkPacket * const *packets, UInt32 count, void *refCon)
{
    (void)owner; (void)queue; (void)packets; (void)refCon;
    return 0;
}

// Stub Skywalk RX completion action: completion notification only —
// actual RX injection still goes through the legacy if_input path.
static unsigned int
skywalkRxAction(OSObject *owner, IOSkywalkRxCompletionQueue *queue,
                IOSkywalkPacket **packets, UInt32 count, void *refCon)
{
    (void)owner; (void)queue; (void)packets; (void)refCon;
    return 0;
}

// Path B Phase 1: 用 BSD KPI public API 自建 ifnet, type=IFT_IEEE80211.
// 这是 architectural workaround: H4 隔离了 IO80211Family panic (跳过 fNetIf->attach),
// 但那样 airportd enumerate 不到我们. 这里 attach 一个 BSD-only 的 wifi-typed
// ifnet, airportd 通过 getifaddrs+SIOCGIFMEDIA 看到 IFM_IEEE80211 接受我们.
// 完全不依赖 IO80211Family 私有 vtable layout, 用稳定的 BSD KPI public API.

ifnet_t gBsdWlanIfnet = NULL;

// TX output: Phase 1 stub. Phase 3 把 packets 喂给 OpenBSD 80211 stack TX path.
static errno_t bsd_wlan_output(ifnet_t ifp, mbuf_t packet) {
    (void)ifp;
    if (packet) mbuf_freem_list(packet);
    return 0;
}

// macOS 真实 ifmediareq layout (跟 macOS user-space SDK <net/if.h> 一致):
//   #pragma pack(4); struct { char[16]; int*5; int*; }; total 44 bytes
// 不要用 fork 的 OpenBSD ifmediareq (64 bytes uint64 字段), 那个 layout 跟实际
// macOS kernel buffer 不匹配, 会写超 caller buffer + 字段 offset 错位.
struct __attribute__((packed)) macos_ifmediareq {
    char  ifm_name[16];       // 0..15
    int   ifm_current;        // 16..19
    int   ifm_mask;           // 20..23
    int   ifm_status;         // 24..27
    int   ifm_active;         // 28..31
    int   ifm_count;          // 32..35
    int  *ifm_ulist;          // 36..43 (packed 4-align ptr)
};
static_assert(sizeof(macos_ifmediareq) == 44, "macos_ifmediareq must be 44 bytes");

// macOS kernel real SIOCGIFMEDIA = _IOWR('i', 56, sizeof(macos_ifmediareq=44))
//   IOC_INOUT(0xC0000000) | (44<<16=0x002C0000) | (105<<8=0x6900) | 56(0x38)
//   = 0xC02C6938
#define MACOS_SIOCGIFMEDIA 0xC02C6938UL

// IOCTL handler: Phase 1 minimal — 只处理 SIOCGIFMEDIA 让 airportd 接受我们,
// apple80211 ioctl SIOCSA80211/SIOCGA80211 在 Phase 2 再 dispatch.
static errno_t bsd_wlan_ioctl(ifnet_t ifp, unsigned long cmd, void *arg) {
    (void)ifp;
    XYLog("PathB bsd_wlan_ioctl cmd=0x%lx\n", cmd);

    if (cmd == MACOS_SIOCGIFMEDIA) {
        // 用 macos_ifmediareq layout (44 bytes packed) + 显式 macOS 数值
        // (不用 IFM_* macro, 因为 fork OpenBSD if_media.h 把 IFM_IEEE80211
        // redefine 为 0x400ULL, 跟 macOS 0x80 不一致).
        struct macos_ifmediareq *ifmr = (struct macos_ifmediareq *)arg;
        ifmr->ifm_current = 0x80;  // IFM_IEEE80211 | IFM_AUTO (macOS value)
        ifmr->ifm_active  = 0x80;
        ifmr->ifm_mask    = 0;
        ifmr->ifm_status  = 0x3;   // IFM_AVALID | IFM_ACTIVE
        ifmr->ifm_count   = 0;
        XYLog("PathB GIFMEDIA: written 0x80\n");
        return 0;
    }
    // Apple 私有 ioctl 0xC020699F (size=32=sizeof(ifreq), num=159, group='i').
    // 反编译 IO80211.framework _getIfListCopy 显示主路径用这个 cmd 而非
    // SIOCGIFTYPE; acceptance criteria: 返 0 + (ifr.functional_type & 0xe0) == 0x80.
    // ifr layout: char ifr_name[16], 然后 ifr_ifru union from offset 16.
    // ifru_functional_type 是 u_int32_t at offset 16. 写 0x80 让 main path 接受.
    if (cmd == 0xC020699FUL) {
        *(uint32_t *)((char *)arg + 16) = 0x80;
        XYLog("PathB private 0xC020699F: written functional_type=0x80\n");
        return 0;
    }
    // Phase 2: 自实 inline apple80211 ioctl handler — V2 class 没 apple80211Request
    // member (AirportSTAIOCTL.cpp 不在 Sequoia15 target). 先 implement CARD_CAPABILITIES
    // 让 BindToInterfaceWithIOCTL 过, 后续 req_type 按 airportd log 迭代加.
    //
    // 注意: 不能用 SIOCSA80211/SIOCGA80211 macro — fork apple80211_ioctl.h 抄旧
    // 版本 hardcoded 值 (0x80306{9C8,C9} 暗示 48-byte struct), 但 Sequoia 15.7.5
    // kernel 实际发的是 0xc02869{c8,c9} (40-byte struct, modern 64-bit layout).
    // dmesg 实测: cmd=0xc02869c9 命中 unhandled. 用字面量绕过 macro bug.
    static const unsigned long REAL_SIOCSA80211 = 0xc02869c8UL;
    static const unsigned long REAL_SIOCGA80211 = 0xc02869c9UL;
    if (cmd == REAL_SIOCSA80211 || cmd == REAL_SIOCGA80211) {
        struct apple80211req *req = (struct apple80211req *)arg;
        if (req->req_len > 65536) return EINVAL;
        bool is_get = (cmd == REAL_SIOCGA80211);

        size_t klen = req->req_len;
        void *kbuf = NULL;
        if (klen > 0) {
            kbuf = IOMalloc(klen);
            if (!kbuf) return ENOMEM;
            bzero(kbuf, klen);
            if (!is_get && req->req_data) {
                if (copyin((uint64_t)(uintptr_t)req->req_data, kbuf, klen) != 0) {
                    IOFree(kbuf, klen);
                    return EFAULT;
                }
            }
        }

        IOReturn r = kIOReturnSuccess;
        // Permissive Phase 2 dispatch — 让 airportd 至少把 en99 当 "正常但未关联"
        // 的 wifi 接口接受. 大多数 GET 用 zero-buffer (kbuf 已 bzero), version=1.
        // SET 全 no-op success. 实际 scan/associate 等到 Phase 3 再 wire.
        if (is_get && klen >= 4) {
            // 几乎所有 apple80211 data struct 第一个字段是 u_int32_t version
            uint32_t version = 1; // APPLE80211_VERSION
            memcpy(kbuf, &version, 4);
        }
        switch (req->req_type) {
        case 12: // APPLE80211_IOC_CARD_CAPABILITIES
            if (is_get && klen >= 18) {
                // struct apple80211_capability_data { u_int32_t version; u_int8_t cap[14]; }
                uint8_t *cap = (uint8_t *)kbuf + 4;
                cap[0] = (1 << 1) | (1 << 3);                       // TKIP + AES_CCM
                cap[1] = (1 << (9-8)) | (1 << (10-8))               // SHSLOT + SHPREAMBLE
                       | (1 << (12-8)) | (1 << (13-8)) | (1 << (14-8)); // TKIPMIC + WPA1 + WPA2
                cap[2] = 0xFF;
                cap[3] = 0x2B;
                cap[4] = 0xAD;
                cap[5] = 0x80 | 0x0C;
                cap[6] = 0x8 | 0x4 | 0x80;
            }
            break;
        case 13: // APPLE80211_IOC_STATE — 0 = INIT (idle, not connected)
            // kbuf 已 bzero version=1, state field at offset 4 = 0 (INIT)
            break;
        case 19: // APPLE80211_IOC_POWER
            // struct apple80211_power_data { u32 version; u32 num_radios; u32 power_state[4]; } = 24 bytes
            // 报 power=ON 让 airportd 解锁 scan/associate 路径. SET 也 no-op success.
            if (is_get && klen >= 24) {
                ((uint32_t *)kbuf)[1] = 1;  // num_radios = 1
                ((uint32_t *)kbuf)[2] = 1;  // power_state[0] = ON
            }
            break;
        case 1:   // APPLE80211_IOC_SSID — return zero ssid (not connected)
        case 9:   // APPLE80211_IOC_BSSID — return zero bssid
        case 103: // APPLE80211_IOC_CURRENT_NETWORK — return empty network
        default:
            // 其他都返 zero buffer success — airportd 把它解读为 "无数据/未关联"
            break;
        }

        int rc = 0;
        if (r != kIOReturnSuccess) {
            rc = EOPNOTSUPP;
        } else if (is_get && klen > 0 && req->req_data) {
            if (copyout(kbuf, (uint64_t)(uintptr_t)req->req_data, klen) != 0) {
                rc = EFAULT;
            }
        }

        XYLog("PathB SIOC%cA80211 type=%d len=%u → 0x%x errno=%d\n",
              is_get ? 'G' : 'S', req->req_type, req->req_len, r, rc);

        if (kbuf) IOFree(kbuf, klen);
        return rc;
    }
    if (cmd == (unsigned long)SIOCSIFFLAGS ||
        cmd == (unsigned long)SIOCSIFADDR ||
        cmd == (unsigned long)SIOCADDMULTI ||
        cmd == (unsigned long)SIOCDELMULTI) {
        return 0;
    }
    XYLog("PathB unhandled cmd=0x%lx\n", cmd);
    return EOPNOTSUPP;
}

// Plan A: Apple80211UserClient on stub IOService.
// airportd 的控制平面 (scan/associate/setpower/...) 不走 BSD ioctl, 走
// IOServiceOpen → IOUserClient::externalMethod. 之前 stub 没 UserClient 所以
// IOServiceOpen 返 -536870201/0xe00002c7 = kIOReturnUnsupported, airportd
// fallback 到 BSD ioctl, 但 BSD ioctl 只接受 GET, SET 都跑不到我们.
//
// 实现 IOUserClient + 给 stub publish IOUserClientClass="BsdWlanUserClient",
// IOServiceOpen 就成功. 第一版 externalMethod 全部 log selector 返 Unsupported,
// 用 dmesg 看 airportd 实际调啥, 一个 selector 一个加.
class BsdWlanUserClient : public IOUserClient {
    OSDeclareDefaultStructors(BsdWlanUserClient)
public:
    virtual bool initWithTask(task_t owningTask, void *securityID,
                              UInt32 type, OSDictionary *properties) override {
        XYLog("BsdWlanUserClient: initWithTask type=%u\n", (uint32_t)type);
        return IOUserClient::initWithTask(owningTask, securityID, type, properties);
    }

    virtual bool start(IOService *provider) override {
        XYLog("BsdWlanUserClient: start provider=%p\n", provider);
        return IOUserClient::start(provider);
    }

    virtual IOReturn clientClose() override {
        XYLog("BsdWlanUserClient: clientClose\n");
        terminate();
        return kIOReturnSuccess;
    }

    virtual IOReturn externalMethod(uint32_t selector,
                                     IOExternalMethodArguments *args,
                                     IOExternalMethodDispatch *dispatch,
                                     OSObject *target,
                                     void *reference) override {
        XYLog("BsdWlanUserClient: externalMethod sel=%u scIn=%u scOut=%u stIn=%llu stOut=%llu\n",
              selector,
              (uint32_t)args->scalarInputCount,
              (uint32_t)args->scalarOutputCount,
              (unsigned long long)args->structureInputSize,
              (unsigned long long)args->structureOutputSize);
        // 先不 dispatch, 全返 Unsupported. airportd 会看到 EINVAL/Unsupported 但
        // IOServiceOpen 已经成功 → 它至少不再 fallback ioctl.
        return kIOReturnUnsupported;
    }
};

#define super IOUserClient
OSDefineMetaClassAndStructors(BsdWlanUserClient, IOUserClient)
#undef super
#define super IO80211Controller // 恢复, V2.cpp 后面 method 是 AirportItlwm

// Phase 1: 创建并 attach 我们自己的 BSD wifi ifnet.
// 用 ifnet_allocate_extended (私有 KPI) + ifnet_init_eparams 设 subfamily=WIFI(3),
// 让 _getIfListCopy 主路径接受 (type=IFT_ETHER + sub=WIFI).
static bool createBsdWlanIfnet(AirportItlwm *self, const u_int8_t mac[6]) {
    if (gBsdWlanIfnet) return true;

    static const u_int8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

    struct macos_ifnet_init_eparams init = {};
    init.ver         = IFNET_INIT_VERSION_2;
    init.len         = sizeof(struct macos_ifnet_init_eparams);
    init.flags       = IFNET_INIT_LEGACY;       // 我们用 output callback 不是 start
    init.name        = "en";
    init.unit        = 99;
    init.family      = IFNET_FAMILY_ETHERNET_2; // 2
    init.type        = 6;                       // IFT_ETHER literal (fork's OpenBSD if_types.h 覆盖了 macOS 版) — airportd main path 期望
    init.subfamily   = IFNET_SUBFAMILY_WIFI;    // 3 — 让 ifnet_subfamily() 返 WIFI
    init.output      = bsd_wlan_output;
    init.demux       = ether_demux;
    init.add_proto   = ether_add_proto;
    init.del_proto   = ether_del_proto;
    init.check_multi = ether_check_multi;
    init.ioctl       = bsd_wlan_ioctl;
    init.softc       = self;
    init.broadcast_addr = bcast;
    init.broadcast_len  = 6;

    errno_t r = ifnet_allocate_extended(&init, &gBsdWlanIfnet);
    if (r != 0) {
        XYLog("Path B: ifnet_allocate_extended err=%d (link fail?)\n", r);
        return false;
    }
    XYLog("Path B: ifnet_allocate_extended OK type=IFT_ETHER subfamily=WIFI\n");

    ifnet_set_lladdr(gBsdWlanIfnet, mac, 6);
    ifnet_set_mtu(gBsdWlanIfnet, 1500);
    // 设 IFF_UP | IFF_RUNNING — airportd._getIfListCopy 可能 require interface
    // up/running 才 enumerate. flags 8802 (没 UP) 时 airportd reject.
    ifnet_set_flags(gBsdWlanIfnet,
                    IFF_UP | IFF_RUNNING |
                    IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX,
                    0xffff);

    struct sockaddr_dl ll_addr = {};
    ll_addr.sdl_len    = sizeof(ll_addr);
    ll_addr.sdl_family = AF_LINK;
    ll_addr.sdl_type   = IFT_IEEE80211;
    ll_addr.sdl_alen   = 6;
    memcpy(LLADDR(&ll_addr), mac, 6);

    r = ifnet_attach(gBsdWlanIfnet, &ll_addr);
    if (r != 0) {
        XYLog("Path B: ifnet_attach err=%d\n", r);
        ifnet_release(gBsdWlanIfnet);
        gBsdWlanIfnet = NULL;
        return false;
    }

    XYLog("Path B: BSD wifi ifnet attached ok (IFT_IEEE80211)\n");

    // Phase 1.5: 建一个 stub IOService 让 Apple80211FindService("en99") 找得到.
    // 反编译 IO80211.framework _isInfraInterface (Sequoia 15.7.5) 显示:
    //   1. _Apple80211FindService(name) 用 IOServiceGetMatchingService
    //      matching = {IOPropertyMatch: {IOInterfaceName: <name>}}
    //   2. 在返回的 IOService 上读 CFString property "IO80211InterfaceRole"
    //   3. CFStringCompare 与 "Infrastructure" — 相等才 accept 进 ifList
    //
    // en99 是纯 BSD ifnet 没 IOKit binding, 所以 FindService 返 0 → reject.
    // 挂个 IOService stub child 上来, 带这两个 property 即可命中.
    IOService *stub = OSTypeAlloc(IOService);
    if (stub) {
        if (stub->init() && stub->attach(self)) {
            char ifname[16];
            snprintf(ifname, sizeof(ifname), "%s%d",
                     ifnet_name(gBsdWlanIfnet), ifnet_unit(gBsdWlanIfnet));
            stub->setProperty("IOInterfaceName", ifname);
            stub->setProperty("IO80211InterfaceRole", "Infrastructure");
            // Plan A: 让 IOServiceOpen 走我们的 UserClient. kxld 会按 class
            // name 找到 BsdWlanUserClient 的 OSMetaClass 实例化.
            stub->setProperty("IOUserClientClass", "BsdWlanUserClient");
            stub->registerService();
            XYLog("Path B: stub IOService for %s registered (Role=Infrastructure)\n", ifname);
        } else {
            stub->release();
            XYLog("Path B: stub IOService init/attach failed\n");
        }
    }

    return true;
}
#endif

void AirportItlwm::releaseAll()
{
    OSSafeReleaseNULL(driverLogPipe);
    OSSafeReleaseNULL(driverDataPathPipe);
    OSSafeReleaseNULL(driverSnapshotsPipe);
    OSSafeReleaseNULL(driverFaultReporter);
#if __IO80211_TARGET >= __MAC_15_0
    OSSafeReleaseNULL(fIO80211FaultReporter);
    OSSafeReleaseNULL(ccFaultReporter);
    OSSafeReleaseNULL(driverLogStream);
    OSSafeReleaseNULL(fTxQueue);
    OSSafeReleaseNULL(fRxQueue);
    OSSafeReleaseNULL(fTxPool);
    OSSafeReleaseNULL(fRxPool);
#endif
    if (fHalService) {
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloop) {
        if (_fCommandGate) {
//            _fCommandGate->disable();
            _fWorkloop->removeEventSource(_fCommandGate);
            _fCommandGate->release();
            _fCommandGate = NULL;
        }
        if (scanSource) {
            scanSource->cancelTimeout();
            scanSource->disable();
            _fWorkloop->removeEventSource(scanSource);
            scanSource->release();
            scanSource = NULL;
        }
        if (fWatchdogWorkLoop && watchdogTimer) {
            watchdogTimer->cancelTimeout();
            fWatchdogWorkLoop->removeEventSource(watchdogTimer);
            watchdogTimer->release();
            watchdogTimer = NULL;
            fWatchdogWorkLoop->release();
            fWatchdogWorkLoop = NULL;
        }
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
    unregistPM();
}

void AirportItlwm::
eventHandler(struct ieee80211com *ic, int msgCode, void *data)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller);
    IO80211SkywalkInterface *interface = that->fNetIf;
    if (!interface)
        return;
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            interface->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, 0);
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            interface->postMessage(APPLE80211_M_ASSOC_DONE, NULL, 0, 0);
            break;
        case IEEE80211_EVT_STA_DEAUTH:
            interface->postMessage(APPLE80211_M_DEAUTH_RECEIVED, NULL, 0, 0);
            break;
        default:
            break;
    }
}

void AirportItlwm::watchdogAction(IOTimerEventSource *timer)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    (*ifp->if_watchdog)(ifp);
    updateLQMIfChanged();
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
}

// Compute and report Link Quality Metric. Replaces deprecated setLinkQualityMetric
// path on Sonoma 14.4+ Skywalk schema. SCDynamicStore key
// State:/Network/Interface/<if>/LinkQuality must be >= 11 for apsd /
// PCInterfaceUsabilityMonitor to consider the interface usable, otherwise the
// entire iServices stack (iMessage / FaceTime / AirDrop) refuses to use WiFi.
void AirportItlwm::updateLQMIfChanged()
{
    if (!fNetIf || !fHalService) {
        return;
    }
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (!ic || ic->ic_state != IEEE80211_S_RUN) {
        return;  // not associated; link-state mechanism signals unusable separately
    }
    // Snapshot ic_bss to a local — ieee80211 layer can clear it from another
    // workloop between our check and deref.
    struct ieee80211_node *ni = ic->ic_bss;
    if (!ni) {
        return;
    }
    // ni_rssi is normalized 0..(IWM_MAX_DBM - IWM_MIN_DBM)
    // i.e. 0 = -100 dBm, 67 = -33 dBm.
    int rssi_norm = ni->ni_rssi;
    unsigned long long lq;
    if (rssi_norm >= 30) {
        lq = 100;                // >= -70 dBm: excellent
    } else if (rssi_norm >= 15) {
        lq = 50;                 // >= -85 dBm: mediocre
    } else {
        lq = 25;                 // weak; still > iServices threshold (11)
    }
    if (lq != fLastReportedLQM) {
        fLastReportedLQM = lq;
        fNetIf->setLQM(lq);
    }
}

void AirportItlwm::fakeScanDone(OSObject *owner, IOTimerEventSource *sender)
{
    UInt32 msg = 0;
    AirportItlwm *that = (AirportItlwm *)owner;
    that->fNetIf->postMessage(APPLE80211_M_SCAN_DONE, &msg, 4, 0);
}

bool AirportItlwm::init(OSDictionary *properties)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    bool ret = super::init(properties);
    awdlSyncEnable = true;
    power_state = 0;
    fLastReportedLQM = 0;
    memset(geo_location_cc, 0, sizeof(geo_location_cc));
    return ret;
}

IOService* AirportItlwm::probe(IOService *provider, SInt32 *score)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    IOPCIEDeviceWrapper *wrapper = OSDynamicCast(IOPCIEDeviceWrapper, provider);
    if (!wrapper) {
        XYLog("%s Not a IOPCIEDeviceWrapper instance\n", __FUNCTION__);
        return NULL;
    }
    pciNub = wrapper->pciNub;
    fHalService = wrapper->fHalService;
    if (!pciNub || !fHalService) {
        XYLog("%s Not a valid IOPCIEDeviceWrapper instance\n", __FUNCTION__);
        return NULL;
    }
    return super::probe(provider, score);
}

#define LOWER32(x)  ((uint64_t)(x) & 0xffffffff)
#define HIGHER32(x) ((uint64_t)(x) >> 32)

bool AirportItlwm::
initCCLogs()
{
    CCPipeOptions driverLogOptions = { 0 };
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 1;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "Itlwm_Logs", sizeof(driverLogOptions.file_name));
    snprintf(driverLogOptions.name, sizeof(driverLogOptions.name), "wlan%d", 0);
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = 0x1000000;
    driverLogOptions.pad10 = 2;
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    driverLogPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "DriverLogs", &driverLogOptions);
    XYLog("%s driverLogPipeRet %d\n", __FUNCTION__, driverLogPipe != NULL);
    
    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 0;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "AppleBCMWLAN_Datapath", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = HIGHER32(0x202800000);
    driverLogOptions.pad10 = LOWER32(0x202800000);
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    driverDataPathPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "DatapathEvents", &driverLogOptions);
    XYLog("%s driverDataPathPipeRet %d\n", __FUNCTION__, driverDataPathPipe != NULL);
    
    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0x200000001;
    driverLogOptions.log_data_type = 2;
    strlcpy(driverLogOptions.file_name, "StateSnapshots", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.name, "0", sizeof(driverLogOptions.name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pipe_size = 128;
    driverSnapshotsPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "StateSnapshots", &driverLogOptions);
    XYLog("%s driverSnapshotsPipeRet %d\n", __FUNCTION__, driverSnapshotsPipe != NULL);
    
    CCStreamOptions faultReportOptions = { 0 };
    faultReportOptions.stream_type = 1;
    faultReportOptions.console_level = 0xFFFFFFFFFFFFFFFF;
#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia 15.7.5: must use the SUBCLASS factory CCDataStream::withPipeAndName,
    // not the abstract CCStream::withPipeAndName. The abstract factory returns
    // a CCStream that, when OSDynamicCast<CCDataStream>'d and wrapped via
    // CCFaultReporter::withStreamWorkloop, produces a faultReporter whose
    // registerCallbacks (slot 0x120) page-faults on null+0x38 inside Apple's
    // PeerManager::initWithInterface. Verified via disasm of failing chain
    // on commit 4a20c48.
    driverFaultReporter = CCDataStream::withPipeAndName(driverSnapshotsPipe, "FaultReporter", &faultReportOptions);
#else
    driverFaultReporter = CCStream::withPipeAndName(driverSnapshotsPipe, "FaultReporter", &faultReportOptions);
#endif
    XYLog("%s driverFaultReporterRet %d\n", __FUNCTION__, driverFaultReporter != NULL);

#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia: wrap driverFaultReporter (CCStream, runtime-castable to
    // CCDataStream when stream_type=1) into IO80211FaultReporter via Apple's
    // CCFaultReporter::withStreamWorkloop -> IO80211FaultReporter::allocWithParams.
    // Apple's IO80211Controller::findAndAttachToFaultReporter calls our
    // getFaultReporterFromDriver() and stores the return into ivars+0x58 as
    // CCFaultReporter*. PeerManager later does vtable[0x120] on it expecting
    // CCFaultReporter::registerCallbacks. CCStream's vtable[0x120] is something
    // else -> NULL+0x38 release deref panic. Must return IO80211FaultReporter*.
    // Sequoia: Apple's IO80211ScanManager::commonInit (file 0x18d023) calls
    // controller->vtable[0xd40] (= getControllerGlobalLogger, slot 426) and
    // stores result as CCLogStream* in scanIvars[0xe8]. Then
    // createReportersAndLegend does fLogStream->shouldLog(1) which derefs
    // ((CCLogStream*)x)->ivars[0x90][0x58]. Must return a real CCLogStream
    // built from CCStream::withPipeAndName + OSDynamicCast<CCLogStream>.
    // Use the SUBCLASS factory CCLogStream::withPipeAndName (Apple-exported,
    // mangled __ZN11CCLogStream15withPipeAndNameEP6CCPipePKcPK15CCStreamOptions)
    // to get a real CCLogStream*. Previous attempt used CCStream:: (abstract
    // base) + OSDynamicCast — returned an invalid CCLogStream-cast that
    // panicked findAndAttachToFaultReporter +0x5A on vtable deref. The real
    // factory constructs a fully-initialized CCLogStream subclass instance.
    OSSafeReleaseNULL(driverLogStream);
    {
        CCStreamOptions logStreamOptions = { 0 };
        logStreamOptions.stream_type = 0;
        logStreamOptions.console_level = 0xFFFFFFFFFFFFFFFF;
        driverLogStream = CCLogStream::withPipeAndName(driverLogPipe, "DriverLogStream", &logStreamOptions);
    }
    XYLog("%s driverLogStreamRet %d\n", __FUNCTION__, driverLogStream != NULL);

    // 15.7.5 actual binary RE: IO80211FaultReporter class no longer exists;
    // ground truth nm of BootKC has no __ZTV20IO80211FaultReporter symbol.
    // Apple's IO80211Controller::findAndAttachToFaultReporter stores our
    // getFaultReporterFromDriver() return verbatim into ivars+0x58 as
    // CCFaultReporter*. PeerManager/ScanManager later call
    // CCFaultReporter::registerCallbacks via vtable[0x120] on it.
    // Just return the raw CCFaultReporter — no IO80211FaultReporter wrapper.
    OSSafeReleaseNULL(fIO80211FaultReporter);
    OSSafeReleaseNULL(ccFaultReporter);
    if (driverFaultReporter) {
        CCDataStream *fdStream = OSDynamicCast(CCDataStream, driverFaultReporter);
        if (fdStream) {
            IOWorkLoop *frWorkloop = IOWorkLoop::workLoop();
            if (frWorkloop) {
                ccFaultReporter = CCFaultReporter::withStreamWorkloop(fdStream, frWorkloop);
                // (workloop release intentionally dropped earlier — see
                // commit 4a20c48 — turned out unrelated to the actual panic.)
            }
        }
    }
    // Wrap raw CCFaultReporter into IO80211FaultReporter so PeerManager's
    // dispatch on slot 36 (byte 0x120) lands on the correct trampoline →
    // CCFaultReporter::registerCallbacks. Without this wrap, slot 36 of a
    // raw CCFaultReporter is inherited IORegistryEntry::copyProperty →
    // wrong dispatch → kernel page fault on null+0x38.
    if (ccFaultReporter) {
        fIO80211FaultReporter = IO80211FaultReporter::allocWithParams(ccFaultReporter);
    }
    XYLog("%s ccFaultReporterRet %d fIO80211FaultReporterRet %d\n", __FUNCTION__,
          ccFaultReporter != NULL, fIO80211FaultReporter != NULL);
    return driverLogPipe && driverDataPathPipe && driverSnapshotsPipe && driverFaultReporter
           && ccFaultReporter && fIO80211FaultReporter && driverLogStream;
#else
    return driverLogPipe && driverDataPathPipe && driverSnapshotsPipe && driverFaultReporter;
#endif
}

// Sequoia 15.7.5 IOLog suppression workaround for non-Apple kexts.
// setProperty on `this` only survives until our instance is destroyed (start
// failure → IOKit releases instance → IORegistry node + properties gone).
// Mirror to IOResources (long-lived registry entry) so trace_step survives
// destruction and we can post-mortem via `ioreg -k airportitlwm_trace`.
#define TRACE_STEP(s) do { \
    setProperty("trace_step", s); \
    IOService *_res = IOService::getResourceService(); \
    if (_res) _res->setProperty("airportitlwm_trace", s); \
} while (0)

bool AirportItlwm::start(IOService *provider)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    struct IOSkywalkEthernetInterface::RegistrationInfo registInfo;
    int boot_value = 0;

    // Boot trace via setProperty (Sequoia 抑制 IOLog, 用 ioreg 抓行号)
    TRACE_STEP("01_entered");

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);

#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia: super::start (Apple's IO80211Controller::start) calls
    // findAndAttachToFaultReporter (KDK 15.7.4 vmaddr 0x112ed5) which calls
    // slot 434 (getFaultReporterFromDriver) and panics with
    // "No ivars->_faultReporter" @IO80211Controller.cpp:3288 if it returns NULL.
    // Original itlwm calls initCCLogs at line 311 (after super::start) — too late.
    // Initialize CCLogs here so driverFaultReporter is valid when Apple asks.
    TRACE_STEP("01b_pre_initCCLogs_early");
    if (!initCCLogs()) {
        TRACE_STEP("FAIL_initCCLogs_early");
        return false;
    }
    TRACE_STEP("01c_post_initCCLogs_early");
#endif

    TRACE_STEP("02_pre_super_start");

#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia 15.7.x: Apple's IO80211Controller::start no longer indirectly
    // invokes createWorkQueue (Apple's base impl is a stub returning 0; not
    // called by super::start). But IO80211SkywalkInterface::start +0xD5 calls
    // controller->getWorkQueue() and stores result in ivars[0x110]+0x38.
    // IO80211Glue::initWithOptions @0x1a194 hard-checks that field non-NULL;
    // if NULL, returns false, parent free() path crashes in
    // IO80211Glue::freeResources on uninitialized list head (CR2=0).
    // Eager-init _fWorkloop here so super::start sees a valid workqueue.
    // KDK source: research/sequoia-port/kdk-extract/.../IO80211Family.kext
    // Documented in docs/009-sequoia-fix-plan.md.
    if (createWorkQueue() == NULL) {
        TRACE_STEP("FAIL_createWorkQueue_pre_super");
        return false;
    }
    TRACE_STEP("02b_post_createWorkQueue_pre_super");
#endif

    if (!super::start(provider)) {
        TRACE_STEP("FAIL_super_start");
        return false;
    }
    TRACE_STEP("03_post_super_start");
    pciNub->setBusMasterEnable(true);
    pciNub->setIOEnable(true);
    pciNub->setMemoryEnable(true);
    pciNub->configWrite8(0x41, 0);
    TRACE_STEP("04_pre_requestPowerDomain");
    if (pciNub->requestPowerDomainState(kIOPMPowerOn,
                                        (IOPowerConnection *) getParentEntry(gIOPowerPlane), IOPMLowestState) != IOPMNoErr) {
        TRACE_STEP("FAIL_requestPowerDomain");
        super::stop(provider);
        return false;
    }
    TRACE_STEP("05_pre_initPCIPowerManagment");
    if (initPCIPowerManagment(pciNub) == false) {
        TRACE_STEP("FAIL_initPCIPowerManagment");
        super::stop(pciNub);
        return false;
    }
    TRACE_STEP("06_post_initPCIPowerManagment");
    if (_fWorkloop == NULL) {
        TRACE_STEP("FAIL_no_workloop");
        XYLog("No _fWorkloop!!\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    TRACE_STEP("07_pre_commandGate");
    _fCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)AirportItlwm::tsleepHandler);
    if (_fCommandGate == 0) {
        TRACE_STEP("FAIL_no_commandGate");
        XYLog("No command gate!!\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    _fWorkloop->addEventSource(_fCommandGate);
    TRACE_STEP("08_pre_createMediumTables");
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        TRACE_STEP("FAIL_createMediumTables");
        XYLog("setup medium fail\n");
        releaseAll();
        return false;
    }
    TRACE_STEP("09_pre_initWithController");
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    fHalService->get80211Controller()->ic_event_handler = eventHandler;

    if (PE_parse_boot_argn("-novht", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOVHT;
    if (PE_parse_boot_argn("-noht40", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOHT40;

    TRACE_STEP("10_pre_halAttach");
    if (!fHalService->attach(pciNub)) {
        TRACE_STEP("FAIL_halAttach");
        XYLog("attach fail\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    TRACE_STEP("11_post_halAttach");
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        TRACE_STEP("FAIL_watchdogWorkloop");
        XYLog("init watchdog workloop fail\n");
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &AirportItlwm::watchdogAction));
    if (!watchdogTimer) {
        TRACE_STEP("FAIL_watchdogTimer");
        XYLog("init watchdog fail\n");
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    fWatchdogWorkLoop->addEventSource(watchdogTimer);
    scanSource = IOTimerEventSource::timerEventSource(this, &fakeScanDone);
    _fWorkloop->addEventSource(scanSource);
    scanSource->enable();

    TRACE_STEP("12_pre_newSkywalkInterface");
    fNetIf = new AirportItlwmSkywalkInterface;
#if __IO80211_TARGET >= __MAC_15_0
    // Sequoia 15.7.5: split init() per new vtable — Apple's
    // IO80211SkywalkInterface::init at slot 414 takes (IOService*) but our
    // own init() form is the framework-friendly path; bind the controller
    // afterwards so all subsequent ivars are wired before super::start.
    if (!((AirportItlwmSkywalkInterface *)fNetIf)->init() ||
        !((AirportItlwmSkywalkInterface *)fNetIf)->bindController(this)) {
        TRACE_STEP("FAIL_skywalkInit");
        XYLog("Skywalk interface init fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
#else
    if (!fNetIf->init(this)) {
        TRACE_STEP("FAIL_skywalkInit");
        XYLog("Skywalk interface init fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
#endif
    TRACE_STEP("13_post_skywalkInit");
#if __IO80211_TARGET >= __MAC_15_0
    // H1 实验 (skip setInterfaceRole=1 on Sequoia): 反编译确认 setInterfaceRole(1)
    // 写 ivars[0x110]+0x58=1, IO80211SkywalkInterface::start 检测到这个值后
    // 创建 IO80211Glue 存在 ivars[0x110]+0xd8.
    //
    // 之后 configd 调 GET_STATE 时:
    //   static getSTATE 看 Glue ptr 非 NULL → 调 sendIOUCToWcl
    //   → sendIOUCToWcl 内部 dispatch block 到 WCL workqueue 跑
    //   → block 内某个 deref ([req_data + 0x8]) 假定 kernel-side struct layout
    //     而 caller 给的是 8-byte buffer → 越界访问 → corrupt 调用方栈 canary
    //   → static getSTATE return 时 stack_chk_fail → kernel panic
    //
    // H1 假设: 跳过 setInterfaceRole(1) → Glue 不创建 → sendIOUCToWcl 不被调用 →
    //          boot 稳定不 panic.
    //
    // 副作用: 走 Glue 转发的 apple80211 ioctl 全部失败. 但目前 apple80211getSTATE
    //         本身就不识别 InfraProtocol class 走 fail path, 所以 effectively 0
    //         功能 regression.
    //
    // Sonoma 14.x 不受影响, 走原 setInterfaceRole(1) 路径.
    TRACE_STEP("13b_skip_setInterfaceRole_H1");
#else
    fNetIf->setInterfaceRole(1);
#endif
    fNetIf->setInterfaceId(1);

#if __IO80211_TARGET < __MAC_15_0
    // Sonoma 14.x: super::start doesn't need driverFaultReporter early —
    // initCCLogs runs here. Sequoia 15.x already ran initCCLogs before super::start.
    if (!initCCLogs()) {
        TRACE_STEP("FAIL_initCCLogs");
        XYLog("CCLog init fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
#endif
    TRACE_STEP("14_post_initCCLogs");
#if __IO80211_TARGET >= __MAC_15_0
    // H4 实验: 不让 fNetIf attach 到 IOService 树, framework 找不到我们 SkywalkInterface
    // → configd apple80211 ioctl 拿不到接口 → 不调 static getCARD_CAPABILITIES → 不 panic.
    //
    // H1 (skip setInterfaceRole) + H2 (driver getCARD_CAPABILITIES 返 Unsupported)
    // 都不消除 panic, 说明 corrupt 在 framework dispatch chain 早期, 不是 driver 写入.
    // 跳过整个 fNetIf 暴露能验证: panic 是否依赖于 framework 找到我们.
    //
    // 副作用: 完全没 WiFi (本来也没). driver 只剩 controller 注册, 接口完全 invisible.
    // 这是 panic source 隔离实验, 不是 fix.
    TRACE_STEP("14b_skip_fNetIf_attach_H4");
#else
    if (!fNetIf->attach(this)) {
        TRACE_STEP("FAIL_skywalkAttach");
        XYLog("attach to service fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
#endif
    TRACE_STEP("15_post_skywalkAttach");
    if (!attachInterface(fNetIf, this)) {
        TRACE_STEP("FAIL_attachInterface");
        XYLog("attach to interface fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    TRACE_STEP("16_post_attachInterface");
    if (!IONetworkController::attachInterface((IONetworkInterface **)&bsdInterface, true)) {
        TRACE_STEP("FAIL_IONCAttachInterface");
        XYLog("attach to IONetworkController interface fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    TRACE_STEP("17_post_IONCAttachInterface");
    memset(&registInfo, 0, sizeof(registInfo));
    if (!fNetIf->initRegistrationInfo(&registInfo, 1, sizeof(registInfo))) {
        TRACE_STEP("FAIL_initRegistrationInfo");
        XYLog("initRegistrationInfo fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    TRACE_STEP("18_post_initRegistrationInfo");
#if __IO80211_TARGET < __MAC_15_0
    // Sonoma 14.x path: legacy upstream code manually populated the
    // mExpansionData/mExpansionData2 RegistrationInfo slots to work around an
    // older itlwm class layout. KEEP for Sonoma — it's been verified working.
    fNetIf->mExpansionData->fRegistrationInfo = (struct IOSkywalkNetworkInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkNetworkInterface::RegistrationInfo));
    fNetIf->mExpansionData2->fRegistrationInfo = (struct IOSkywalkEthernetInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkEthernetInterface::RegistrationInfo));
    memcpy(fNetIf->mExpansionData->fRegistrationInfo, &registInfo, sizeof(registInfo));
    memcpy(fNetIf->mExpansionData2->fRegistrationInfo, &registInfo, sizeof(registInfo));
    if (fNetIf->getInterfaceRole() == 1)
        fNetIf->deferBSDAttach(true);
    TRACE_STEP("19_pre_skywalkStart");
    fNetIf->start(this);
    TRACE_STEP("20_post_skywalkStart");
#else
    // Sequoia 15.x: build the full Skywalk packet path (TX/RX pools + queues +
    // registerEthernetInterface) so IOSkywalkNetworkBSDClient can match and
    // create the BSD ifnet via the new logical-link route. The pools/queues
    // are stubbed (callbacks just return success) — actual datapath remains
    // the legacy IOEthernetInterface path; the Skywalk plumbing exists only
    // to satisfy the framework contract.
    //
    // Class sizes from research/sequoia-port/diff/15.7.5-class-sizes.txt:
    //   IOSkywalkEthernetInterface = 0x110
    //   IO80211SkywalkInterface    = 0x118
    // So mExpansionData2 sits at +0x108 (verified by static_assert in header).
    // initRegistrationInfo (called above) populates both expansion slots
    // through the framework — DO NOT manually IOMalloc them like Sonoma.
    {
        IOSkywalkPacketBufferPool::PoolOptions poolOpts = {};
        poolOpts.packetCount = 256;
        poolOpts.bufferCount = 256;
        poolOpts.bufferSize  = 2048;
        poolOpts.maxBuffersPerPacket = 1;

        fTxPool = IOSkywalkPacketBufferPool::withName("AirportItlwm-TX", fNetIf, 0, &poolOpts);
        fRxPool = IOSkywalkPacketBufferPool::withName("AirportItlwm-RX", fNetIf, 0, &poolOpts);
        if (!fTxPool || !fRxPool) {
            TRACE_STEP("FAIL_skywalkPool");
            XYLog("Skywalk pool create fail TX=%p RX=%p\n", fTxPool, fRxPool);
            super::stop(provider);
            releaseAll();
            return false;
        }
    }
    TRACE_STEP("18b_post_pools");

    // Sequoia 15.7.5: IOSkywalkTxSubmissionQueue::withPool /
    // IOSkywalkRxCompletionQueue::withPool are not in Apple's kxld export
    // table. We resolve them at runtime via AirportItlwmShim.kext (Lilu plugin)
    // and call through the function pointer instead of taking a static-link
    // reference. Wait for the shim's IOResources publication first, then
    // pull the function pointers.
    {
        OSDictionary *match = IOService::serviceMatching("IOResources");
        IOService *resSvc = IOService::waitForService(match, NULL);  // consumes match
        (void)resSvc;
        // 5 second cap: if the shim never showed up, prefer to fail explicitly
        // rather than NULL-deref the function pointer below.
        for (int i = 0; i < 50 && !IOService::getResourceService()->getProperty("AirportItlwm-Shim-Ready"); i++) {
            IOSleep(100);
        }
        if (!resolveSequoiaShimSymbols()) {
            TRACE_STEP("FAIL_shim_symbols_unresolved");
            XYLog("AirportItlwmShim did not publish required Sequoia symbols\n");
            super::stop(provider);
            releaseAll();
            return false;
        }
    }
    TRACE_STEP("18b1_post_resolveShim");

    fTxQueue = gShimTxWithPool(fTxPool, 256, 0, this,
                               skywalkTxAction, NULL, 0);
    fRxQueue = gShimRxWithPool(fRxPool, 256, 0, this,
                               skywalkRxAction, NULL, 0);
    if (!fTxQueue || !fRxQueue) {
        TRACE_STEP("FAIL_skywalkQueue");
        XYLog("Skywalk queue create fail TX=%p RX=%p\n", fTxQueue, fRxQueue);
        super::stop(provider);
        releaseAll();
        return false;
    }
    TRACE_STEP("18c_post_queues");

    {
        IOSkywalkPacketQueue *queues[] = {
            (IOSkywalkPacketQueue *)fTxQueue,
            (IOSkywalkPacketQueue *)fRxQueue,
        };
        // 15.7.5 ground truth: registerEthernetInterface returns IOReturn
        // (kIOReturnSuccess = 0). The header was previously bool, inverting
        // truthiness — fixed in commit 86e3adb.
        IOReturn regRet = fNetIf->registerEthernetInterface(
            (const IOSkywalkEthernetInterface::RegistrationInfo *)&registInfo,
            queues, 2, fTxPool, fRxPool, 0);
        if (regRet != kIOReturnSuccess) {
            TRACE_STEP("FAIL_registerEthernetInterface");
            XYLog("registerEthernetInterface fail ret=0x%x\n", regRet);
            super::stop(provider);
            releaseAll();
            return false;
        }
    }
    TRACE_STEP("18d_post_registerEthernetInterface");

    TRACE_STEP("19_pre_skywalkStart");
    fNetIf->start(this);
    TRACE_STEP("20_post_skywalkStart");

    // Trigger BSD ifnet publication via IOSkywalkNetworkBSDClient matching.
    // deferBSDAttach(false) clears the IODeferBSDAttach property and
    // re-registers the service so BSDClient can create the nexus channel
    // and BSD ifnet.
    fNetIf->deferBSDAttach(false);
    TRACE_STEP("20b_post_deferBSDAttach_false");
#endif

    setLinkStatus(kIONetworkLinkValid);
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess))
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    TRACE_STEP("21_pre_registerService");
    registerService();

#if __IO80211_TARGET >= __MAC_15_0
    // Path B Phase 1: 在所有其他 setup 完成后, attach 我们自己的 BSD wifi ifnet.
    // H4 跳过 fNetIf->attach 已经避免 IO80211Family panic, 但 airportd 看不到我们.
    // 这个 BSD KPI ifnet (type=IFT_IEEE80211, ioctl 返 IFM_IEEE80211) 让 airportd
    // 通过 getifaddrs+SIOCGIFMEDIA 主路径接受我们.
    {
        TRACE_STEP("22a_pre_createBsdWlanIfnet");
        u_int8_t mac[6];
        if (fHalService && fHalService->get80211Controller()) {
            memcpy(mac, fHalService->get80211Controller()->ic_myaddr, 6);
        } else {
            memset(mac, 0, 6);
        }
        if (createBsdWlanIfnet(this, mac)) {
            TRACE_STEP("22b_post_createBsdWlanIfnet_OK");
        } else {
            TRACE_STEP("22b_FAIL_createBsdWlanIfnet");
        }
    }
#endif

    TRACE_STEP("22_DONE");
    return true;
}

void AirportItlwm::stop(IOService *provider)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);XYLog("%s\n", __PRETTY_FUNCTION__);
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    super::stop(provider);
    disableAdapter(bsdInterface);
    setLinkStatus(kIONetworkLinkValid);
    fHalService->detach(pciNub);
    ether_ifdetach(ifp);
    detachInterface(fNetIf, true);
    OSSafeReleaseNULL(fNetIf);
    releaseAll();
}

void AirportItlwm::free()
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (fHalService != NULL) {
        fHalService->release();
        fHalService = NULL;
    }
    if (syncFrameTemplate != NULL && syncFrameTemplateLength > 0) {
        IOFree(syncFrameTemplate, syncFrameTemplateLength);
        syncFrameTemplateLength = 0;
        syncFrameTemplate = NULL;
    }
    if (roamProfile != NULL) {
        IOFree(roamProfile, sizeof(struct apple80211_roam_profile_band_data));
        roamProfile = NULL;
    }
    if (btcProfile != NULL) {
        IOFree(btcProfile, sizeof(struct apple80211_btc_profiles_data));
        btcProfile = NULL;
    }
    super::free();
}

#if __IO80211_TARGET >= __MAC_15_0
IO80211WorkQueue *AirportItlwm::createWorkQueue()
{
    if (_fWorkloop == nullptr) {
        _fWorkloop = IO80211WorkQueue::workQueue();
    }
    return _fWorkloop;
}
#else
bool AirportItlwm::createWorkQueue()
{
    XYLog("%s %d\n", __FUNCTION__, _fWorkloop != 0);
    return _fWorkloop != 0;
}
#endif

#if __IO80211_TARGET >= __MAC_15_0
// 15.7.5 ground truth: slot 398 = __ZNK17IO80211Controller12getWorkQueueEv (CONST).
IO80211WorkQueue *AirportItlwm::getWorkQueue() const
{
    return _fWorkloop;
}
#else
IO80211WorkQueue *AirportItlwm::getWorkQueue()
{
    return _fWorkloop;
}
#endif

#if __IO80211_TARGET < __MAC_15_0
void *AirportItlwm::getFaultReporterFromDriver()
{
    return driverFaultReporter;
}
#endif

#if __IO80211_TARGET >= __MAC_15_0
// Force our AirportItlwm vtable slot 268 (executeCommand) to be a real
// function pointer to OUR symbol. kxld is supposed to fill this slot from
// IONetworkController parent at load time, but Sequoia evidence (panic NX
// fault @ IO80211InfraProtocol::gMetaClass when configd's BSD ioctl chain
// dispatched ctrl->vtable[0x850]) shows the resolution went wrong for
// us — the slot ended up pointing at OSMetaClass data instead of code.
// By providing our own override, slot 268 is owned by us in the on-disk
// vtable and kxld doesn't need to fill it. Body just forwards to super.
// Documented in docs/static-analysis/decompile-keyfuncs.txt.
IOReturn AirportItlwm::executeCommand(OSObject *client,
                                      IONetworkController::Action action,
                                      void *target,
                                      void *param0, void *param1,
                                      void *param2, void *param3)
{
    return super::executeCommand(client, action, target,
                                 param0, param1, param2, param3);
}
#endif

#if __IO80211_TARGET < __MAC_15_0
IOReturn AirportItlwm::enable(IO80211SkywalkInterface *netif)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    super::enable(netif);
    _fCommandGate->enable();
    if (power_state)
        enableAdapter(bsdInterface);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::disable(IO80211SkywalkInterface *netif)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    super::disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}
#endif

bool AirportItlwm::configureInterface(IONetworkInterface *netif)
{
    IONetworkData *nd;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    
    if (super::configureInterface(netif) == false) {
        XYLog("super failed\n");
        return false;
    }
    
    nd = netif->getParameter(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        XYLog("network statistics buffer unavailable?\n");
        return false;
    }
    ifp->netStat = fpNetStats;
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    fpNetStats->collisions = 0;
#ifdef __PRIVATE_SPI__
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#endif
    
    return true;
}

IONetworkInterface *AirportItlwm::createInterface()
{
    // D3-light (af3967f) tried returning nullptr for Sequoia path —
    // Apple framework does NOT auto-wrap Skywalk to BSD. Result:
    // IONetworkController::start failed because createInterface gave
    // NULL → AirportItlwm IOService instance never started → driver
    // class loaded but no kext active behavior.
    //
    // Revert to creating AirportItlwmEthernetInterface for both paths.
    // Sequoia bypass of the broken executeCommand chain happens in
    // AirportItlwmEthernetInterface::performCommand which selectively
    // dispatches apple80211 ioctls (SIOCSA80211/SIOCGA80211) to
    // apple80211Request and returns Unsupported for everything else.
    AirportItlwmEthernetInterface *netif = new AirportItlwmEthernetInterface;
    if (!netif)
        return NULL;
    if (!netif->initWithSkywalkInterfaceAndProvider(this, fNetIf)) {
        netif->release();
        return NULL;
    }
    return netif;
}

bool AirportItlwm::createMediumTables(const IONetworkMedium **primary)
{
    IONetworkMedium    *medium;

    OSDictionary *mediumDict = OSDictionary::withCapacity(2);
    if (mediumDict == NULL) {
        XYLog("Cannot allocate OSDictionary\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(kIOMediumIEEE80211, 54000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    if (primary) {
        *primary = medium;
    }
    medium = IONetworkMedium::medium(kIOMediumIEEE80211None, 0);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    
    bool result = publishMediumDictionary(mediumDict);
    if (!result) {
        XYLog("Cannot publish medium dictionary!\n");
    }

    mediumDict->release();
    return result;
}

IOReturn AirportItlwm::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

bool AirportItlwm::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    struct _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    if (status == currentStatus) {
        return true;
    }
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    currentStatus = status;
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#ifdef __PRIVATE_SPI__
            bsdInterface->startOutputThread();
#endif
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkUp, (void *)0);
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#ifdef __PRIVATE_SPI__
            bsdInterface->stopOutputThread();
            bsdInterface->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkDown, (void *)fHalService->get80211Controller()->ic_deauth_reason);
        }
    }
    return ret;
}

IOReturn AirportItlwm::
setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    IOReturn ret = that->fNetIf->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1);
    that->fNetIf->setRunningState((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp);
    that->fNetIf->postMessage(APPLE80211_M_LINK_CHANGED, NULL, 0, false);
    that->fNetIf->postMessage(APPLE80211_M_BSSID_CHANGED, NULL, 0, false);
    that->fNetIf->postMessage(APPLE80211_M_SSID_CHANGED, NULL, 0, false);
    if ((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp) {
        that->fNetIf->reportLinkStatus(3, 0x80);
        that->updateLQMIfChanged();
    } else {
        that->fNetIf->reportLinkStatus(1, 0);
        that->fLastReportedLQM = 0;
    }
    that->bsdInterface->setLinkState((IO80211LinkState)(uint64_t)arg0);
    return ret;
}

#ifdef __PRIVATE_SPI__
IOReturn AirportItlwm::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    mbuf_t m = NULL;
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    while (kIOReturnSuccess == interface->dequeueOutputPackets(1, &m)) {
        if (outputPacket(m, NULL)!= kIOReturnOutputSuccess ||
            ifq_is_oactive(&ifp->if_snd))
            return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::networkInterfaceNotification(
                    IONetworkInterface * interface,
                    uint32_t              type,
                    void *                  argument )
{
    XYLog("%s\n", __FUNCTION__);
    return kIOReturnSuccess;
}
#endif

extern const char* hexdump(uint8_t *buf, size_t len);

UInt32 AirportItlwm::outputPacket(mbuf_t m, void *param)
{
//    XYLog("%s\n", __FUNCTION__);
    IOReturn ret = kIOReturnOutputSuccess;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    
    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (m == NULL) {
        XYLog("%s m==NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!(mbuf_flags(m) & MBUF_PKTHDR) ){
        XYLog("%s pkthdr is NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE) {
        XYLog("%s mbuf is FREE!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    size_t len = mbuf_len(m);
    ether_header_t *eh = (ether_header_t *)mbuf_data(m);
    if (len >= sizeof(ether_header_t) && eh->ether_type == htons(ETHERTYPE_PAE)) { // EAPOL packet
        const char* dump = hexdump((uint8_t*)mbuf_data(m), len);
        XYLog("output EAPOL packet, len: %zu, data: %s\n", len, dump ? dump : "Failed to allocate memory");
        if (dump)
            IOFree((void*)dump, 3 * len + 1);
    }
    if (!ifp->if_snd.queue->lockEnqueue(m)) {
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    (*ifp->if_start)(ifp);
    return ret;
}

const OSString * AirportItlwm::newVendorString() const
{
    return OSString::withCString("Apple");
}

const OSString * AirportItlwm::newModelString() const
{
    return OSString::withCString(fHalService->getDriverInfo()->getFirmwareName());
}

IOReturn AirportItlwm::getHardwareAddress(IOEthernetAddress *addrP)
{
    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr))
        return kIOReturnError;
    else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn AirportItlwm::setHardwareAddress(const void *addrP, UInt32 addrBytes)
{
    if (!fNetIf || !addrP)
        return kIOReturnError;
    if_setlladdr(&fHalService->get80211Controller()->ic_ac.ac_if, (const UInt8 *)addrP);
    if (fHalService->get80211Controller()->ic_state > IEEE80211_S_INIT) {
        fHalService->disable(bsdInterface);
        fHalService->enable(bsdInterface);
    }
    return kIOReturnSuccess;
}

UInt32 AirportItlwm::getFeatures() const
{
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn AirportItlwm::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastMode(IOEnetMulticastMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

IOReturn AirportItlwm::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn    rtn = kIOReturnSuccess;
    if (group == gIOEthernetWakeOnLANFilterGroup && magicPacketSupported)
        *filters = kIOEthernetWakeOnMagicPacket;
    else if (group == gIONetworkFilterGroup)
        *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
    else
        rtn = IOEthernetController::getPacketFilters(group, filters);
    return rtn;
}

SInt32 AirportItlwm::
enableFeature(IO80211FeatureCode code, void *data)
{
    if (code == kIO80211Feature80211n) {
        return 0;
    }
    return 102;
}

bool AirportItlwm::getLogPipes(CCPipe**logPipe, CCPipe**eventPipe, CCPipe**snapshotsPipe)
{
    bool ret = false;
    if (logPipe) {
        *logPipe = driverLogPipe;
        ret = true;
    }
    if (eventPipe) {
        *eventPipe = driverDataPathPipe;
        ret = true;
    }
    if (snapshotsPipe) {
        *snapshotsPipe = driverSnapshotsPipe;
        ret = true;
    }
    return ret;
}

#define APPLE80211_CAPA_AWDL_FEATURE_AUTO_UNLOCK    0x00000004
#define APPLE80211_CAPA_AWDL_FEATURE_WOW            0x00000080

IOReturn AirportItlwm::
getCARD_CAPABILITIES(OSObject *object,
                                     struct apple80211_capability_data *cd)
{
    uint32_t caps = fHalService->get80211Controller()->ic_caps;
    memset(cd, 0, sizeof(struct apple80211_capability_data));
    
    if (caps & IEEE80211_C_WEP)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_WEP;
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_TKIP | 1 << APPLE80211_CAP_AES_CCM;
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_PMGT)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_PMGT;
    // if (caps & IEEE80211_C_IBSS)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_IBSS;
    // if (caps & IEEE80211_C_HOSTAP)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_HOSTAP;
    // AES not enabled, like on Apple cards
    
    if (caps & IEEE80211_C_SHSLOT)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHSLOT - 8);
    if (caps & IEEE80211_C_SHPREAMBLE)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHPREAMBLE - 8);
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_WPA1 - 8) | 1 << (APPLE80211_CAP_WPA2 - 8) | 1 << (APPLE80211_CAP_TKIPMIC - 8);
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_TXPMGT)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_TXPMGT - 8);
    // if (caps & IEEE80211_C_MONITOR)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_MONITOR - 8);
    // WPA not enabled, like on Apple cards

    cd->version = APPLE80211_VERSION;
    cd->capabilities[2] = 0xFF; // BURST, WME, SHORT_GI_40MHZ, SHORT_GI_20MHZ, WOW, TSN, ?, ?
    cd->capabilities[3] = 0x2B;
    cd->capabilities[5] = 0x40;
    cd->capabilities[6] = (
//                           1 |    //MFP capable
                           0x8 |
                           0x4 |
                           0x80
                           );
    *(uint16_t *)&cd->capabilities[8] = 0x201;
//
//    cd->capabilities[2] |= 0x10;
//    cd->capabilities[5] |= 0x1;
//
//    cd->capabilities[2] |= 0x2;
//
//    cd->capabilities[3] |= 0x20;
//
//    cd->capabilities[0] |= 0x80;
//
//    cd->capabilities[3] |= 0x80;
//    cd->capabilities[4] |= 0x4;
//
//    cd->capabilities[4] |= 0x1;
//    cd->capabilities[3] |= 0x1;
//    cd->capabilities[6] |= 0x8;
//
//    cd->capabilities[3] |= 3;
//    cd->capabilities[4] |= 2;
//    cd->capabilities[6] |= 0x10;
//    cd->capabilities[5] |= 0x20;
//    cd->capabilities[5] |= 0x80;
//
//    if (cd->capabilities[6] & 0x20) {
//        cd->capabilities[2] |= 8;
//    }
//    cd->capabilities[5] |= 8;
//    cd->capabilities[8] |= 2;
//
//    cd->capabilities[11] |= (2 | 4 | 8 | 0x10 | 0x20 | 0x40 | 0x80);
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getDRIVER_VERSION(OSObject *object,
                                  struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    snprintf(hv->string, sizeof(hv->string), "itlwm: %s%s fw: %s", ITLWM_VERSION, GIT_COMMIT, fHalService->getDriverInfo()->getFirmwareVersion());
    hv->string_len = strlen(hv->string);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getHARDWARE_VERSION(OSObject *object,
                                    struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    strncpy(hv->string, fHalService->getDriverInfo()->getFirmwareVersion(), sizeof(hv->string));
    hv->string_len = strlen(fHalService->getDriverInfo()->getFirmwareVersion());
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getCOUNTRY_CODE(OSObject *object,
                                struct apple80211_country_code_data *cd)
{
    char user_override_cc[3];
    const char *cc_fw = fHalService->getDriverInfo()->getFirmwareCountryCode();
    
    if (!cd)
        return kIOReturnError;
    cd->version = APPLE80211_VERSION;
    memset(user_override_cc, 0, sizeof(user_override_cc));
    PE_parse_boot_argn("itlwm_cc", user_override_cc, 3);
    /* user_override_cc > firmware_cc > geo_location_cc */
    strncpy((char*)cd->cc, user_override_cc[0] ? user_override_cc : ((cc_fw[0] == 'Z' && cc_fw[1] == 'Z' && geo_location_cc[0]) ? geo_location_cc : cc_fw), sizeof(cd->cc));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCOUNTRY_CODE(OSObject *object, struct apple80211_country_code_data *data)
{
    XYLog("%s cc=%s\n", __FUNCTION__, data->cc);
    if (data && data->cc[0] != 120 && data->cc[0] != 88) {
        memcpy(geo_location_cc, data->cc, sizeof(geo_location_cc));
        fNetIf->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, 0);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    if (!pd)
        return kIOReturnError;
    pd->version = APPLE80211_VERSION;
    pd->num_radios = 4;
    pd->power_state[0] = power_state;
    pd->power_state[1] = power_state;
    pd->power_state[2] = power_state;
    pd->power_state[3] = power_state;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    if (!pd)
        return kIOReturnError;
    IOLog("itlwm: setPOWER: num_radios[%d]  power_state(0:%u  1:%u  2:%u  3:%u)\n", pd->num_radios, pd->power_state[0], pd->power_state[1], pd->power_state[2], pd->power_state[3]);
    if (pd->num_radios > 0) {
        bool isRunning = (fHalService->get80211Controller()->ic_ac.ac_if.if_flags & (IFF_UP | IFF_RUNNING)) != 0;
        if (pd->power_state[0] == 0) {
            changePowerStateToPriv(1);
            if (isRunning) {
                net80211_ifstats(fHalService->get80211Controller());
                disableAdapter(bsdInterface);
            }
        } else {
            changePowerStateToPriv(2);
            if (!isRunning)
                enableAdapter(bsdInterface);
        }
        power_state = (pd->power_state[0]);
    }
    
    return kIOReturnSuccess;
}

#if __IO80211_TARGET < __MAC_15_0
SInt32 AirportItlwm::apple80211_ioctl(IO80211SkywalkInterface *interface,unsigned long cmd,void *data, bool b1, bool b2)
{
    if (!ml_at_interrupt_context())
        XYLog("%s cmd: %s b1: %d b2: %d\n", __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), b1, b2);
    return super::apple80211_ioctl(interface, cmd, data, b1, b2);
}

SInt32 AirportItlwm::apple80211SkywalkRequest(UInt request,int cmd,IO80211SkywalkInterface *interface,void *data)
{
    if (!ml_at_interrupt_context())
        XYLog("%s 1 cmd: %s request: %d\n", __FUNCTION__, convertApple80211IOCTLToString(cmd), request);
    return kIOReturnUnsupported;
}

SInt32 AirportItlwm::apple80211SkywalkRequest(UInt request,int cmd,IO80211SkywalkInterface *interface,void *data,void *)
{
    if (!ml_at_interrupt_context())
        XYLog("%s 2 cmd: %s request: %d\n", __FUNCTION__, convertApple80211IOCTLToString(cmd), request);
    return kIOReturnUnsupported;
}
#endif

IOReturn AirportItlwm::enableAdapter(IONetworkInterface *netif)
{
    fHalService->enable(netif);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
    watchdogTimer->enable();
    return kIOReturnSuccess;
}

void AirportItlwm::disableAdapter(IONetworkInterface *netif)
{
    watchdogTimer->cancelTimeout();
    watchdogTimer->disable();
    fHalService->disable(netif);
}

IOReturn AirportItlwm::
tsleepHandler(OSObject* owner, void* arg0, void* arg1, void* arg2, void* arg3)
{
    AirportItlwm* dev = OSDynamicCast(AirportItlwm, owner);
    if (dev == 0)
        return kIOReturnError;
    
    if (arg1 == 0) {
        if (_fCommandGate->commandSleep(arg0, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    } else {
        AbsoluteTime deadline;
        clock_interval_to_deadline((*(int*)arg1), kNanosecondScale, reinterpret_cast<uint64_t*> (&deadline));
        if (_fCommandGate->commandSleep(arg0, deadline, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    }
}

bool AirportItlwm::initPCIPowerManagment(IOPCIDevice *provider)
{
    UInt16 reg16;

    reg16 = provider->configRead16(kIOPCIConfigCommand);

    reg16 |= ( kIOPCICommandBusMaster       |
               kIOPCICommandMemorySpace     |
               kIOPCICommandMemWrInvalidate );

    reg16 &= ~kIOPCICommandIOSpace;  // disable I/O space

    provider->configWrite16( kIOPCIConfigCommand, reg16 );
    provider->findPCICapability(kIOPCIPowerManagementCapability,
                                &pmPCICapPtr);
    if (pmPCICapPtr) {
        UInt16 pciPMCReg = provider->configRead32( pmPCICapPtr ) >> 16;
        if (pciPMCReg & kPCIPMCPMESupportFromD3Cold)
            magicPacketSupported = true;
        provider->configWrite16((pmPCICapPtr + 4), 0x8000 );
        IOSleep(10);
    }
    return true;
}

static IOPMPowerState powerStateArray[kPowerStateCount] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

void AirportItlwm::unregistPM()
{
    if (powerOffThreadCall) {
        thread_call_free(powerOffThreadCall);
        powerOffThreadCall = NULL;
    }
    if (powerOnThreadCall) {
        thread_call_free(powerOnThreadCall);
        powerOnThreadCall = NULL;
    }
}

IOReturn AirportItlwm::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
    IOReturn result = IOPMAckImplied;
    
    if (pmPowerState == powerStateOrdinal)
        return result;
    switch (powerStateOrdinal) {
        case kPowerStateOff:
            if (powerOffThreadCall) {
                retain();
                if (thread_call_enter(powerOffThreadCall))
                    release();
                result = 5000000;
            }
            break;
        case kPowerStateOn:
            if (powerOnThreadCall) {
                retain();
                if (thread_call_enter(powerOnThreadCall))
                    release();
                result = 5000000;
            }
            break;
            
        default:
            break;
    }
    return result;
}

IOReturn AirportItlwm::setWakeOnMagicPacket(bool active)
{
    magicPacketEnabled = active;
    return kIOReturnSuccess;
}

static void handleSetPowerStateOff(thread_call_param_t param0,
                             thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *)param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOff,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOff();
        self->release();
    }
}

static void handleSetPowerStateOn(thread_call_param_t param0,
                            thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *) param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOn,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOn();
        self->release();
    }
}

IOReturn AirportItlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;
    
    pmPowerState = kPowerStateOn;
    pmPolicyMaker = policyMaker;
    
    powerOffThreadCall = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOff,
                                            (thread_call_param_t)this);
    powerOnThreadCall  = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOn,
                                              (thread_call_param_t)this);
    ret = pmPolicyMaker->registerPowerDriver(this,
                                             powerStateArray,
                                             kPowerStateCount);
    return ret;
}

void AirportItlwm::setPowerStateOff()
{
    XYLog("%s\n", __FUNCTION__);
    pmPowerState = kPowerStateOff;
    disableAdapter(bsdInterface);
    pmPolicyMaker->acknowledgeSetPowerState();
}

void AirportItlwm::setPowerStateOn()
{
    XYLog("%s\n", __FUNCTION__);
    pmPowerState = kPowerStateOn;
    pmPolicyMaker->acknowledgeSetPowerState();
}
