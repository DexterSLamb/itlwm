# itlwm — DexterSLamb fork

> **Fork-specific change**: this fork patches `AirportItlwm` to fix the
> iServices breakage (iMessage / FaceTime / AirDrop / Continuity refusing
> to use Wi-Fi) that affects upstream V2 builds on **macOS Sonoma 14.4+**.
>
> **Root cause**: the upstream V2 (Skywalk) path commented out the
> `setLinkQualityMetric(100)` call from V1 because the API was renamed in
> the new `IO80211SkywalkInterface` schema, but never ported to the
> replacement `setLQM(uint64)`. As a result, the SCDynamicStore key
> `State:/Network/Interface/<if>/LinkQuality` stays at the default
> "no signal" value, `+[PCInterfaceUsabilityMonitor isBadLinkQuality:]`
> (literal threshold `lq < 11`) marks the interface unusable, and
> `apsd._connectStreamWithInterfacePreference:` rejects every interface
> in the iServices APNS path.
>
> **Fix**: call `IO80211InfraInterface::setLQM(uint64)` (resolved via
> virtual dispatch through `IO80211SkywalkInterface*`) on link-up and on
> every 1 Hz watchdog tick, with the value mapped from current
> `ni_rssi` to {100, 50, 25} so that signal degradation is reflected
> while still keeping LQ above the iServices threshold of 11.
>
> Builds at https://github.com/DexterSLamb/itlwm/releases for every
> macOS target the upstream project supports. The fork keeps
> `MODULE_VERSION` at `2.3.0` so the binary drops in without OpenCore
> config changes.
>
> ---
>
> **本 fork 的改动**: 针对 macOS Sonoma 14.4+ 上 AirportItlwm V2 (Skywalk)
> 路径下 iServices (iMessage / FaceTime / AirDrop / Continuity) 拒绝使用
> Wi-Fi 接口的故障的修复。
>
> **根因**: 上游 V2 路径在 Skywalk 接口改名之后注释掉了 V1 里的
> `setLinkQualityMetric(100)` 调用,但没有移植到替代 API
> `setLQM(uint64)`。导致 SCDynamicStore 的
> `State:/Network/Interface/<if>/LinkQuality` 一直是默认 "无信号" 值,
> `+[PCInterfaceUsabilityMonitor isBadLinkQuality:]` (字面阈值 `lq < 11`)
> 把接口判为不可用, `apsd._connectStreamWithInterfacePreference:` 拒绝
> 所有接口, 整条 iServices APNS 链路失败。
>
> **修复**: 在 link-up 和每 1 Hz 看门狗 tick 调用
> `IO80211InfraInterface::setLQM(uint64)` (通过 `IO80211SkywalkInterface*`
> 的 virtual dispatch 解析), 根据当前 `ni_rssi` 映射到 {100, 50, 25} 三档,
> 既反映信号变化, 又保持 LQ 高于 iServices 阈值 11。
>
> 构建产物在 https://github.com/DexterSLamb/itlwm/releases, 覆盖上游支持的
> 全部 macOS 变体。fork 保留 `MODULE_VERSION = 2.3.0`, 二进制可直接替换
> 不用改 OpenCore config。
>
> ---
>
> ### Symptoms / 症状关键字
>
> If you're searching for any of these, you're in the right place. /
> 如果你搜索下面任何一个关键字, 你来对地方了。
>
> - iMessage / iMessage waiting for activation / iMessage activation failed
> - FaceTime not working / FaceTime activation could not complete
> - AirDrop not finding devices on Hackintosh
> - Continuity / Handoff not working between Mac and iPhone
> - APNS push not working / push notifications broken
> - `apsd` log: `no closed interfaces are usable`
> - `apsd` log: `Push is connected? NO ... isBadLQ? YES`
> - `apsd._connectStreamWithInterfacePreference:` rejecting interfaces
> - `+[PCInterfaceUsabilityMonitor isBadLinkQuality:]`
> - `PCNonCellularUsabilityMonitor` / `PCInterfaceMonitor`
> - `scutil`: `State:/Network/Interface/en0/LinkQuality` returns `-2` or
>   doesn't exist
> - macOS Sonoma 14.4 / 14.5 / 14.6 / 14.7 / 14.8 / Sequoia 15.x +
>   AirportItlwm v2.3.0 / v2.4.0 + Intel AX201 / AX210 / AX211 / AC9560 /
>   Wi-Fi 6 / Wi-Fi 6E / Hackintosh / OpenCore: iServices fully broken
> - 黑苹果 macOS 14.4+ 升级以后 iMessage / 短信中继 / 接力 / FaceTime /
>   AirDrop / iCloud 推送 全部失效
>
> Upstream PR: https://github.com/OpenIntelWireless/itlwm/pull/1056

