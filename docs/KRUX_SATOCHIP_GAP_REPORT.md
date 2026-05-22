# Krux 与 Satochip 功能对照审计报告

日期：2026-05-18

> 历史报告说明：本文件是 2026-05-18 的静态审计记录，后续已经多次调整菜单和智能卡能力边界。当前是否可刷、是否可商用、哪些智能卡功能隐藏，以 `docs/README_FIRST_DELIVERY.md`、`docs/SATOCHIP_COMMERCIAL_REVIEW_20260520.md`、`docs/SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md` 和 `docs/RELEASE_POINTERS_AND_HISTORY.md` 为准。

本报告只做静态审计和代码修正记录。本轮没有编译固件、没有刷机、没有打包。

## 总结

当前 Kern 不是空壳：旧 Kern 真钱包核心已经接入 Krux 风格中文入口，加载/创建助记词、钱包首页、公钥、地址、描述符、备份、扫码 PSBT 签名和消息签名都能进入真实 C 页面。

当前 Kern 也不是 Krux + satochip-signer 的完整移植版：高级助记词工具、BIP85、钢板/点阵/1248、SeedKeeper 管理、Satochip 写卡/改 PIN/重置和 BTC PSBT/消息签名还没有完整 C 实现，不能作为真钱包正式功能宣称完成。Web3 观察钱包连接码、Satochip Web3 签名、USB CCID/APDU 基础、路径地址和 BTC 观察公钥已经接入可测范围。

2026-05-22 更新：Satochip Web3 连接/签名、USB CCID 检测、路径地址和 BTC 观察公钥已回到可测主线；SeedKeeper 设置 PIN、改 PIN、写入助记词、查看/导入条目和重置已进入测试卡验收范围。Satochip BTC PSBT/消息签名、TypedData 仍不能作为已交付生产能力宣传。当前智能卡实测操作以 `docs/SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md` 为准。

## 本轮检查范围

参考项目：

- `/home/ak/123/krux`
- `/home/ak/123/satochip-signer`

目标项目：

- `/home/ak/123/Kern`

检查方法：

- 静态阅读 Krux 功能入口、助记词工具、签名、Web3、备份、加载和设置相关源码。
- 静态阅读 satochip-signer 的树莓派固件、Android 智能卡 App、PC/SC CLI、Satochip Java/Kotlin/Python 协议层。
- 静态阅读 Kern 当前 C 代码、Krux shell 功能目录、真钱包 legacy 映射、PIN 保护、扫码签名和备份页面。
- 不运行构建，不刷开发板，不生成交付包。

## Kern 当前真钱包主路径

已接入真实 C 页面：

| 功能 | Kern 当前状态 | 说明 |
|---|---|---|
| 开机/PIN | 已接入 | 开机 PIN、自动锁定、三次错误后关机/重启保护已经接入。 |
| 加载助记词 | 已接入主路径 | 二维码、手动单词、存储/加密备份走旧 Kern 流程。 |
| 新建助记词 | 已接入主路径 | D6 骰子、手动单词、相机熵源走旧 Kern 流程。 |
| 钱包首页 | 已接入 | 显示网络、策略、指纹、会话入口。 |
| 扩展公钥 | 已接入 | 公钥派生、显示和二维码导出。 |
| 地址 | 已接入 | 收款/找零地址、二维码、地址核对入口。 |
| 描述符 | 已接入 | 单签/多签描述符管理相关入口。 |
| 助记词备份 | 已接入主路径 | 文字、二维码、加密备份；已补 0-2047 序号和指纹提示。 |
| PSBT 扫码签名 | 已接入主路径 | 交易解析、审查、签名、二维码导出。 |
| 消息签名 | 已接入主路径 | signmessage 格式解析、路径/地址/消息确认、签名导出。 |
| 显示/触摸/亮度 | 已接入 | 4.3 寸布局、触摸、背光设置。 |
| 相机测试/普通二维码 | 已接入低风险工具 | 普通二维码识别和相机预览可用于硬件验收。 |
| 存储卡测试/浏览 | 已接入低风险工具 | 读写测试、只读根目录浏览。 |

## Krux 功能差距

