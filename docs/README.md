# KernSigner 文档入口

日期：2026-05-23

这个目录里有操作手册、排障手册、真机记录、发布记录和安全边界。日常使用不要从所有文件里乱翻，先按下面这张表选入口。

当前固件仍是测试资金验收版，不是审计完成的生产钱包。真实资产使用前，必须完成生产发布门禁、真机回归和独立安全审查。

## 最先看这几篇

| 你现在要做什么 | 先看 |
| --- | --- |
| 第一次上手、刷机、开机、创建或加载测试助记词 | [小白照抄完整使用手册.zh-CN.md](小白照抄完整使用手册.zh-CN.md) |
| 设备出问题，想按现象一步步排查 | [故障排查照抄手册.zh-CN.md](故障排查照抄手册.zh-CN.md) |
| 扫码慢、扫不到、小尺寸高密度码、OKX 圆点码、树莓派摄像头复刻 | [QR_CAMERA_TROUBLESHOOTING.zh-CN.md](QR_CAMERA_TROUBLESHOOTING.zh-CN.md) |
| 连接 OKX、Bitget、MetaMask、Rabby、TokenPocket、BlueWallet、Electrum | [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md) |
| Satochip、SeedKeeper、智能卡 PIN/PUK、外接供电 OTG | [SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md) |
| 找某个菜单入口在哪里 | [功能菜单逐项索引.zh-CN.md](功能菜单逐项索引.zh-CN.md) |
| 重新编译、刷机、复现当前固件 | [REPRODUCIBLE_BUILD.md](REPRODUCIBLE_BUILD.md) 和 [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md) |
| 判断能不能当生产版或商业版 | [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md) |

## 阅读规则

1. 新手只看“小白手册”和“故障排查照抄手册”就够了。
2. 扫码问题统一先看 `QR_CAMERA_TROUBLESHOOTING`，OKX 事故复盘只当证据和踩坑记录。
3. 智能卡问题先确认供电和 OTG，再看 Satochip/SeedKeeper 手册。
4. 构建和刷机必须先确认 full/app 类型、地址、SHA256 和串口。
5. 评审、交付、历史记录类文档不要当作当前操作步骤。

## 扫码和摄像头

| 文档 | 用途 |
| --- | --- |
| [QR_CAMERA_TROUBLESHOOTING.zh-CN.md](QR_CAMERA_TROUBLESHOOTING.zh-CN.md) | 日常主入口：对焦、小尺寸高密度码、动态码、桌面 ZBar 验证、树莓派复刻 |
| [OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md](OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md) | OKX 圆点动态码从完全没反应到跑通的事故复盘 |
| [WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md](WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md) | Web3 钱包二维码格式兼容坑 |
| [TROUBLESHOOTING_GENERAL.zh-CN.md](TROUBLESHOOTING_GENERAL.zh-CN.md) | 相机黑屏、触摸、显示、构建等通用排障 |

最重要的结论：ESP32-P4 4.3 的 OV5647 摄像头首次使用必须手动调焦。OKX 圆点码不是新协议，排障顺序是 `成像 -> QR 解码 -> UR 拼包 -> CBOR/Web3 解析 -> 签名页面`。

## 日常操作

| 文档 | 用途 |
| --- | --- |
| [USER_QUICK_START.zh-CN.md](USER_QUICK_START.zh-CN.md) | 更短的新手快速上手 |
| [全功能操作总手册.zh-CN.md](全功能操作总手册.zh-CN.md) | 当前用户可见流程总览 |
| [零基础第一次上手教程.zh-CN.md](零基础第一次上手教程.zh-CN.md) | 零基础首次使用路线 |
| [连接钱包教程.zh-CN.md](连接钱包教程.zh-CN.md) | Web3 钱包、BTC 观察钱包和扫码签名 |
| [MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md](MNEMONIC_CREATION_BIP39_VERIFY.zh-CN.md) | 创建助记词和 BIP39 验证 |
| [BACKUP_AND_RECOVERY_GUIDE.zh-CN.md](BACKUP_AND_RECOVERY_GUIDE.zh-CN.md) | 助记词、熵、二维码、KEF、智能卡备份恢复 |
| [SECURITY_PIN_GUIDE.zh-CN.md](SECURITY_PIN_GUIDE.zh-CN.md) | 开发板 PIN、智能卡 PIN、PUK 和剩余次数 |

## 硬件和智能卡

