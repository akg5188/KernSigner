# PN5180 NFC 接线和使用说明

日期：2026-06-08

这份文档写当前 KernSigner 真机已经跑通的 PN5180 NFC 接法。现在主固件所有智能卡流程都先使用 PN5180 读 Satochip / SeedKeeper 这类 ISO14443A CPU 智能卡，USB CCID 只在 PN5180 硬件不可用时备用，PN532 不再参与运行路径。

当前实测结论：

- PN5180 可以跑通智能卡状态读取、APDU、Satochip / SeedKeeper 相关流程。
- 手机贴到 PN5180 天线区域会有 NFC 感应，说明射频场已经起来。
- 这块 PN5180 模块实测要接开发板 `5V` 才稳定，接 `3V3` 时射频场不可靠。
- PN5180 的逻辑信号线仍然只接 GPIO 信号脚，绝对不要把 `5V` 接到任何 GPIO。

## 最终实测接线

断电后接线，接完先检查一遍再上电。

| PN5180 模块 | wave_43 开发板 | 说明 |
| --- | --- | --- |
| `5V` / `VCC` | `5V` | 本次实测模块用 5V 供电才稳定 |
| `GND` | `GND` | 必须共地，可以换到方便插的 GND |
| `SCK` | `GPIO52` | SPI 时钟 |
| `MOSI` | `GPIO51` | SPI 主机发送 |
| `MISO` | `GPIO50` | SPI 主机接收 |
| `NSS` / `SS` / `CS` | `GPIO49` | SPI 片选 |
| `BUSY` | `GPIO31` | PN5180 忙信号 |
| `RST` / `NRESET` | `GPIO30` | PN5180 复位 |
| `IRQ` | 不接 | 当前固件不用 IRQ，配置为 `-1` |

简单照抄版：

```text
PN5180 5V     -> 开发板 5V
PN5180 GND    -> 开发板 GND
PN5180 SCK    -> GPIO52
PN5180 MOSI   -> GPIO51
PN5180 MISO   -> GPIO50
PN5180 NSS/CS -> GPIO49
PN5180 BUSY   -> GPIO31
PN5180 RST    -> GPIO30
PN5180 IRQ    -> 不接
```

## 供电红线

`5V` 只接 PN5180 模块的电源输入脚，不接 GPIO。

不要这样接：

- 不要把 `5V` 接到 `SCK/MOSI/MISO/NSS/BUSY/RST/IRQ`。
- 不要把 `GND` 插到旁边的信号脚。
- 不要带电插拔杜邦线。
- 不要让杜邦线金属头歪到相邻针脚。
- 不要把 PN5180 当 I2C 模块接 `SDA/SCL`。

如果上电后模块异常发热、冒烟、有异味、手机完全没有 NFC 感应，马上断电。先查 `5V/GND`，再查 `SCK/MOSI/MISO/NSS/BUSY/RST`。

## PN5180 和 PN532 的区别

| 项目 | PN5180 | PN532 |
| --- | --- | --- |
| 当前固件定位 | 当前主 NFC 方案 | 旧方案，不参与当前运行路径 |
| 总线 | SPI | 常见红板多用 I2C |
| 当前实测供电 | 本模块用 5V 稳定 | 旧 PN532 红板通常接 3V3 |
| 当前接线 | `52/51/50/49/31/30` | 旧文档曾用 `28/31` I2C |
| 当前建议 | 使用 PN5180 | 不再使用 |

PN5180 是 NFC 前端，不是 Wi-Fi、蓝牙，也不是网络设备。它只在很近距离产生 NFC 射频场读卡，和无线联网不是一回事。

## 上电前检查

1. 断开 USB 电源。
2. 看模块丝印，不按线的颜色猜。
3. 确认 `5V` 只接 PN5180 的 `5V/VCC`。
4. 确认 `GND` 只接 PN5180 的 `GND`。
5. 用万用表蜂鸣档检查 `5V` 和 `GND` 不短路。
6. 确认 SPI 六根线没有错排、错列、歪插。
7. 先桌面裸测，不要马上塞进外壳。

