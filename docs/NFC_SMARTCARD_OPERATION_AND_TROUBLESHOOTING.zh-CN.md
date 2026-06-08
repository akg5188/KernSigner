# NFC 智能卡操作和排错

日期：2026-06-08

这份文档只写 NFC 智能卡怎么用、怎么判断跑通、常见问题怎么排。当前主方案是 **PN5180 NFC 模块**，不是 PN532。

一句话：PN5180 负责让 Satochip / SeedKeeper 这类非接触 CPU 智能卡贴卡通信；上层的状态读取、地址、公钥、签名、SeedKeeper 列表仍然走同一套智能卡 APDU 逻辑。

## 当前读卡方式

| 方式 | 当前状态 | 用途 |
| --- | --- | --- |
| PN5180 NFC | 当前主方案，已真机跑通 | 贴卡读 Satochip / SeedKeeper |
| USB CCID 读卡器 | 保留，可用但要外接供电 OTG | 接触式读卡器，例如 ACR39U-NF |
| PN532 NFC | 当前禁用 | 只保留旧测试文档，不参与 probe、APDU 或 fallback |

所有连接钱包、签名、状态、公钥、SeedKeeper 操作都会先使用 PN5180。PN5180 硬件已经起来但卡没贴好时，不会自动跳去 USB；只有 PN5180 硬件不可用时，USB CCID 才作为备用。

## NFC 接线入口

PN5180 最终接线不要在这里凭记忆接，按专门接线文档照抄：

[PN5180 NFC 接线和使用说明](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)

核心接法如下：

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

## 跑通标准

先看三个层级，不要一开始就测签名。

| 层级 | 怎么判断 |
| --- | --- |
| 射频场 | 手机贴上 PN5180 天线区域有 NFC 感应 |
| 模块通信 | 串口出现 `PN5180 ready on SPI1 ...` |
| 智能卡 APDU | 状态页或日志出现 `ISO-DEP APDU OK`、`SW=9000` |

页面里正常会看到类似：

```text
Selected: NFC PN5180
Active: NFC PN5180

NFC PN5180
Initialized: yes
PN5180 present: yes
RF field: yes
Card present: yes
ISO-DEP/APDU: yes
```

如果 `Card present: yes` 但 `ISO-DEP/APDU: no`，说明可能贴到的不是 CPU 智能卡，或者卡片没有进入 ISO14443-4。

## 推荐测试顺序

1. 不贴卡开机，看 PN5180 初始化日志。
2. 手机贴天线，看有没有 NFC 感应。
3. 智能卡贴天线，进入 `设备检查 -> 智能卡检测`。
4. 先读状态，不要先签名。
5. Satochip 先测路径地址或公钥。
6. SeedKeeper 先测状态和列表。
7. 最后再测 Web3 / BTC 签名。

已经验证过后，先贴卡再打开状态页面也可以。当前固件已经处理了先贴卡场景，状态读取应比较快。

## Satochip 常用流程

连接 Web3 钱包：

```text
连接钱包
-> 选择 OKX / Bitget / MetaMask / Rabby / TokenPocket / imToken
-> 智能卡
-> 贴 Satochip
-> 输入 Satochip PIN
-> 显示连接二维码
```

读取地址或公钥：

```text
BTC 钱包 / 自定义派生 / Satochip 工具
-> 选择智能卡来源
-> 贴 Satochip
-> 输入 PIN
-> 读取地址、xpub、zpub 或其他公钥
```

扫码签名：

```text
扫码签名
-> 扫手机钱包签名请求
-> 选择智能卡
-> 贴 Satochip
-> 核对内容
-> 输入 Satochip PIN
-> 显示签名二维码
```

签名过程中卡不能挪开。NFC 比 USB 接触式读卡器更吃位置，卡片移动会导致 APDU 超时或响应不完整。

## SeedKeeper 常用流程

读状态：

