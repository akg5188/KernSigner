# 发布包指针与历史版本说明

日期：2026-05-23

本文件用于防止刷错旧包。代码、文档、截图和 `_release/LATEST_RELEASE.txt` 不一定同时更新，必须分开确认。

## 当前规则

- `_release/LATEST_RELEASE.txt` 只指向“上一次已经生成的交付包”。
- 只改源码或文档不会自动更新 `_release/LATEST_RELEASE.txt`。
- 用户明确确认“出固件”之前，不允许重新编译新固件、复制新的 bin、更新下载目录或刷机。
- 普通 `ship/shipflash` 只能生成开发验收包。
- 商业真钱包候选必须使用 `prodship/prodshipflash`，并且 `PRODUCTION_CHECK.txt` 与 `MANUFACTURING_READINESS.txt` 必须通过。

## 当前仓库内预置固件

仓库当前包含 `firmware/wave_43/` 下的 `0.0.7-rc1-untested` 预置固件：

- `kernsigner-wave43-0.0.7-rc1-untested-full.bin`
- `kernsigner-wave43-0.0.7-rc1-untested-app.bin`
- `SHA256SUMS.txt`

2026-05-23 当前预置固件哈希：

- app-only: `733082ea5a4946fccad2ea78ae8fea4cb933f34fcf709de9921f9c4c0b4decd4`
- full image: `896c00b2cc488fb76d8bf208705ce3b0faa9b51142285b437a57c8a69d6c5c0b`

这组固件是开发测试快照，不是商业生产版。刷机前先看 `firmware/wave_43/README.zh-CN.md` 和 `docs/UNTESTED_FIRMWARE_NOTICE.md`。

2026-05-23 本地复核：

- 构建：`JOBS=2 tools/signer_delivery.sh build` 通过。
- 模拟器验收：`docs/screens/delivery_20260523_175620/ACCEPTANCE_REPORT.txt`，`FINAL: PASS`。
- 单测：`./scripts/test.sh` 通过。
- 固件校验：`firmware/wave_43/SHA256SUMS.txt` 全部通过。
- OKX 桌面样本：静态图 + 1 fps 视频抽帧 `12/12 decoded`。
- 真机刷写：当前电脑未枚举出 `/dev/ttyACM*` 或 `/dev/ttyUSB*`，需开发板串口重新出现后补刷机和启动日志。

## 2026-05-20 状态

本轮对外部 `satochip-signer` 参考项目与 KernSigner 做了智能卡迁移复查、菜单收敛、安全边界文档和低风险稳定性修复。

本轮没有：

- 编译固件。
- 复制 bin 到下载目录。
- 更新 `_release/LATEST_RELEASE.txt`。
- 刷写真机。

因此，如果此时查看 `_release/LATEST_RELEASE.txt`，它可能仍指向旧交付包。旧包不包含本轮所有源码和文档改动。

## 出固件前必须记录

每次真正出包时，必须在交付包内记录：

- git commit 或工作区状态。
- 固件版本号。
- `kernsigner.bin` SHA256。
- 是否 app-only 包。
- 是否生产候选包。
- `prodcheck` 结果。
- 真机启动日志。
- 智能卡真机验收日志和截图。

没有这些记录，不要把包发给别人刷，也不要说它是正式版。
