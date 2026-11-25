# CarPlay SDK 集成与依赖

[English](carplay-sdk-integration.md)

原厂 Luban-Lite v1.3.3 基线为 `90a740fe9c1b663ea52ec3f7e1af35acde200aea`。
主仓维护 ArtInChip 平台适配、配置、Bootloader 与固件交付。
`rt-carplay` 维护 Receiver 协议，`rt-carplay-demo` 维护应用和 HMI，
`cherryusb-ipheth` 维护 USB Ethernet。三个 Gitlink 固定版本。
`application/rt-thread/rt-carplay-demo` 从 `packages/third-party/rt-carplay`
构建一份 Receiver。正式 D213 目标使用 Demo 原生 NCM，ipheth 有独立验证配置。

## 版本与维护策略

| 组件 | 固定来源与许可证 | 决策 |
| --- | --- | --- |
| CherryUSB | [boa-z/CherryUSB](https://github.com/boa-z/CherryUSB) 子模块，Apache-2.0。首次锁定 `a0054795c44037ace8a2b77b6e610adbff2257ea`，与迁移前的上游源码树 `f36dac4d25d67f9553cdd68c27a74be70f33a9d7` 一致。 | 由 Gitlink 固定源码版本。已对照较新的上游提交 `fd76ba101d829fff80272a2038c5caa89de9b42d` 审查下表的补丁行为。 |
| CherryUSB SDK 适配与旧栈 | `cherryusb-luban` 保存构建、Kconfig、IRQ/Cache、Baremetal OSAL、SDK USB 辅助代码和显示资产。`cherryusb-1.0.0` 来自原厂 SDK。 | 默认构建启用现行适配层，旧版 Kconfig 入口保持禁用。保持对原厂遗留源码的访问。 |
| RT-Thread / lwIP | SDK 中的 [RT-Thread](https://github.com/RT-Thread/rt-thread) 4.1.1（Apache-2.0）、[lwIP](https://github.com/lwip-tcpip/lwip) 2.1.2（BSD-3-Clause）。NAPT 转换器来自 [Espressif lwIP](https://github.com/espressif/esp-lwip/tree/f5af4d8b1b4cd4585322f43833beff52b4a189ea) 固定提交。 | 保留 SDK ABI 与必要移植。RT-Thread `v5.3.0`、lwIP `STABLE-2_2_1_RELEASE` 需要单独移植及驱动回归。 |
| Mbed TLS / WebClient | [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 2.28.1 与 [WebClient](https://github.com/RT-Thread-packages/webclient)，Apache-2.0。SDK 基线固定二者源码，WebClient 原始导入没有精确上游 SHA。 | 保留 Receiver 的 2.x ABI、厂商加速、OTA TLS 校验和接口绑定。已核对 WebClient 最新标签 `2.2.0`，升级需覆盖这些契约。 |
| Zephyr Bluetooth | [Zephyr](https://github.com/zephyrproject-rtos/zephyr) SDK 快照标记 4.4.0，保留 Apache-2.0 和文件级许可证声明。 | 保留 RT-Thread 适配、RTL8733BS H:5、D40 H:4 和各自板级固件。`v4.5.0` 作为独立移植候选。 |
| LVGL / MPP / BSP | SDK 的 [LVGL](https://github.com/lvgl/lvgl) 8.3.1 与 9.1.0（MIT）；ArtInChip MPP 和 BSP 依据原厂文件声明。 | 保留显示、触摸、GE、Cache/DMA 和解码路径。`v9.6.0` 升级需单独验证 ABI、渲染和性能。 |
| 独立依赖 | Receiver 与 Demo（GPL-3.0 系列，详见各仓许可证），ipheth（Apache-2.0 及 BSD 派生部分）。 | `git submodule status` 列出固定提交。集成 CI 使用 clang-format 23.1.1 检查实际集成的 Receiver 和 Demo。 |

厂商未记录精确的原始上游 SHA 时，以 SDK 基线固定源码版本。

## CherryUSB 本地补丁

生成编译源时从官方树精确应用补丁，源树可以直接核对。

| 补丁 | 用途与核对结果 |
| --- | --- |
| `0001` | EHCI 提前结束的短 Bulk IN、错误回收及 DMA 同步。更新的上游仍只在尾 qTD 设置 Bulk IOC，扫描遇到 Active 就返回。 |
| `0002` | 传播 Hub 启动失败并移除失败的总线节点。上游仍忽略返回值。EP0 锁计时独立受 `LPKG_CHERRYUSB_HOST_CONTROL_PROBE` 控制，固件 CI 检查关闭。 |
| `0003` | Baremetal 前台按批量处理 Hub 事件和定时器。上游仍使用线程。 |
| `0004` | 将 AIC USB Device ISR 适配到正确的 SDK IRQ 签名。上游低层仍直接注册旧声明。 |
| `0005` | 保留 RT-Thread OSAL 对空互斥句柄的处理。上游仍直接调用内核，原版本地改动已从官方源树迁出。 |

## SDK 保留项与回归

- WLAN 保留同步取消、悬挂定时器预防和工作对象生命周期修复。移除 RT-Thread 通用对象追踪、清空对象表的恢复逻辑、失效指针定时器诊断。
- lwIP 保留可选 NAPT 的输入、转发及接口销毁钩子、DHCP 选项、SNTP 回包检查、队列和套接字配置。转换器及转发策略由 `port/napt/` 单独编译，Demo 测试兼容新旧路径。独立的 `RT_LWIP_MDNS` 控制 mDNS，并限制额外定时器在相关功能启用时分配。
- 无线与蓝牙保留 D40/D40L 选型、AP/STA 和 Vendor IE 适配、Realtek 接收缓冲区所有权、BR/EDR 和设置表。D40L 配置独立验证 WLAN，D40 Classic 仅用于 D40。
- Mbed TLS 和 WebClient 保留 RV64 MPI、AES-CTR 后端、出口网卡绑定和 TLS 校验。板级 OTA 仍需实测时间、证书及更新服务器。
- BSP/MPP/LVGL 保留 VSync 计数、H.264 缺失 SPS 处理、GUI 启动和触摸唯一所有者。需要实测 Fusion、音视频和触摸的性能与行为。
- Bootloader 和构建仍负责 USB 升级、IMG 与 `bootcfg.txt` 对应、A/B 槽位恢复、兼容 ID、CPIO 校验。断电回滚、数据保留和实机烧录需使用同一 SHA256 的实际产物验证。

递归初始化依赖后运行 `sh packages/third-party/rt-carplay/scripts/fetch-mbedtls.sh`
和 `bash .ci/test-sdk.sh`。双目标构建、增加硬件目标及上板清单参见
[固件指南](carplay-firmware.zh-CN.md)。可选旧版 USB Display 与 UVC
需要单独验证官方 CherryUSB 版本兼容性。
