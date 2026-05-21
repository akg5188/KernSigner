# 刷机前检查

日期：2026-05-20

目标：防止刷错板、刷错口、把 app-only 包当 factory 包、或者在用户未确认时误出固件。

## 当前规则

- 用户说“先不要出固件”时，不编译最终固件、不复制 bin、不刷机。
- 用户确认要出固件后，才允许打包。
- 用户确认要刷机后，才允许执行刷机命令。
- 当前常规交付包里的 `kern.bin` 是 app 分区升级固件，不是空白板完整 factory 包。

## 刷机前逐项确认

- 目标板：Waveshare ESP32-P4 WiFi6 Touch LCD 4.3。
- 串口：确认 `ESPPORT` 指向下载口，不是 OTG 读卡器口。
- 固件类型：确认是 app-only 升级包还是 factory 全量包。
- SHA256：交付包内记录的 SHA256 必须匹配。
- NVS：确认本次是否保留 NVS。没有明确要求，不做全擦。
- 安全等级：测试资金验收版可以不通过 `prodcheck`，商业生产版必须通过。
- 日志：刷完必须抓 `boot.log`。
- 真机：刷完必须按 `REAL_DEVICE_ACCEPTANCE_CHECKLIST.md` 验收。

## 禁止操作

- 禁止全擦后只刷 `kern.bin`。
- 禁止把 OTG 口当下载口。
- 禁止用户未确认时刷机。
- 禁止在 `prodcheck` 失败时标记商业生产版。
- 禁止用真实助记词、真实私钥、真实资金做首轮验收。

## 常用命令

只升级 app：

```bash
cd /home/ak/123/Kern
ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh appflash
```

刷机并重新打包验收：

```bash
cd /home/ak/123/Kern
JOBS=2 ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh shipflash
```

生产门禁检查：

```bash
cd /home/ak/123/Kern
tools/kern_delivery.sh prodcheck
```
