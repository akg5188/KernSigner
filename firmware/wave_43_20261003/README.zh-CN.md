# Wave43 2026-10-03 测试固件

本目录对应 2026-10-03 在 ESP32-P4 4.3 寸开发板上完整擦除后刷入、并通过启动日志验证的固件。仅用于测试，未经生产安全审计，请勿用于真实资产。

固件版本号仍为 `0.0.7-rc1`；请用目录名和 `SHA256SUMS.txt` 区分本次镜像与 `firmware/wave_43/` 的历史镜像。

| 文件 | 刷写地址 |
| --- | --- |
| `bootloader.bin` | `0x2000` |
| `partition-table.bin` | `0x8000` |
| `ota_data_initial.bin` | `0xf000` |
| `kernsigner.bin` | `0x20000` |
| `kernsigner-full.bin` | `0x0`，包含以上四段镜像 |

下载后先在本目录执行 `sha256sum -c SHA256SUMS.txt`。确认端口属于 ESP32-P4 开发板，再用 ESP-IDF v5.5.4 附带的 esptool 刷写。下面的 `/dev/ttyACM1` 仅为本次实测端口，实际端口可能不同。

完整擦除会删除设备 Flash 的所有数据（包括 NVS 和历史 SPIFFS）；只有确定无需保留数据时才执行：

```bash
python -m esptool --chip esp32p4 -p /dev/ttyACM1 -b 115200 erase_flash
python -m esptool --chip esp32p4 -p /dev/ttyACM1 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x2000 bootloader.bin 0x8000 partition-table.bin \
  0xf000 ota_data_initial.bin 0x20000 kernsigner.bin
```

也可以在擦除后把 `kernsigner-full.bin` 单独刷到 `0x0`。日常只更新应用时，把 `kernsigner.bin` 刷到 `0x20000`，不要刷到 `0x0`。完整构建与逐字节复现步骤见 [构建记录](../../docs/REPRODUCIBLE_BUILD.md)。

本次只确认了完整写入、写后哈希校验、从 `0x20000` 启动以及屏幕和触摸初始化；扫码、签名、BIP85 和智能卡等实际操作仍需真机回归。没有启用 Secure Boot、Flash Encryption 或 NVS Encryption，也没有操作 eFuse。
