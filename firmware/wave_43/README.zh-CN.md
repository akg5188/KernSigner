# KernSigner 4.3 寸开发板固件

**历史测试固件：**本目录是较早的镜像，不对应当前源码。2026-10-03 已刷入且可逐字节重建的版本见 [新版测试固件](../wave_43_20261003/README.zh-CN.md)。

适用硬件：

- Waveshare ESP32-P4-WiFi6-Touch-LCD-4.3
- 屏幕 480x800
- 触摸 GT911
- 相机 OV5647

这个目录里的固件是已经编译好的测试版。不会编译源码的人，可以直接下载这里的 `.bin` 文件刷机。

## 文件说明

| 文件 | 用途 |
| --- | --- |
| `kernsigner-wave43-0.0.7-rc1-untested-full.bin` | 推荐新手使用。完整固件，从 `0x0` 地址刷入。 |
| `kernsigner-wave43-0.0.7-rc1-untested-app.bin` | 只更新应用，从 `0x20000` 地址刷入。 |
| `bootloader.bin` | 启动程序。 |
| `partition-table.bin` | 分区表。 |
| `ota_data_initial.bin` | OTA 初始数据。 |
| `flash_args` | ESP-IDF 生成的刷机参数。 |
| `flasher_args.json` | ESP-IDF 生成的刷机参数 JSON。 |
| `SHA256SUMS.txt` | 校验文件，确认下载没有损坏。 |
| `flash_wave43_linux.sh` | Linux 一键刷完整固件脚本。 |
| `flash_wave43_windows.bat` | Windows 一键刷完整固件脚本。 |

## 新手刷机方法

优先刷完整固件：

```bash
cd /home/ak/123/Kern/firmware/wave_43
python3 -m esptool --chip esp32p4 -p /dev/ttyACM0 -b 115200 \
  --before default_reset --after hard_reset write_flash 0x0 \
  kernsigner-wave43-0.0.7-rc1-untested-full.bin
```

Linux 可以直接运行：

```bash
cd firmware/wave_43
./flash_wave43_linux.sh /dev/ttyACM0
```

Windows 可以双击或在命令行运行：

```bat
flash_wave43_windows.bat COM3
```

`COM3` 要换成你电脑里实际显示的串口。

## 校验固件

Linux/macOS：

```bash
sha256sum -c SHA256SUMS.txt
```

Windows PowerShell：

```powershell
Get-FileHash .\kernsigner-wave43-0.0.7-rc1-untested-full.bin -Algorithm SHA256
```

完整固件 SHA256：

```text
cda073a2cc766bc34cd9b9dc1aa75eb6164fb9e582ba35113b8ea9cb2889c3be
```

## 历史镜像与源码

当前仓库源码已更新，不能再用它复现本目录的历史镜像。要重建并验证当前刷入的版本，请使用 [2026-10-03 构建记录](../../docs/REPRODUCIBLE_BUILD.md)；历史镜像的实际 SHA256 以本目录 `SHA256SUMS.txt` 为准。

## 注意

- 这是测试版，不是审计过的商业生产固件。
- 本目录这版已经本地构建、通过模拟器验收，并在 `/dev/ttyACM0` 上完成 app-only 真机刷写；本地启动日志确认屏幕、GT911 触摸、LVGL task 和背光初始化正常。
- 真实资产使用前，要先完成安全审计、真机验收和生产配置检查。
- 第一次给手机小尺寸高密度签名二维码扫码前，先用普通黑白二维码手动调焦：保持 10-20 cm，捏住 OV5647 镜头外圈轻轻扭动/旋转到二维码边缘和小格子最清楚。只扭镜头外圈，不要扭排线或摄像头小板。普通二维码稳定后，再测 OKX、Bitget、TokenPocket 这类高密度动态码。
- 本版测试资金流程已实测 OKX、Bitget、imToken、MetaMask、Rabby、TokenPocket 的 Web3 扫码签名；BTC 侧 BlueWallet 和 Electrum 的观察钱包/PSBT 签名流程也已实测通过。
- 如果 USB 读卡器不亮，优先检查供电。ACR39U-NF 读卡器建议用外接供电 Hub 或稳定 OTG 供电方案。
