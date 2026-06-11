# Wi-Fi、蓝牙和无线连接极端关闭说明

日期：2026-06-08

这份文档写 KernSigner 当前“不动硬件、不烧 eFuse、不启用 Secure Boot / Flash Encryption”前提下，怎样把 Wi-Fi、蓝牙等无线连接压到最极端的关闭状态。

结论先放前面：

- ESP32-P4 主控本身没有 Wi-Fi / BLE 射频。
- Waveshare 这块 4.3 寸板带一个 ESP32-C6 无线伴随芯片。
- 当前固件在 bootloader 和 app 早期都把 ESP32-C6 的 `C6_CHIP_PU` 拉低，也就是让 C6 处于禁用/复位状态。
- 主程序里有编译期保护，发现 Wi-Fi、蓝牙、以太网、Thread、IEEE802.15.4、PHY、LWIP 等网络/无线栈启用时会直接拒绝编译。
- 这不是用电烙铁短路，也不是拆电容，而是用板子原理图上的使能脚把无线伴随芯片关住。

## 当前板子的无线结构

| 部分 | 说明 |
| --- | --- |
| ESP32-P4 | 主控，跑屏幕、相机、钱包、NFC、USB，没有内置 Wi-Fi / BLE |
| ESP32-C6 | 板载无线伴随芯片，负责 Wi-Fi / BLE / 802.15.4 这类无线能力 |
| `GPIO54` | ESP32-P4 连接到 ESP32-C6 的 `C6_CHIP_PU` |
| `C6_CHIP_PU` | ESP32-C6 使能脚，拉低时 C6 被关住 |

代码依据：

- `components/wave_43/include/bsp/esp32_p4_wifi6_touch_lcd_43.h` 定义 `BSP_ESP32_C6_CHIP_PU = GPIO54`。
- `components/wave_43/wave_43.c` 的 `bsp_wireless_disable()` 把 GPIO54 配成输出低电平并开启下拉。
- `bootloader_components/wireless_off_hooks/wireless_off_hooks.c` 在 bootloader 前后也把 GPIO54 拉低。

正常启动日志应看到：

```text
ESP32-C6 wireless companion held disabled on GPIO54
```

## 已经做了什么

| 层级 | 处理 |
| --- | --- |
| 硬件使能脚 | GPIO54 拉低，保持 ESP32-C6 `C6_CHIP_PU` 禁用 |
| bootloader | bootloader hook 提前把 GPIO54 拉低 |
| app 启动 | `app_main()` 第一行调用 `bsp_wireless_disable()` |
| 编译保护 | 如果无线/网络栈被打开，`main/main.c` 直接 `#error` |
| 功能设计 | KernSigner 走相机二维码、NFC、USB 智能卡，不提供联网流程 |

`main/main.c` 的保护范围包括：

```text
CONFIG_BT_ENABLED
CONFIG_ESP_WIFI_ENABLED
CONFIG_ESP_HOST_WIFI_ENABLED
CONFIG_ETH_ENABLED
CONFIG_IEEE802154_ENABLED
CONFIG_ESP_COEX_ENABLED
CONFIG_ESP_PHY_ENABLED
CONFIG_LWIP_ENABLE
CONFIG_OPENTHREAD_ENABLED
```

这些配置只要被启用，正式离线构建就不应通过。

## app-only 和完整刷机的区别

GitHub Release 通常会同时提供日常升级用的 `kernsigner.bin` 和完整刷机包。
这两个东西不要混淆：

| 刷法 | 会更新什么 | 对无线关闭的影响 |
| --- | --- | --- |
| 只刷 `kernsigner.bin` 到 `0x20000` | 只更新主固件 App | App 里的无线关闭会更新；已有 bootloader hook 会保留，但不会新写入 bootloader |
| 完整刷机 4 个 bin | bootloader、分区表、OTA 初始化数据、主固件 | bootloader hook 和 App 无线关闭都会写入 |

所以：

- 已经刷过 KernSigner 完整包的设备，日常升级只刷 `kernsigner.bin` 就行，bootloader hook 不会被删除。
- 刚刷回过原厂固件、bootloader 被覆盖、或者想重新确认 bootloader 级无线关闭写进去时，应该刷完整 4 个 bin。
- 只刷 app 不能把 bootloader hook 安装到一个没有该 hook 的 bootloader 里。

## 这和短路/拆电容不是一回事

把 GPIO54 输出低电平，是对无线伴随芯片的使能脚施加“关闭”状态，相当于按原理图控制它不上电或不启动。它不是把电源和地短在一起，也不是用烙铁强行破坏线路。

区别如下：

