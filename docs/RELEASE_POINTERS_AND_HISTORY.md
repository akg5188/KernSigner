# 发布包指针与历史版本说明

日期：2026-05-20

本文件用于防止刷错旧包。代码、文档、截图和 `_release/LATEST_RELEASE.txt` 不一定同时更新，必须分开确认。

## 当前规则

- `_release/LATEST_RELEASE.txt` 只指向“上一次已经生成的交付包”。
- 只改源码或文档不会自动更新 `_release/LATEST_RELEASE.txt`。
- 用户明确确认“出固件”之前，不允许编译新固件、复制 bin、打包到下载目录或刷机。
- 普通 `ship/shipflash` 只能生成开发验收包。
- 商业真钱包候选必须使用 `prodship/prodshipflash`，并且 `PRODUCTION_CHECK.txt` 与 `MANUFACTURING_READINESS.txt` 必须通过。

## 2026-05-20 状态

本轮对 `/home/ak/123/satochip-signer` 与 Kern 做了智能卡迁移复查、菜单收敛、安全边界文档和低风险稳定性修复。

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
