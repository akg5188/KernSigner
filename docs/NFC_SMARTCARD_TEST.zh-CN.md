# PN532 NFC 旧方案历史说明

日期：2026-06-08

这份文档只保留 PN532 旧测试路线的历史说明。当前 KernSigner 主 NFC 方案已经切到 **PN5180**；PN532 不再参与主固件 probe、APDU、状态报告或 fallback。新接线、新测试、新排错请看：

- [PN5180 NFC 接线和使用说明](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)
- [NFC 智能卡操作和排错](NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md)

不要再按这份旧文档给新固件接 PN532。当前 PN5180 已经占用 `GPIO31` 做 `BUSY`，旧 PN532 I2C 方案曾使用 `GPIO31` 做 `SCL`，两套线不能同时接。当前主固件也不会把 PN532 当备用读卡器。

## 当前推荐

当前推荐接法是 PN5180：

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

主固件状态页应显示 `Active: NFC PN5180`。

## PN532 旧接线记录

旧 PN532 红板测试时曾使用 I2C：

```text
PN532 VCC -> 开发板 3V3
PN532 GND -> 开发板 GND
PN532 SDA -> 开发板 GPIO28
PN532 SCL -> 开发板 GPIO31
```

这个旧路线曾经用于验证 PN532 firmware、UID、ISO-DEP/APDU，但现在不是主路线。

## 为什么不继续推荐 PN532

- 你当前 PN5180 模块已经真机跑通，手机贴上有反应，智能卡状态读取也已经变快。
- PN5180 信号更强，更适合现在的外壳和贴卡测试。
- PN532 红板质量差异大，I2C 上拉、电源、拨码和焊接都容易踩坑。
- PN5180 当前占用了 `GPIO31`，和旧 PN532 `SCL` 冲突。

## 如果你只是查历史故障

旧 PN532 常见问题仍可参考：

| 现象 | 旧判断 |
| --- | --- |
| PN532 发热 | 可能 VCC/GND 接反、错接 5V、错排或模块短路 |
| 扫不到 I2C 0x24 | 查 SDA/SCL、拨码、3V3/GND |
| UID 有但 APDU 不稳 | 卡片位置、I2C 干扰、PN532 天线和供电 |
| USB 读卡器正常但 PN532 不正常 | 多半是 PN532 硬件、天线或 I2C 问题 |

如果你现在是在测当前正式路线，不要继续在 PN532 上花时间，直接回到 PN5180 文档。
