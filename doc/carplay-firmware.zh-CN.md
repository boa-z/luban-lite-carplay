# CarPlay 固件

[English](carplay-firmware.md)

[SDK 集成与依赖审计](carplay-sdk-integration.zh-CN.md)

| 硬件目标 | 无线路径 | 固件功能 |
| --- | --- | --- |
| `d213-demo128-nand-carplay-rtl8733bs` | Realtek SDIO，H:5 BR/EDR | 有线和无线 CarPlay、以太网共享、A/B OTA |
| `d213-demo128-nand-carplay-aic8800d40` | AIC8800 D80 系列 SDIO 固件，H:4 BR/EDR | 有线和无线 CarPlay、A/B OTA |

用户确认 `c59d3cc3` 生成的 D40 镜像通过有线和无线 CarPlay，IMG SHA‑256 为 `098367850f4a45b821813bc9266b0f0b66b4b09fb65e76fe8342d411b0ee5dec`。后续镜像需要各自的上板记录。D40L 保留独立 WLAN 选项，无法选择 D40 的 Classic 主机。D80 是已上板 D40 配置使用的 SDK SDIO 产品和固件系列标识。

## 构建与新增目标

在 Linux 或 WSL 安装 Firmware build 工作流列出的工具和固定 Python 依赖，运行 `git lfs pull` 和 `git submodule update --init --recursive`。

```sh
python3 .ci/hardware_targets.py matrix
bash .ci/build-firmware.sh d213-demo128-nand-carplay-aic8800d40
```

脚本支持单独运行 `build` 和 `verify`。产物在 `output/ci/<target>/`，日志在 `output/ci-logs/<target>/`。本地锁串行化共享 `.config` 和打包目录的构建。需要并行时使用不同检出目录。GitHub Actions 每个目标使用独立任务，输入可选目标 ID 或 `all`。

新增目标时提供 BSP 和 Bootloader、应用 defconfig，再向 `.ci/hardware-targets.json` 增加目标描述、无线驱动和能力、固件功能、分区布局、工具链前缀与配置断言。互不兼容的升级域使用不同的 OTA 兼容 ID，并与 `RTCARPLAY_OTA_COMPAT` 一致。现有 CPIO 实现覆盖 D213 的 OS/Rodata 布局。新布局或镜像格式需要对应实现和验证器。

## 发布与验收

- **Development** 保留已校验 Artifact。`[ultra-dev]` 不会自动发布。
- **Nightly** 使用滚动 `nightly` Release，提供两种目标的 ZIP 和带目标 ID 的 CPIO，保留 RTL8733BS 的 `ota.cpio` 下载入口。
- **Stable** 使用指向已验证提交的轻量标签 `firmware-v<version>`。固件版本取自 BSP 镜像配置，Manifest 单独记录 Luban-Lite、rt-carplay 和 Demo 的版本或提交。

运行 **Promote verified firmware** 时提供全目标构建的运行 ID，以及按目标 ID 索引的实机证据 JSON。每项含 `status: PASS`、`img_sha256` 和指向报告的 HTTPS `url`。现阶段由测试人员提交，后续 `artinchip-flash` HIL 可采用同一格式。发布校验哈希和目标完整性，直接使用原 Artifact。每种 ZIP 包含对应 IMG 和 `bootcfg.txt`。

升级器在写入前检查 CPIO 兼容 ID。RTL 保持旧包标识 `demo128-nand-carplay`，D40 使用 `d213-demo128-nand-carplay-aic8800d40`。现有 A/B 升级器复验暂存包，写入并回读非活动 OS、Rodata 槽位后才请求切换。首个支持 OTA 的 D40 镜像需用完整 IMG 安装。实机验收包括首次配对、有线和无线会话、重连、AP 重启、双向拒绝跨型号 OTA、A→B 与 B→A 更新、写入中断恢复、启动未确认回滚和 `/data` 保留。CI 对新镜像的硬件状态标记为 `NOT_RUN`。
