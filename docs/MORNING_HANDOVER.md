# Kern/Krux 真钱包功能版交付说明

> 2026-05-22 智能卡商业评审更新：外接供电后 ACR39U-NF + Satochip/SeedKeeper 已跑通主线。Satochip 已可测 Web3 连接码、OKX/Bitget 常见 EVM 签名、路径地址与 BTC 观察公钥；SeedKeeper 已可测设置 PIN、改 PIN、写入助记词、查看/导入条目和重置。新版 SeedKeeper 重置走错 PIN/错 PUK 到 `FF00`，不是旧 `B0 FF`。Satochip BTC PSBT/消息签名和 TypedData 仍未作为交付能力开放。仍按测试助记词/测试资金验收，未通过生产安全门槛前不要放真钱。

## 交付定位

这是 Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 的 Kern/Krux `0.0.7-rc1` 真钱包功能版。它已经把旧 Kern 真实钱包核心接入 Krux 风格中文界面，可作为硬件底包、低风险工具和真钱包主路径固件继续验收，但还不是可以直接放真钱的生产审计版。

商业钱包口径：

- 核心测试新增 PSBT 攻击拒签和 BTC 消息签名路径策略，`make -C main/core/test run` 必须通过。
- 正式资金版发布前必须执行 `tools/kern_delivery.sh prodcheck`，并完成 Secure Boot、Flash Encryption、NVS 加密、调试口/控制台关闭、panic/WDT 策略、clean release provenance 和 eFuse 流程。
- 当前开发配置下 `prodcheck` 会提示 Secure Boot / Flash Encryption / NVS 加密 / 调试口与控制台等条件未满足，所以不能标记为商业资金正式版。
- Waveshare 4.3 寸显示启动路径已经做过商业钱包级防崩溃加固：显示、触摸、LVGL 启动失败会记录日志并可控重启，不再靠 `assert` 或 abort 蓝屏。

本交付包里的 `kernsigner.bin` 是 app 分区升级固件，不是空白板完整 factory 刷机包。不要全擦后只刷这个文件；正常使用下面的 `appflash` 命令升级。

## 明早先看这几个文件

