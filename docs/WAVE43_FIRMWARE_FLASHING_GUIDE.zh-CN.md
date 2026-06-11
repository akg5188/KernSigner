# Wave43 固件刷机教程

日期：2026-06-08

适用设备：Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3

这份文档只讲“怎么把 GitHub Release 下载的固件刷进开发板”。日常升级通常只需要刷一个文件：`kernsigner.bin`。

## 先看结论

日常升级用这个：

```text
kernsigner.bin -> 0x20000
```

只有空白板、bootloader 损坏、分区表变化、或者需要完整恢复时，才用完整包里的 4 个 bin。

重要区别：

- 只刷 `kernsigner.bin`：只更新主固件 App，不会改 bootloader。
- 完整刷机 4 个 bin：同时写入 bootloader、分区表、OTA 初始化数据和主固件。
- 如果设备以前已经刷过带无线关闭 hook 的 bootloader，只刷 `kernsigner.bin` 会保留原来的 bootloader hook。
- 如果设备刚从原厂固件回来、bootloader 被覆盖、或者要确认 bootloader 级无线关闭也写进去，用完整刷机 4 个 bin。

## 你要下载哪个文件

到 GitHub Release 页面下载：

```text
kernsigner.bin
```

这是日常升级用的主固件。不要把它刷到 `0x0`，也不要刷到 `0x10000`，当前分区布局下主固件地址是：

```text
0x20000
```

如果你想保存完整恢复包，再下载：

```text
kernsigner-wave43-pn5180-radio-off-20260608.zip
```

完整包里有 bootloader、分区表、OTA 初始化数据、主固件和说明。

## 准备电脑

需要 Python 和 esptool。

如果还没有 esptool：

```bash
python -m pip install esptool
```

Linux 下查看串口：

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

常见端口是：

```text
/dev/ttyACM0
```

Windows 下去“设备管理器”看 COM 口，例如：

```text
COM3
```

刷机用的是开发板下载/串口那个 USB 口，不是外接 USB 智能卡读卡器。

## 日常升级：只刷一个 kernsigner.bin

把 `kernsigner.bin` 放到当前目录，然后执行：

```bash
python -m esptool --chip esp32p4 -p /dev/ttyACM0 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x20000 kernsigner.bin
```

如果你的端口不是 `/dev/ttyACM0`，把命令里的端口换掉。

Windows 示例：

```powershell
python -m esptool --chip esp32p4 -p COM3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x20000 kernsigner.bin
```

刷完会自动复位。开机后看到 KernSigner 界面，就说明主固件刷进去了。

注意：这个方式不会更新 bootloader。如果你只是日常升级已配置好的 KernSigner
设备，这是推荐方式；如果你需要把 bootloader 里的无线关闭 hook 也重新写入，请用下面的完整刷机。

## 完整刷机：只有需要时才用

完整刷机需要 zip 里的 4 个 bin：

| 文件 | 地址 | 用途 |
| --- | --- | --- |
| `bootloader.bin` | `0x2000` | bootloader |
| `partition-table.bin` | `0x8000` | 分区表 |
| `ota_data_initial.bin` | `0xf000` | OTA 初始化数据 |
| `kernsigner.bin` | `0x20000` | 主固件 |

解压完整包后，在这几个文件所在目录执行：

```bash
python -m esptool --chip esp32p4 -p /dev/ttyACM0 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 kernsigner.bin
```

Windows 只需要把端口换成自己的 COM 口：

```powershell
python -m esptool --chip esp32p4 -p COM3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin 0x20000 kernsigner.bin
```

不确定时，优先用“日常升级”方式，只刷 `kernsigner.bin`。

但如果你刚刷回过原厂固件，或者不确定当前 bootloader 是否包含无线关闭 hook，
就不要只刷 app；用完整刷机把 bootloader 一起刷回 KernSigner 版本。

## 从源码编译后刷机

如果你是在本机源码目录里编译，不需要手动下载 Release。

进入项目目录：

```bash
cd /home/ak/123/KernSigner
```

加载 ESP-IDF：

```bash
source /home/ak/esp-idf-v5.5.4/export.sh
```

构建当前 Wave43 radio-off 固件：

```bash
idf.py -B build_wave_43_radio_off build
```

刷机：

```bash
idf.py -B build_wave_43_radio_off -p /dev/ttyACM0 flash
```

这个 `idf.py flash` 会按构建目录里的 flash 参数刷完整需要的分区。

## 刷机后确认

最简单的确认：

1. 屏幕能进入 KernSigner。
2. 触摸正常。
3. NFC 智能卡页面能看到 PN5180。
4. 连接钱包和签名流程能进入。

如果你抓串口日志，正常启动里应该能看到类似：

```text
Loaded app from partition at offset 0x20000
ESP32-C6 wireless companion held disabled on GPIO54
Display initialized successfully
```

这说明主固件从 `0x20000` 启动，无线 companion 也被固件保持关闭。

## 常见错误

### 找不到串口

现象：

```text
Could not open /dev/ttyACM0
```

处理：

1. 重新插拔开发板 USB。
2. 换能传数据的 USB 线。
3. 换开发板另一个 USB 口。
4. Linux 下重新运行：

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

### 写入时一直 Connecting

处理：

1. 按一下 RESET 再试。
2. 需要时按住 BOOT，再点 RESET，然后松开 BOOT。
3. 换 USB 线或 USB 口。

### 刷错地址

日常升级必须是：

```text
0x20000 kernsigner.bin
```

不要把 `kernsigner.bin` 刷到 `0x0`、`0x10000` 或 `0x8000`。

### 刷完黑屏

先确认刷的是 Wave43 固件，不是其它屏幕尺寸固件。

当前发布只给这个板用：

```text
Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3
```

### USB 读卡器端口和刷机端口混淆

刷机端口是开发板自己的串口，通常是 `/dev/ttyACM0`。

外接 USB CCID 智能卡读卡器是给智能卡用的，不是刷开发板固件用的。

## 不要做什么

- 不要随便 `erase_flash`，除非你明确知道会清掉什么。
- 不要把 app-only 的 `kernsigner.bin` 当成完整 factory 包刷到其它地址。
- 不要把 5V 接到任何 GPIO。
- 不要刷到非 Wave43 板子。
- 不要把这个测试资金固件当成已审计生产真钱包。

## 相关文档

- [PN5180_NFC_WIRING_AND_USAGE.zh-CN.md](PN5180_NFC_WIRING_AND_USAGE.zh-CN.md)
- [NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md](NFC_SMARTCARD_OPERATION_AND_TROUBLESHOOTING.zh-CN.md)
- [WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md](WIRELESS_RADIO_OFF_EXTREME_GUIDE.zh-CN.md)
- [FLASH_PRECHECK.md](FLASH_PRECHECK.md)
- [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md)