| 方法 | 性质 | 风险 |
| --- | --- | --- |
| GPIO54 拉低 `C6_CHIP_PU` | 固件控制使能脚 | 不破坏硬件，可恢复 |
| 不编译无线/网络栈 | 软件裁剪 | 不影响 NFC、相机、屏幕 |
| 拆电容/短路 | 物理破坏 | 容易伤旁边元件、板子电源或信号线 |
| 烧 eFuse / Flash Encryption | 芯片永久锁定类操作 | 不可逆，刷错可能变砖 |

所以当前做法是“在不动硬件、不烧 eFuse 的约束下最极端”，不是物理毁坏式断开。

## 为什么不拆电容或短路

不建议对无线芯片旁边的小电容下手：

- 小电容可能是去耦、滤波、晶振、RF 匹配或电源稳定用。
- 旁边多个电容不一定都只服务无线芯片。
- 焊盘太小，烙铁碰到相邻件很容易短路。
- 弄坏电源滤波可能让整块板开机不稳，不只是无线失效。
- 破坏后很难判断故障点，也不方便以后恢复。

当前文档和固件都按“不动硬件”处理。

## 为什么不启用 Secure Boot / Flash Encryption

这两个是生产发布安全功能，但它们不是“关闭无线”的必要条件。

当前没有启用它们，原因是：

- 用户当前要求是不动硬件，也不要不可逆锁死。
- Secure Boot / Flash Encryption 配错可能导致无法重新刷机或调试。
- eFuse 一旦烧错很难恢复。
- 当前阶段仍是测试资金验收版，不是最终审计生产发布。

生产版如果以后要做，需要单独按 [secure-boot.md](secure-boot.md) 和商业发布门禁完整执行，不能和这次无线关闭混在一起。

## 怎么验证无线确实被压住

### 1. 看启动日志

串口里应看到：

```text
ESP32-C6 wireless companion held disabled on GPIO54
```

### 2. 看 PN5180 仍然正常

无线关闭不应该影响 NFC。PN5180 正常时应看到：

```text
PN5180 ready on SPI1 SCK=52 MOSI=51 MISO=50 NSS=49 BUSY=31 RST=30 Hz=1000000
```

### 3. 看构建保护

`main/main.c` 里有无线/网络栈编译期检查。如果你改配置后触发：

```text
KernSigner must be built without wireless or network stacks.
```

说明配置把某个无线或网络栈打开了，要关掉后再构建。

### 4. 看应用功能

应用里不应该出现：

- Wi-Fi 配网
- 蓝牙配对
- 网络连接
- 远程同步
- MQTT/HTTP 联网功能
- BLE 钱包连接

KernSigner 的正常通道是：

- 摄像头扫二维码
- 屏幕显示二维码
- PN5180 NFC 智能卡
- USB CCID 智能卡读卡器
- 本机屏幕和触摸

## 当前方法的边界

这句话要说清楚：只靠固件控制，永远不能等同于“物理拆掉无线芯片”或“eFuse 永久锁死”。

在用户限定的条件里：

```text
不拆硬件
不短路
不烧 eFuse
不开 Secure Boot
不开 Flash Encryption
```

当前已经是合理的最极端方案：

```text
不编译无线/网络栈
+ bootloader 提前拉低 C6_CHIP_PU
+ app 启动第一步再次拉低 C6_CHIP_PU
+ 日志可验证
+ NFC/相机/USB 不被破坏
```

如果以后要比这更极端，只剩三类方向：

| 方向 | 是否当前采用 | 说明 |
| --- | --- | --- |
| 物理移除/断开 ESP32-C6 | 否 | 需要热风、显微焊接和原理图级确认 |
| eFuse / Secure Boot / Flash Encryption | 否 | 不可逆，需要生产发布流程 |
| 换无无线伴随芯片的板 | 否 | 最干净，但要重新适配硬件 |

## NFC 不算无线联网

PN5180 NFC 会产生很近距离的 13.56 MHz 场，用来读贴在天线上的智能卡。它不是 Wi-Fi，不是蓝牙，也不能联网。

保留 PN5180 的原因是它是智能卡读卡通道。关闭 Wi-Fi/蓝牙不等于关闭所有近距离读卡功能。

## 刷机后检查清单

1. 串口看到 `ESP32-C6 wireless companion held disabled on GPIO54`。
2. 串口看到 PN5180 初始化成功。
3. 应用里没有 Wi-Fi / BLE / 网络连接入口。
4. 手机贴 PN5180 有 NFC 感应。
5. Satochip / SeedKeeper 状态能读取。
6. QR 扫码和显示正常。
7. 屏幕、触摸、相机、USB 读卡器功能没有被无线关闭影响。

## 相关文档

- PN5180 接线：[PN5180_NFC_WIRING_AND_USAGE.zh-CN.md](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)
- NFC 智能卡排错：[NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md](NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md)
- 安全计划：[security-plan.md](security-plan.md)
- Secure Boot 生产说明：[secure-boot.md](secure-boot.md)