# itlwm

**An Intel Wi-Fi Adapter Kernel Extension for macOS, based on the OpenBSD Project.**

## Documentation

We highly recommend exploring our documentation before using this Kernel Extension:

- [Intro](https://OpenIntelWireless.github.io/itlwm)
- [Compatibility](https://openintelwireless.github.io/itlwm/Compat)
- [FAQ](https://openintelwireless.github.io/itlwm/FAQ)

## Download

[![Download from https://github.com/OpenIntelWireless/itlwm/releases](https://img.shields.io/github/v/release/OpenIntelWireless/itlwm?label=Download)](https://github.com/OpenIntelWireless/itlwm/releases)

## Questions and Issues

Check out our [FAQ Page](https://openintelwireless.github.io/itlwm/FAQ) for more info.

If you have other questions or feedback, feel free to [![Join the chat at https://gitter.im/OpenIntelWireless/itlwm](https://badges.gitter.im/OpenIntelWireless/itlwm.svg)](https://gitter.im/OpenIntelWireless/itlwm?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge).

We only accept bug reports in GitHub Issues, before opening an issue, you're recommended to reconfirm it with us on [Gitter](https://gitter.im/OpenIntelWireless/itlwm); once it's confirmed, please use the provided issue template.

## Credits

- [Acidanthera](https://github.com/acidanthera) for [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)
- [Apple](https://www.apple.com) for [macOS](https://www.apple.com/macos)
- [AppleIntelWiFi](https://github.com/AppleIntelWiFi) for [Black80211-Catalina](https://github.com/AppleIntelWiFi/Black80211-Catalina)
- [ErrorErrorError](https://github.com/ErrorErrorError) for UserClient bug fixes
- [Intel](https://www.intel.com) for [Wireless Adapter Firmwares](https://www.intel.com/content/www/us/en/support/articles/000005511/network-and-io/wireless.html) and [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [Linux](https://www.kernel.org) for [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [mercurysquad](https://github.com/mercurysquad) for [Voodoo80211](https://github.com/mercurysquad/Voodoo80211)
- [OpenBSD](https://openbsd.org) for [net80211, iwn, iwm, and iwx](https://github.com/openbsd/src)
- [pigworlds](https://github.com/OpenIntelWireless/itlwm/commits?author=pigworlds) for DVM devices support, MIRA bug fixes, and Tx aggregation for MVM Gen 1 devices
- [rpeshkov](https://github.com/rpeshkov) for [black80211](https://github.com/rpeshkov/black80211)
- [usr-sse2](https://github.com/usr-sse2) for implementing the usage of Apple RSN Supplicant and bug fixes
- [zxystd](https://github.com/zxystd) for developing [itlwm](https://github.com/OpenIntelWireless/itlwm)

## Acknowledgements

- [@penghubingzhou](https://github.com/startpenghubingzhou)
- [@Bat.bat](https://github.com/williambj1)
- [@iStarForever](https://github.com/XStar-Dev)
- [@stevezhengshiqi](https://github.com/stevezhengshiqi)
- [@DogAndPot](https://github.com/DogAndPot) for providing resources and help for system configuration
- [@Daliansky](https://github.com/Daliansky) for providing Wi-Fi cards