| Krux 功能 | Krux 参考位置 | Kern 状态 | 结论 |
|---|---|---|---|
| SeedQR/CompactSeedQR/UR/KEF 加载 | `src/krux/pages/mnemonic_loader.py` | 部分到已接 | 旧 Kern 加载主路径已接，但 Krux 全格式兼容需要逐项真机验收。 |
| 手动单词导入 | `mnemonic_loader.py` / `mnemonic_editor.py` | 已接 | 真实手动单词选择、校验、加载确认。 |
| 0-2047/0-7FF/八进制导入 | `mnemonic_loader.py` | 未完整 | 不进入真机菜单，尚无独立 C 输入页。 |
| 钢板二次还原导入 | `mnemonic_loader.py` / `secondary_mnemonic.py` | 未完整 | 尚无 C 版钢板数字解析和加载流程。 |
| TinySeed/OneKey/Binary Grid 导入 | `tiny_seed.py` | 未完整 | 尚无 C 版点阵识别和手动导入。 |
| Stackbit 1248 导入 | `stack_1248.py` | 未完整 | 尚无 C 版 1248 输入、预览、恢复。 |
| D6 骰子创建 | `new_mnemonic/dice_rolls.py` | 已接 | 旧 Kern D6 页面已接入。 |
| D20 骰子创建 | `new_mnemonic/dice_rolls.py` | 未完整 | 不进入真机菜单，尚无 C 版输入和校验流程。 |
| 扑克牌熵创建 | `login.py` | 未完整 | 不进入真机菜单，尚无 C 版卡牌输入和 iancoleman 规则转换。 |
| 十六进制熵创建 | `login.py` | 未完整 | 已列入口，尚无 C 版 hex 输入、长度检查和助记词转换。 |
| 相机熵创建 | `capture_entropy.py` | 已接 | 已接相机熵源页面，但真机相机稳定性仍要验收。 |
| 助记词文字备份 | `mnemonic_backup.py` | 已接 | 已显示单词、指纹、0-2047 序号。 |
| 助记词二维码备份 | `mnemonic_backup.py` | 已接 | 已显示指纹和 0-2047 序号摘要。 |
| 助记词编号备份 | `mnemonic_backup.py` | 部分 | 0-2047 已补到文字/二维码页，独立十进制/十六进制/八进制备份页未完成。 |
| 钢板打孔备份 | `mnemonic_backup.py` | 未完整 | 尚无位权拆分和打孔核对 C 页面。 |
| TinySeed/Stackbit 备份 | `tiny_seed.py` / `stack_1248.py` | 未完整 | 尚无 C 版点阵/1248 备份显示。 |
| BIP85 | `home_pages/bip85.py` | 未完整 | 不进入真机菜单，不可宣称可派生子助记词/密码/熵。 |
| 助记词 XOR | `home_pages/mnemonic_xor.py` | 未完整 | 不进入真机菜单，未接真实 XOR 分片/恢复。 |
| 第二助记词 | `home_pages/secondary_mnemonic.py` | 未完整 | 不进入真机菜单，未接临时第二助记词和钢板二次还原全流程。 |
| 附加口令 | `wallet_settings.py` | 部分 | 旧 Kern 钱包设置支持手动附加口令和指纹变化；Krux 的扫码附加口令未迁移。 |
| PSBT 扫码签名 | `psbt.py` / `home_pages/home.py` | 已接主路径 | 旧 Kern PSBT 扫码签名是真逻辑；多签无描述符时已禁用“不核对直接签名”。 |
| PSBT 文件签名 | `file_manager.py` | 未完整 | 不进入真机菜单，尚未从存储卡读取 PSBT 并进入签名流程。 |
| Bitcoin 消息签名 | `sign_message_ui.py` | 已接主路径 | 当前仅覆盖旧 Kern 已支持的消息格式。 |
| Web3 观察连接码 | `web3.py` / `web3_ui.py` | 已接 | 已接 EVM 地址二维码、观察账户连接码，并支持助记词或 Satochip 来源选择。 |
| Web3 签名 | `web3.py` / `web3_ui.py` | 已接主线 | 已接 Satochip Web3 扫码签名主线，OKX/Bitget 转账已真机跑通；复杂 TypedData/多钱包格式仍需回归。 |
| 打印机 | `print_page.py` | 未完整 | 默认不开放，避免敏感输出泄露。 |

## satochip-signer 功能差距

satochip-signer 里有三类参考代码：树莓派固件页面、Android 智能卡 App、PC/SC CLI 与 Satochip 协议库。它们可作为移植蓝图，但不能直接复制到 ESP32-P4 使用。

