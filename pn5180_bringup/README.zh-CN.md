# KernSigner PN5180 NFC 独立跑通准备

这个目录是给 PN5180 模块做第一次上板准备的。它先只管接线、安装方式和第一轮检查，不碰钱包逻辑。

这里现在包含一个独立 ESP-IDF 小固件：只初始化 PN5180 的 SPI/GPIO，复位模块，读取 EEPROM 版本区，确认硬件链路能说话。它不会写卡，不会打开钱包功能，也不会把 NFC 接进签名流程。

PN5180 是非接触式 NFC 前端，不是 USB 接触式智能卡读卡器。它解决的是贴卡感应和 ISO14443 / APDU 这条链路，不是接触式卡座。

## 到货前先准备

- PN5180 模块本体，确认是 SPI 版，排针已经焊好或可焊。
- 母对母或母对公杜邦线，尽量短。
- 2.54mm 排针，如果模块还没焊针，先补齐。
- 铁氧体片或磁屏蔽贴，尺寸至少覆盖天线区域。
- 万用表。
- 绝缘胶带、双面胶或 3D 打印支架。
- 一张已经验证过能跑 APDU 的 contactless CPU 卡。
- 开发板的 USB 下载线，方便看串口日志。

## wave_43 当前实测接线

这块 PN5180 模块最后实测要用开发板 `5V` 供电才稳定，接 `3V3` 时手机贴上去没有可靠 NFC 感应。注意：`5V` 只接 PN5180 的电源输入脚，不接任何 GPIO。

首测建议按 **8 根线** 接：

- `VCC`
- `GND`
- `SCK`
- `MOSI`
- `MISO`
- `NSS`
- `BUSY`
- `RST`

### 最终跑通版

| PN5180 模块 | wave_43 开发板 | 说明 |
| --- | --- | --- |
| `5V` / `VCC` | `5V` | 本次实测模块用 5V 才稳定 |
| `GND` | `GND` | 必须共地 |
| `SCK` | `GPIO52` | SPI 时钟 |
| `MOSI` | `GPIO51` | SPI 主发 |
| `MISO` | `GPIO50` | SPI 主收 |
| `NSS` / `SS` | `GPIO49` | SPI 片选 |
| `BUSY` | `GPIO31` | 首测建议接上 |
| `RST` / `NRESET` | `GPIO30` | 复位 |
| `IRQ` | 不接 | 首测先禁用 |
| `GPO1` / `AUX` / `REQ` | 不接 | 首次跑通先别接太多线 |

照抄版：

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

### 不要先碰的脚

- `SCL` / `SDA`
- `DP` / `DM`
- 把 `5V` 接到任何 GPIO
- 任何已经给屏幕、触摸、摄像头或 USB 用掉的脚

## 物理摆位

- 天线面朝外壳开口。
- 铁氧体贴在模块背面，也就是模块和开发板中间。
- 不要让金属螺丝、屏蔽罩、电池、排线压在天线附近。
- 外壳里先留 1 到 3 mm 的塑料隔层，不要把天线直接顶死在金属件上。
- SPI 线尽量短，先桌面裸测，再考虑固定进外壳。

## 第一次上电顺序

1. 先在桌面裸测，不装外壳。
2. 用万用表确认 `VCC` 和 `GND` 没有短路。
3. 先接 `5V/VCC`、`GND`，再接 SPI 线。
4. 第一次通电不贴卡，先看模块是否异常发热。
5. 等驱动准备好后，再看能否识别 PN5180。
6. 再把已知可用的卡贴到天线面上测感应和 APDU。
7. 稳定后再把模块挪进外壳，继续复测。

## 明天到货后先做什么

- 先对照模块丝印，把 `SCK/MOSI/MISO/NSS/BUSY/RST` 一根根接对。
- 按当前实测接 `5V` 给 PN5180 供电，但不要把 `5V` 接到 GPIO。
- 先把模块放在开发板外面试，不要立刻塞进壳里。
- 先看天线面和卡之间的距离，不稳就先调摆位和铁氧体，再考虑别的。
- 如果模块本身有 `IRQ/GPO1/AUX/REQ`，先别一次接满。

## 预期的结果

如果这一步顺利，后面才值得继续做：

- PN5180 SPI 能稳定初始化。
- 能检测到卡在天线范围内。
- 能稳定跑 ISO14443A / ISO-DEP / APDU。
- 再把它接回 KernSigner 的智能卡传输层。

如果卡感应还是飘，先改天线摆位、铁氧体和外壳距离，不要先怀疑主固件。

主固件和日常使用说明看：

- [PN5180 NFC 接线和使用说明](../docs/PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)
- [NFC 智能卡操作和排错](../docs/NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md)

## 独立小固件

默认引脚就是上面“最终跑通版”的接线：

| 信号 | GPIO |
| --- | --- |
| `SCK` | `GPIO52` |
| `MOSI` | `GPIO51` |
| `MISO` | `GPIO50` |
| `NSS` / `SS` | `GPIO49` |
| `BUSY` | `GPIO31` |
| `RST` / `NRESET` | `GPIO30` |
| `IRQ` | 不接 / `-1` |

供电按上面的最终跑通版接 `5V` 和 `GND`。

这个小固件还会继续把 `GPIO54 / C6_CHIP_PU` 拉低，保持板载 ESP32-C6 无线伴随芯片禁用。

构建：

```bash
cd /home/ak/123/KernSigner/pn5180_bringup
. /home/ak/esp-idf-v5.5.4/export.sh
idf.py -B build build
```

刷机并看日志：

```bash
idf.py -B build -p /dev/ttyACM0 flash monitor
```

硬件链路跑通时，串口里应该看到类似：

```text
PN5180 SPI communication OK
EEPROM[00..15] raw: ...
Product version raw: ...
Firmware version raw: ...
EEPROM version raw: ...
```

如果卡住或失败：

- `PN5180 reset/BUSY wait failed`：优先查 `5V/GND/RST/BUSY`。
- `PN5180 READ_EEPROM failed`：优先查 `SCK/MOSI/MISO/NSS`，线尽量短。
- `BUSY` 长期为 `1`：模块可能没复位好、BUSY 接错、供电不稳，或模块没有进入 SPI 模式。

## 参考

- NXP PN5180: https://www.nxp.com/products/PN5180
- PN5180 datasheet: https://www.nxp.com/docs/en/data-sheet/PN5180A0XX-C1-C2.pdf
- Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3: https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3