| 文档 | 用途 |
| --- | --- |
| [HARDWARE_OVERVIEW_AND_OTG.md](HARDWARE_OVERVIEW_AND_OTG.md) | 开发板、摄像头、OTG、供电和接口说明 |
| [TROUBLESHOOTING_SMARTCARD_POWER_OTG.md](TROUBLESHOOTING_SMARTCARD_POWER_OTG.md) | ACR39U-NF、供电 Hub、OTG 读卡器排障 |
| [pn5180_bringup/README.zh-CN.md](../pn5180_bringup/README.zh-CN.md) | PN5180 NFC 模块接线、铁氧体摆位和首次上板测试 |
| [SMARTCARD_ADAPTER_TEST_WORKFLOW.md](SMARTCARD_ADAPTER_TEST_WORKFLOW.md) | 只测读卡器和 APDU 状态的验收流程 |
| [SMARTCARD_CAPABILITY_BOUNDARY.md](SMARTCARD_CAPABILITY_BOUNDARY.md) | 智能卡哪些功能开放、隐藏或禁止 |
| [SMARTCARD_REAL_DEVICE_ACCEPTANCE.md](SMARTCARD_REAL_DEVICE_ACCEPTANCE.md) | 智能卡真机验收 |

## 构建、刷机和发布

| 文档 | 用途 |
| --- | --- |
| [FLASH_PRECHECK.md](FLASH_PRECHECK.md) | 刷机前检查，防止刷错包、刷错地址、刷错板 |
| [BUILD_FLASH_DEBUG_GUIDE.zh-CN.md](BUILD_FLASH_DEBUG_GUIDE.zh-CN.md) | 构建、刷机、串口、模拟器、交付命令 |
| [REPRODUCIBLE_BUILD.md](REPRODUCIBLE_BUILD.md) | 当前预置固件的可复现构建记录 |
| [UNTESTED_FIRMWARE_NOTICE.md](UNTESTED_FIRMWARE_NOTICE.md) | 随仓库固件的测试版说明 |
| [RELEASE_POINTERS_AND_HISTORY.md](RELEASE_POINTERS_AND_HISTORY.md) | 发布文件、哈希和历史指针 |
| [DELIVERY_STATUS.md](DELIVERY_STATUS.md) | 当前交付范围、验收步骤和边界 |

## 安全和生产边界

| 文档 | 用途 |
| --- | --- |
| [COMMERCIAL_RELEASE_GATE.md](COMMERCIAL_RELEASE_GATE.md) | 生产发布必须满足的条件 |
| [security-plan.md](security-plan.md) | 安全计划 |
| [secure-boot.md](secure-boot.md) | Secure Boot、签名、锁定和发布流程 |
| [UNOPENED_FEATURES.md](UNOPENED_FEATURES.md) | 故意不开放的功能 |
| [PROJECT_NOTICE.md](PROJECT_NOTICE.md) | AI 项目、学习用途和资金风险提示 |

## 证据、截图和历史记录

| 文档或目录 | 用途 |
| --- | --- |
| [screens/](screens/README.md) | 当前截图和自动验收图片，公开批次是 `current_20260523_163539` |
| [logs/](logs/) | 启动日志和测试日志 |
| [REAL_DEVICE_ACCEPTANCE_CHECKLIST.md](REAL_DEVICE_ACCEPTANCE_CHECKLIST.md) | 真机验收清单 |
| [PROJECT_PROGRESS_AND_PLAN.md](PROJECT_PROGRESS_AND_PLAN.md) | 项目进度和阶段计划 |
| [MORNING_HANDOVER.md](MORNING_HANDOVER.md) | 交接记录 |
| [DOCUMENTATION_COVERAGE.md](DOCUMENTATION_COVERAGE.md) | 文档覆盖关系和维护规则 |
| [THIRD_PARTY.md](THIRD_PARTY.md) | 第三方代码、许可证和归属 |

## 维护规则

- 代码改了用户流程，必须同步更新对应中文手册。
- 扫码、相机、Web3 QR、UR、ZBar、quirc 相关改动，必须同步更新 `QR_CAMERA_TROUBLESHOOTING`。
- 构建脚本、固件文件、哈希、刷机地址变化，必须同步更新 `REPRODUCIBLE_BUILD`、`BUILD_FLASH_DEBUG_GUIDE` 和 `firmware/wave_43/README.zh-CN.md`。
- 历史复盘可以保留细节，但日常入口必须保持短、直、能照做。