```text
智能卡
-> SeedKeeper
-> 状态 / 空间 / 列表
-> 贴 SeedKeeper
-> 输入 SeedKeeper PIN
```

写入或读取秘密：

```text
助记词
-> 当前助记词
-> 写入智能卡
-> 选择 SeedKeeper
-> 贴卡并输入 SeedKeeper PIN
```

SeedKeeper 是保存秘密的卡，不要把它和 Satochip 签名钱包卡混成一个东西。SeedKeeper 列表里看到的内部 ID 不一定等于钱包主指纹，长期核对要以钱包指纹和地址为准。

## 常见状态字

| 状态字 | 常见含义 | 处理 |
| --- | --- | --- |
| `9000` | 成功 | 正常 |
| `63Cx` | PIN/PUK 错，还剩 x 次 | 停下来确认 PIN，不要乱猜 |
| `9C04` | 卡可能未初始化 | 去 setup / 设置 PIN |
| `9C06` | PIN 验证或权限状态不对 | 确认输入的是智能卡 PIN，不是开发板 PIN |
| `9C0C` | PIN 或 PUK 已锁 | 按对应卡的重置流程处理 |
| `9C20` | 当前命令或旧流程不支持 | SeedKeeper 重置不要走旧 `B0 FF` |
| `FF00` | SeedKeeper 恢复为空白卡成功 | 拔插卡后重新 setup |

## WTX 和等待

有些智能卡执行 APDU 时会请求 WTX，也就是让读卡器多等一会。当前 PN5180 固件已经处理 WTX，不会因为卡稍慢就马上失败。

如果仍然长时间卡住，按这个顺序查：

1. 卡片是否贴稳。
2. PN5180 是否 5V 供电。
3. `GND` 是否牢靠。
4. 杜邦线是否太长或接触不稳。
5. 卡是否已经锁 PIN、未 setup 或不是目标 applet。

## NFC 没反应怎么查

| 现象 | 优先查 |
| --- | --- |
| 手机贴 PN5180 没反应 | `5V/GND`、模块焊接、PN5180 是否真的上电 |
| 手机有反应，页面无卡 | 卡片位置、是否 CPU 智能卡、天线距离 |
| 初始化失败 | `SCK/MOSI/MISO/NSS/BUSY/RST` |
| 状态一会儿有一会儿没有 | 线太长、卡片移动、天线被金属或开发板铜皮影响 |
| `SW=9C06` 或 PIN 相关失败 | PIN 输错、卡未 setup、进错卡类型菜单 |
| USB 读卡器正常，NFC 不正常 | PN5180 供电、天线、卡片位置，不是上层 APDU 问题 |

## PN532 旧方案说明

旧文档 [NFC_SMARTCARD_TEST.zh-CN.md](NFC_SMARTCARD_TEST.zh-CN.md) 记录的是 PN532 红板 I2C 方案。那条路线以前用于验证 NFC APDU，但现在不参与当前主固件运行路径。

当前不要再同时按 PN532 和 PN5180 两套线接。特别是 `GPIO31` 现在给 PN5180 的 `BUSY` 用，不能再拿去当 PN532 的 `SCL`。当前传输层不会 probe PN532，也不会在 PN5180 失败后 fallback 到 PN532。

## 每次刷机后的快速验收

1. 启动日志里能看到无线关闭日志。
2. 启动日志里能看到 PN5180 初始化成功。
3. 手机贴 PN5180 有 NFC 感应。
4. 智能卡状态页显示 `Active: NFC PN5180`。
5. Satochip 状态或地址读取返回 `SW=9000`。
6. SeedKeeper 状态或列表能读取。
7. 测试资金签名能完成。

## 相关文档

- PN5180 接线：[PN5180_NFC_WIRING_AND_USAGE.zh-CN.md](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)
- Satochip / SeedKeeper 总手册：[SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md)
- USB 读卡器供电：[TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md)
- 无线关闭说明：[WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md](WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md)
