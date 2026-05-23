# KernSigner 4.3 寸开发板固件

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
896c00b2cc488fb76d8bf208705ce3b0faa9b51142285b437a57c8a69d6c5c0b
```

## 编译同样固件

如果要从源码编译，使用 ESP-IDF v5.5.4：

```bash
git clone --recursive https://github.com/akg5188/KernSigner.git
cd KernSigner
. /home/ak/esp-idf-v5.5.4/export.sh
idf.py -B build_wave_43_fresh \
  -D SDKCONFIG=build_wave_43_fresh/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
  build
sha256sum build_wave_43_fresh/kernsigner.bin
```

正常情况下，`build_wave_43_fresh/kernsigner.bin` 应该和 app 固件 SHA256 一致：

```text
733082ea5a4946fccad2ea78ae8fea4cb933f34fcf709de9921f9c4c0b4decd4
```

## 注意

- 这是测试版，不是审计过的商业生产固件。
- 本目录这版已经本地构建并通过模拟器验收；如果电脑当前看不到 `/dev/ttyACM0` 或实际串口，先让开发板重新枚举，再刷机和抓启动日志。
- 真实资产使用前，要先完成安全审计、真机验收和生产配置检查。
- 第一次使用 ESP32-P4 4.3 摄像头时，先用普通黑白二维码手动调焦：保持 10-20 cm，捏住 OV5647 镜头外圈小幅旋转到二维码最清楚，再测 OKX、Bitget、TokenPocket 这类高密度动态码。
- 如果 USB 读卡器不亮，优先检查供电。ACR39U-NF 读卡器建议用外接供电 Hub 或稳定 OTG 供电方案。