- `_release/kern_delivery_YYYYMMDD_HHMMSS/RELEASE_SUMMARY.txt`：本次交付包、固件 SHA256、截图数量和启动日志状态。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/PROJECT_PROGRESS_AND_PLAN.md`：项目总进展、详细计划、风险边界和后续阶段。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/FINAL_READINESS.txt`：最终交付状态，集中确认固件、截图、启动日志和安全边界。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/ACCEPTANCE_REPORT.txt`：模拟器首屏/底部截图、中文缺字、UI 烟测、滚动验收和按钮点击验收结果，最终必须是 `FINAL: PASS`。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/boot.log`：真机启动日志，应该看到屏幕、GT911 触摸和背光初始化成功。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/contact_sheet_key_pages.png`：关键页面拼图，方便快速确认中文 UI。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/screenshots/contact_sheet_all_top.png`：全部首屏拼图。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/screenshots/contact_sheet_all_bottom.png`：全部可滚动页面底部拼图。
- `_release/kern_delivery_YYYYMMDD_HHMMSS/DELIVERY_STATUS.md`：完整验收清单和风险边界。
- `docs/README_FIRST_DELIVERY.md`：交付前先读入口，统一当前版本定位。
- `docs/FLASH_PRECHECK.md`：刷机前检查，防止 app-only 包误刷。
- `docs/RELEASE_POINTERS_AND_HISTORY.md`：确认 `_release/LATEST_RELEASE.txt` 是否还是旧包，防止刷错版本。
- `docs/REAL_DEVICE_ACCEPTANCE_CHECKLIST.md`：整机真机验收清单。
- `docs/SMARTCARD_MIGRATION_MATRIX_20260520.md`：智能卡迁移状态、未开放功能、商业阻断项和后续开发顺序，避免把检测/观察公钥误当完整智能卡钱包。
- `docs/MULTI_EXPERT_REVIEW_20260520_SMARTCARD.md`：本轮多专家智能卡复查、已修复项和商业交付判断。
- `docs/COMMERCIAL_RELEASE_GATE.md`：商业生产版必须通过的安全门禁。
- `docs/SMARTCARD_REAL_DEVICE_ACCEPTANCE.md`：Satochip/SeedKeeper 真机验收步骤。
- `docs/SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md`：Satochip/SeedKeeper 实测操作手册。
- `docs/SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md`：智能卡连接码、签名和观察公钥证据模板。
- `docs/SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md`：确认未迁移智能卡高风险功能没有露出入口。
- `docs/SMARTCARD_CAPABILITY_BOUNDARY.md`：已开放和必须隐藏的智能卡能力边界。
- `docs/UNOPENED_FEATURES.md`：未开放功能红线，防止把参考项目能力误当已迁移。
- `docs/TROUBLESHOOTING_SMARTCARD_POWER_OTG.md`：ACR39U-NF 供电和 OTG 排障。
- `docs/REFERENCE_PROJECT_MAPPING.md`：`satochip-signer` 到 Kern 的文件映射。

`ACCEPTANCE_REPORT.txt` 的 `FINAL: PASS` 只代表对应交付包当时的模拟器截图、中文缺字、UI 烟测、滚动和按钮导航验收通过。页面数量和按钮数量以该交付包内报告为准；本轮未出新固件、未更新截图，所以不能拿旧报告当作当前源码的最终验收。真机钱包创建、导入、备份或签名流程仍按下面的实机顺序逐项点测。

## 真机验收顺序

1. 开机后确认首页是黑底中文 UI，主入口为 `扫码签名`、`加载助记词`、`创建助记词`、`连接钱包`、`设置`、`设备检查`。
2. 进入 `连接钱包`，确认第一层是 `Web3钱包`、`比特币钱包`、`自定义路径`。
3. 进入 `连接钱包 -> 比特币钱包`，确认可选择 `已加载助记词` 或 `智能卡账户`，智能卡账户只开放 BlueWallet zpub/xpub。
4. 进入 `扫码签名`，确认可选择 `本机助记词` 或 `智能卡`。
5. 进入 `设备检查`，先看固件信息、硬件快照和相机/触摸/存储测试入口。
6. 点相机/扫码相关入口，用普通文本或 URL 二维码测试识别。
7. 点 `设备检查 -> 二维码`，确认可以生成测试二维码。
8. 点 `设备检查 -> 触摸`，确认触摸坐标和次数变化。
9. 点 `设置 -> 屏幕显示`，确认亮度能调节且最低不会黑屏。
10. 插入 FAT32 存储卡后进入存储卡测试/文件管理，确认读写测试和只读浏览可用。
11. 进入真钱包相关流程时，只用测试助记词验收加载/创建、钱包首页、扩展公钥、地址、备份和扫码签名入口能打开、返回、取消。

## 必须记住的安全边界

- 旧 Kern 钱包核心和真钱包入口已接入真机固件，但还需要按真机流程逐项验收。
- 不要输入真实资产助记词、私钥、SeedQR、PSBT 或钱包备份文件。
- 当前只允许用测试助记词、测试 PSBT 和公开测试数据验收。
- 智能卡已开放 Satochip Web3 连接/签名、路径地址和 BTC 观察公钥读取；仍需要用测试资金继续回归不同钱包和异常路径。
- BIP85 扩展、打印机、Satochip BTC PSBT/消息签名、TypedData 等未接专项不放进真钱包主菜单，不冒充已完成；智能卡写卡、改 PIN、重置、SeedKeeper 管理只按测试卡验收能力处理。

## 如果明早要重新刷

```bash
cd /home/ak/123/Kern
ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh appflash
```

刷完后脚本会自动抓启动日志并判断是否 PASS。

如果想一次完成“刷机、抓日志、重新打包、最终校验”：

```bash
cd /home/ak/123/Kern
JOBS=2 ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh shipflash
```

## 最终确认

```bash
cd /home/ak/123/Kern
tools/kern_delivery.sh final
```

看到 `final verify: PASS` 就表示最新交付包、固件 SHA256、截图报告、按钮验收、启动日志和关键文档都完整。

如果要检查是否达到商业真钱包生产安全配置：

```bash
tools/kern_delivery.sh prodcheck
```

当前如果这里失败，说明还只是验收固件，不是生产资金固件。

如果只是重新生成交付包并最终校验，不刷开发板：

```bash
tools/kern_delivery.sh ship
```

## 已知非阻塞日志

- `GPIO 26 is not usable`：背光仍已设置成功，当前不阻塞交付。
- `I2C pull-up` 提示：GT911 已识别成功，当前不阻塞触摸验收。
- `flash 32MB larger than image header 16MB`：固件按 16MB 镜像头使用，当前不阻塞启动。
- `single-point pointer events`：当前只验收单点触摸，不验收多指手势。