| 功能 | 参考位置 | Kern 状态 | 结论 |
|---|---|---|---|
| ACR39U PC/SC 读卡 | `pi-signer/src/main/java/.../PcscConnector.java` | 已改为 USB Host CCID | ESP32-P4 不走 PC/SC，Kern 走 USB Host CCID C 驱动。 |
| Android USB CCID | `app/src/main/java/com/tpsigner/UsbCcidCardChannel.kt` | 已接基础 | 已实现 CCID 枚举、PowerOn、GetSlotStatus、XfrBlock 和 APDU 发送；继续做异常回归。 |
| APDU 编解码 | `satochip-lib/src/main/java/org/satochip/io` | 已接基础 | 已有 C 版 Satochip APDU、状态码和错误文案处理。 |
| Satochip SELECT/GET_STATUS/PIN/xpub/sign | `SatochipCommandSet.java` | 已接主线 | 已接 SELECT/GET_STATUS/PIN、EVM 签名和 BTC 观察公钥读取；写卡/改 PIN/重置未开放。 |
| Secure Channel | `SecureChannelSession.java` | 已接基础 | 已接 secp256k1 ECDH、HMAC-SHA1、AES-CBC-PKCS7 和安全通道封装。 |
| Satochip 导出 xpub | `SatochipSigner.kt` | 已接 | 已接 xpub/ypub/zpub/tpub/upub/vpub 读取和二维码显示。 |
| Satochip BTC PSBT 签名 | `BitcoinPsbtSigner.kt` | 未接 | Kern 可复用现有 libwally PSBT 审查，再接外部卡签名。 |
| Satochip EVM 签名 | `SatochipSigner.kt` / `EvmTxEncoder.kt` | 已接主线 | 已接 Keccak/RLP/recId/Web3 回传，OKX/Bitget 转账已验证；复杂 TypedData 继续回归。 |
| TokenPocket 多分片 QR | `TpQrCodec.kt` / `tp_fragment.py` | 部分接入 | Web3 relay/中转 QR 主线已可扫码签名；TokenPocket 特殊格式还需专项样本回归。 |
| SeedKeeper 导入/导出 | `seedkeeper_utils.py` / `ToolsSeedkeeper*` | 未接 | 需要智能卡协议层和安全 UI。 |
| 智能卡工具菜单 | `ToolsTpSmartcardToolsView` / `ToolsSatochipView` | 部分接入 | 已接 Satochip Web3 连接/签名、路径地址、BTC 观察公钥和读卡状态；写卡、改 PIN、重置、SeedKeeper 管理、BTC PSBT/消息签名仍未开放。 |

## 本轮已经修正的风险点

| 风险点 | 修正 |
|---|---|
| 多签交易无描述符时可点“不核对直接签名” | 已改为“安全说明：必须先加载描述符”，不会设置跳过核对。 |
| PIN 文案写“擦除阈值”，但实际是关机/重启保护 | 已改成“错误关机保护/关机保护”，避免误导。 |
| 附加口令入口显示未开始但旧 Kern 有部分实现 | 已改为“待接服务”，并映射到真钱包设置；同时说明扫码附加口令未迁移。 |
| satochip-signer 的 BIP39 自检和智能卡工具没有真实 C 实现 | 已从真机菜单移除，不会发送 APDU，也不会冒充可用智能卡功能。 |
| 未实现的卡牌/hex/D20 入口误进新建总入口 | 已从真机菜单移除，只保留 D6、手动单词和相机随机。 |
| 打印备份误进旧备份页 | 已从真机菜单移除，不冒充打印功能。 |
| 未完成专项占据设备菜单 | 已清理菜单，只保留已接入真实流程的入口。 |
| 历史设置里可能残留宽松签名 | 启动时强制关闭宽松签名，避免历史 NVS 状态影响交易审查。 |
| LVGL 图标缺字导致竖框/方块 | 已补常用 LVGL 符号进图标字体并重新烘焙图标资源。 |

## 出固件前必须完成的实现顺序

### P0：先保证现有主路径不撒谎

- 保持未实现功能不进入真机菜单，不跳到不相关真钱包页面。
- 保持多签无描述符时拒绝跳过核对。
- 全部助记词显示页继续显示指纹和 0-2047 序号。
- 新增中文文案后必须重新烘焙中文字体和图标字体。

### P1：补 Krux 助记词工具

- 0-2047/0-7FF/八进制导入。
- 独立 0-2047/十六进制/八进制编号备份页。
- 十六进制熵创建。
- 扑克牌熵创建。
- D20 骰子创建。
- 钢板打孔和钢板二次还原。
- TinySeed、Binary Grid、OneKey KeyTag、Stackbit 1248。

### P2：补派生和高级钱包功能

- BIP85 子助记词/密码/熵。
- 助记词 XOR。
- 第二助记词和临时会话。
- PSBT 文件签名。
- QR passphrase。

### P3：补智能卡

- USB Host CCID 枚举和供电状态。
- CCID PowerOn、ATR、GetSlotStatus、XfrBlock。
- APDU short command/response 层。
- SELECT Satochip、GET_STATUS、VERIFY PIN。
- Secure Channel。
- 导出 xpub。
- BTC PSBT 外部卡签名。
- SeedKeeper 导入/导出。

### P4：补 Web3 签名

- 复核已接入的 EVM 观察钱包连接码和地址派生。
- personalSign。
- legacy/type2 transaction RLP 编码和签名。
- TokenPocket/OKX/Bitget/Relay QR。
- EIP-712 typedData 最后做，必须有测试向量和结构化审查页。

## 当前交付判断

可以继续作为“ESP32-P4 中文硬件底包 + 旧 Kern 真钱包主路径接入版”开发。

不能现在标记为“Krux 全功能真钱包正式版”。

不能现在标记为“Satochip/SeedKeeper 智能卡钱包正式版”。

等用户明确指令前，本报告之后仍不编译、不刷机、不打包。