## 第一次测试顺序

1. 先不贴卡上电，看 PN5180 是否异常发热。
2. 打开串口日志，看是否出现 PN5180 初始化日志。
3. 把手机贴到 PN5180 天线区域，手机应能感应到 NFC 场。
4. 把 Satochip / SeedKeeper 智能卡贴到天线面。
5. 进入 `设备检查 -> 智能卡检测` 或智能卡状态页面。
6. 看页面是否显示 `Active: NFC PN5180` 和卡片状态。
7. 再测试 Satochip 状态、公钥、地址、签名或 SeedKeeper 列表。

正常启动日志类似：

```text
ESP32-C6 wireless companion held disabled on GPIO54
PN5180 ready on SPI1 SCK=52 MOSI=51 MISO=50 NSS=49 BUSY=31 RST=30 Hz=1000000
```

读到 CPU 智能卡后，日志里会出现类似：

```text
PN5180 ISO14443A CPU card ready
ISO-DEP APDU OK
SW=9000
```

## 卡片摆放

- 卡片贴在 PN5180 天线正面，位置要覆盖天线中心。
- 签名、读取状态、SeedKeeper 列表时不要移动卡。
- 先裸板测试，稳定后再固定进外壳。
- PN5180 背面和开发板之间建议加铁氧体片或磁屏蔽贴。
- 避开金属螺丝、屏蔽罩、电池、大面积铜皮和粗电源线。
- 杜邦线尽量短，优先保证 `GND` 和 SPI 线接触牢靠。

如果手机一贴就有感应，但智能卡读不到，优先调卡片位置和天线距离。如果手机也完全没反应，优先查 PN5180 供电和模块焊接。

## 主固件状态

当前 PN5180 主固件已经做了这些适配：

- 所有智能卡流程默认优先选择 `NFC PN5180` 智能卡通道。
- USB CCID 只在 PN5180 硬件不可用时作为 fallback。
- 先贴卡再进页面也可以，不需要每次后贴卡。
- APDU 支持 WTX 等待，卡片处理慢时不会立刻误判失败。
- 支持 ISO-DEP 链式帧，长响应不会因为分片直接失败。
- 每次探测前会重新整理 RF 场，减少卡已经在上面但状态不刷新的问题。
- 当前 PN532 不参与 probe、APDU、fallback 或状态报告。

## 常见问题

| 现象 | 优先判断 | 处理 |
| --- | --- | --- |
| 手机贴上完全没反应 | PN5180 射频场没起来 | 查 `5V/GND`，确认本模块用 5V，查模块焊接 |
| 手机有反应，KernSigner 读不到卡 | 卡片位置或 APDU 链路问题 | 卡贴稳，进智能卡检测，看 `Active: NFC PN5180` |
| `PN5180 reset/BUSY wait failed` | `BUSY/RST/GND/供电` 问题 | 查 GPIO31、GPIO30、GND、5V |
| `PN5180 EEPROM read failed` | SPI 通信失败 | 查 `SCK/MOSI/MISO/NSS`，线缩短 |
| `EEPROM read was blank` | MISO/NSS/供电不稳 | 重点查 MISO、CS 和模块供电 |
| `ready, but no ISO14443-4 NFC smartcard found` | 没检测到 CPU 智能卡 | 换卡片角度，确认卡是 contactless CPU 智能卡 |
| 状态页以前要等很久 | 旧探测等待或卡没被立即识别 | 当前固件已优化，仍慢就固定卡片位置再进页面 |
| 签名中途失败 | 卡移动或射频不稳 | 卡贴住不要动，线缩短，加铁氧体片 |

## 相关文档

- NFC 智能卡操作和排错：[NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md](NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md)
- Satochip / SeedKeeper 操作手册：[SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md)
- PN5180 独立小固件：[../pn5180_bringup/README.zh-CN.md](../pn5180_bringup/README.zh-CN.md)
- 无线关闭说明：[WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md](WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md)
