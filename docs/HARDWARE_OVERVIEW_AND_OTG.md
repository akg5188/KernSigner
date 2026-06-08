# Waveshare ESP32-P4 4.3 Hardware Overview

日期：2026-06-08

这页只写和 KernSigner 当前真机交付直接相关的硬件信息，重点是 USB 口、供电方式、PN5180 NFC、无线关闭和智能卡接法。它不是完整 datasheet，但足够让 GitHub 访客按图接线、按口刷机，不把 OTG 误当下载口。

这个项目主要由 AI 辅助实现，目前功能还没有完善，只适合学习交流和测试，请不要放钱进去。

## 开发板型号

- 板型：Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3
- 内部分支名：`wave_43`
- 屏幕：480x800 MIPI DSI
- 触摸：GT911
- 摄像头：OV5647
- 当前 BSP 代码：`components/wave_43/`

## 板级能力

- 显示和触摸已接入。
- 背光亮度可调。
- 摄像头预览链路已接入。
- USB OTG 可作为 Host 接外设。
- USB-to-UART / 下载口用于刷机和串口日志。
- PN5180 NFC 已作为当前主智能卡贴卡路线跑通。
- 板载 ESP32-C6 无线伴随芯片由 GPIO54 拉低保持禁用。

## 摄像头首次对焦

Waveshare ESP32-P4 4.3 常见 OV5647 摄像头不是手机那种自动对焦。第一次使用、换排线、拆装摄像头或装外壳后，如果直接扫 OKX、TokenPocket 这类高密度动态二维码，可能会表现成“完全没有反应”。

首检步骤：

1. 进入 `自检 -> 外设 -> 相机` 或扫码页。
2. 手机或电脑显示普通黑白二维码。
3. 保持 10-20 cm 距离，调高屏幕亮度，避开反光。
4. 捏住镜头外圈小幅旋转，让二维码边缘和小格子最清楚。
5. 不要拧 FFC 排线，不要拉摄像头主板，不要让金手指松动。
6. 普通二维码稳定识别后，再测试 Web3 高密度动态码。

如果普通二维码都不灵敏，优先查焦距、镜头脏污、排线方向和锁扣，不要先改 Web3 协议。

## 关键接口

### 1. USB-to-UART / 下载口

这个口只用于：

- 刷机。
- 串口监控。
- 读取启动日志。

不要把这个口当成 USB Host 外设口，也不要把 CCID 读卡器插到这里。

### 2. USB OTG 口

这个口才是给外设做 USB Host 的口，但有一个前提：

- OTG 口本身**没有稳定供电就不能用**。
- 直连无供电 OTG 线通常不够。
- 需要 `带电 OTG 转接线` 或 `外接供电 Hub`。

如果没有外部供电，常见现象是：

- 读卡器灯只闪一下。
- 设备枚举不到。
- 只能看到 Hub 或完全没有 USB 设备。
- CCID 能识别但 ATR 上不来。

## PN5180 NFC 推荐接法

当前 NFC 主路线是 PN5180，不是 PN532。实测 PN5180 模块用开发板 `5V` 供电才稳定，信号线仍只接 GPIO。

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

`5V` 只接 PN5180 电源输入，不接任何 GPIO。手机贴上 PN5180 天线区域有 NFC 感应，说明射频场已经起来；如果手机也没反应，优先查供电、GND、焊接和天线摆位。

详细说明看 [PN5180 NFC 接线和使用说明](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)。

## USB 智能卡推荐接法

推荐链路如下：

```text
ESP32-P4 OTG 口
-> 带电 OTG 转接线 或 外接供电 Hub
-> ACR39U-NF 读卡器
-> Satochip 卡
```

![带供电 OTG 智能卡接法实拍图](screens/powered_otg_smartcard_setup.jpg)

如果是你发的那种“转接头上带灯、并且有外接供电”的接法，就属于正确方向。重点不是外观，而是它必须真的给 OTG Host 侧供上电。

供电安全提醒：使用合规的带供电 OTG Y 线或真正外接供电 Hub，避免 5V 回灌/反供；不要把外部 5V 打到下载口；接线发热、反复掉电或闻到异味时立即断电。

### 连接顺序

1. 先把开发板的下载口接到电脑，保留刷机和日志通道。
2. 再把 OTG 口接到带电 Hub 或带电 OTG 转接线。
3. 读卡器插到 Hub 下游或转接线末端。
4. 智能卡插进读卡器。
5. 再进入 KernSigner 的 `设备检查 -> 智能卡检测`。

### 不要这样接

- 不要把读卡器插到下载口。
- 不要把无供电 OTG 线当成可用方案。
- 不要把“电脑能识别读卡器”当成“ESP32-P4 直插就能用”。
- 不要把 Hub 只当转接头，Hub 自己也要真供电。

## 为什么要带电 OTG

ACR39U-NF 这类 CCID 读卡器在这块板子上比较吃供电条件。没有外接供电时，USB Host 链路很容易卡在这些阶段：

- 只枚举到 Hub。
- 读卡器刚亮一下就掉。
- 读卡器枚举到了，但卡片无法上电。
- ATR 读不到。

所以 KernSigner 的智能卡相关文档都默认把 `带电 OTG` 当成前置条件，而不是可选项。

## 当前软件对应入口

- `设备检查 -> 智能卡检测`
- `连接钱包 -> 比特币钱包 -> 智能卡账户`
- `连接钱包 -> Web3钱包 -> 智能卡账户`
- `扫码签名 -> 智能卡 -> 扫码 Web3`
- `扫码签名 -> 智能卡 -> 路径地址`
- `扫码签名 -> 智能卡 -> 观察公钥`

## 无线关闭

ESP32-P4 主控本身没有 Wi-Fi / BLE。该 Waveshare 板上的无线能力来自板载 ESP32-C6 伴随芯片。KernSigner 当前固件会在 bootloader 和 app 启动早期把 `GPIO54 / C6_CHIP_PU` 拉低，保持 ESP32-C6 禁用。

正常日志应看到：

```text
ESP32-C6 wireless companion held disabled on GPIO54
```

详细说明看 [Wi-Fi、蓝牙和无线连接极端关闭说明](WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md)。

## 你可以先看这些文档

- [刷机前检查](FLASH_PRECHECK.md)
- [PN5180 NFC 接线和使用说明](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)
- [NFC 智能卡操作和排错](NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md)
- [Wi-Fi、蓝牙和无线连接极端关闭说明](WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md)
- [智能卡真机验收清单](SMARTCARD_REAL_DEVICE_ACCEPTANCE.md)
- [智能卡供电和 OTG 排障](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md)
- [智能卡读卡器转接线测试流程](SMARTCARD_ADAPTER_TEST_WORKFLOW.md)
- [整机真机验收清单](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md)
- [第三方与归属](THIRD_PARTY.md)

## 额外说明

`components/wave_43/` 里的 BSP 代码只负责这块板子的显示、触摸、电源和总线初始化，不会替你解决读卡器的供电问题。智能卡那条链路要靠正确的 OTG 供电和接线。
