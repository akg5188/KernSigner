# Kern Wave43 多专家复评记录

时间：2026-05-19

> 历史报告说明：本报告是 2026-05-19 的复评记录，不代表 2026-05-20 之后的智能卡/Web3 菜单、发布包指针或商业交付状态。当前请先看 `docs/README_FIRST_DELIVERY.md`、`docs/SATOCHIP_COMMERCIAL_REVIEW_20260520.md` 和 `docs/RELEASE_POINTERS_AND_HISTORY.md`。

目标：对当前 ESP32-P4 4.3 寸中文钱包固件做第二轮多领域评测。本轮以只读评测为主，不刷机，不主动改代码。

## 覆盖领域

- 安全威胁建模
- PIN、锁屏、会话保护
- 无状态钱包与敏感数据生命周期
- BIP39/BIP32/助记词工具
- 比特币 PSBT、描述符、地址和扩展公钥
- Web3/EVM 钱包连接
- QR/UR/BBQR 扫码与生成
- 相机和嵌入式稳定性
- UI/视觉设计
- 中文文案和信息架构
- 存储、KEF 加密备份、SD/Flash 边界
- 构建、刷机包和交付验收

## 当前基线

- 核心测试：`make -C main/core/test run` 通过。
- 格式检查：`git diff --check` 通过。
- ESP32-P4 固件构建：`cmake --build build_wave_43 -- -j2` 通过。
- 下载目录刷机包：`/home/ak/Downloads/kern_wave43_20260519_173451.zip`。
- 刷机包 `SHA256SUMS.txt` 校验通过。

## 初步结论

- 暂未发现会阻止生成固件或导致核心测试失败的 P0 构建问题。
- 正式入口已经隐藏智能卡/读卡器/CCID/Satochip/SeedKeeper 相关功能。
- 临时/无校验助记词的核心层门禁已加强：不能签名、派生地址、导出 xpub、Web3 或备份导出。
- 仍需真机重点压测相机/扫码进入退出、Web3 二维码识别、备份二维码全屏显示、PIN 错误保护和掉电清空。

## 已收到专家意见

### Web3 / EVM / 发布交付

P0：未发现直接可触发的 Web3/EVM 盲签或刷机包篡改绕过问题。

P1：

- Web3 菜单实际只暴露连接码/通用码，Bitget、MetaMask、Rabby、TokenPocket 虽有 profile/catalog，但默认菜单不可直接选到。
- BTC `signmessage` 可对任意 BIP32 path 派生并签名，包括潜在 `m/44'/60'/...` EVM coin-type 路径。建议限制 BTC 消息签名只允许 BTC purpose/coin-type。
- `_release` 目录存在旧 `v0.0.7` zip，和当前 `0.0.7-rc1` 推荐刷机包口径不一致，容易刷错旧包。
- 旧完整 zip 只有 bin，缺 `SHA256SUMS`、签名、README、Windows/Linux 刷机脚本。
- 工作区为 dirty，不能作为生产资金版的可复现发布状态。

P2：

- EVM 地址派生方向正确：`m/44'/60'/0'/0/0`、uncompressed pubkey、Keccak-256、EIP-55 checksum。
- OKX/Bitget/MetaMask/Rabby/TokenPocket 缺少 golden vector、CBOR/UR 解码断言和手机钱包实扫记录。
- Web3 签名请求当前只识别并提示“暂不盲签”，不进入 EVM 签名，安全边界正确。

建议：

- 发布前统一 `_release` 和 Downloads 包口径，归档旧包，生成包含校验/脚本/说明的新完整包。
- Web3 菜单明确列出 OKX、Bitget、MetaMask、Rabby、TokenPocket，未实测则标记实验。
- BTC 消息签名增加路径策略，拒绝 EVM coin-type。
- 给 EVM 连接 QR 增加 golden tests 和手机实扫验收记录。

### UI / 中文文案 / 信息架构

P0：未发现 UI 阻断项。截图批次 77 张均为 480x800，缺字为 0；主 Shell 首页 2x3、黑底白字橙色边框方向达标。

P1：

- 解锁后的钱包首页仍是 7 项，不符合 2x3 主入口心智。建议改为 `扫码签名 / 连接钱包 / 地址核对 / 公钥导出 / 备份 / 设置`，`清除会话` 移到安全设置。
- 连接钱包分类混合链、导出物和工具。建议改为 `比特币连接 / 以太坊连接 / 公钥导出 / 地址核对 / 自定义路径`。
- Web3/二维码页残留英文和测试语义：`Keystone 二维码`、`crypto-multi-accounts 模拟连接码`、raw `UR:.../TEST`、默认文本 `Kern离线钱包-480x800`。
- 生成二维码页在 480x800 上拥挤，建议默认只显示二维码、状态、输入入口和生成按钮，键盘仅编辑态出现。
- 点阵/1248 术语不统一，应统一为 `点阵板` 与 `1248 打孔板`。
- 点阵板备份横向过满，建议点阵格降到 34-36px 或增加安全边距。
- 系统检测/防篡改页暴露模拟器和开发信息，建议默认只显示固件版本、设备型号、安全状态，详细信息放开发信息。

P2：

- 部分页面顶部和底部都有返回按钮，建议减少重复。
- 顶部指纹未加载状态建议显示 `未加载钱包`，加载后显示 `指纹 ABCD1234`。
- 主菜单命名统一为 `设备检查`，避免 `固件自检`/`设备检查` 混用。
- 详情页色彩建议统一黑白橙，绿/红/青只用于状态和警告。

### 安全威胁建模 / PIN / 无状态钱包

P0：

- 生产级固件完整性未在默认构建中强制启用。默认配置未看到 Secure Boot / Flash Encryption / NVS encryption 强制项，分区表里的 `nvs` 和 `storage` 未标加密。若生产发布没有另行烧 eFuse 和启用 secure boot/flash encryption，物理攻击者可刷改版固件绕过 PIN 或诱导用户导入助记词。

P1：

- PIN 设置流程简化，首次输入后直接保存，缺少二次确认、split-PIN、eFuse HMAC provision 和反钓鱼词展示；HMAC 不可用时退回固定 salt。生产模式应 fail-closed。
- Krux shell 备份直达目标可绕过备份菜单里的二次 PIN 和危险确认。`backup_seed_words / backup_entropy / backup_seed_qr / backup_kef` 直达页目前只校验有效助记词。
- 导入助记词确认页释放明文时使用普通 `free` 宏，未统一 `secure_memzero`。
- KEF 密码、BIP39 passphrase、LVGL textarea 生命周期未统一安全擦除；passphrase 输入未默认 password mode。
- 三次 PIN 错误当前是清 RAM 后关机/重启保护，不是 wipe/永久锁定。产品文案和威胁模型必须明确。

P2：

- 会话内最多保留 5 份明文助记词，扩大 RAM 泄露面。建议默认只保留当前钱包，多助记词 slots 改显式 opt-in。
- SD 删除只是 unlink，不等于安全擦除；KEF 文件名/ID 可能泄露关联信息。
- 反钓鱼状态页可能误导：硬件状态显示了，但 UI 未实际启用反钓鱼交互。

### 钱包算法 / BIP39 / PSBT / 描述符

P0：

- PSBT 签名前没有强制使用已实现的输入所有权分类。UI 确认后直接调用 `psbt_sign()`；`psbt_sign()` 没有要求每个输入都经过 `psbt_classify_input()` 并满足 `owned && verified`，也没有在签名前强制比对 UTXO script/redeem/witness script。这是真钱包签名安全阻断项。

P1：

- PSBT 网络检测是 bool，unknown 默认主网，且只看每个输入/输出第一个 keypath；unknown/mixed 应拒签。
- Taproot 分类和签名能力不一致。分类已处理 taproot leaf paths，但签名主要处理普通 input keypaths，BIP86/Taproot PSBT 应明确支持或明确拒签。
- 新建/扫码导入助记词时，确认页展示指纹用空 passphrase，实际加载可能沿用当前 session passphrase，用户看到的身份可能不是最终钱包身份。
- 通用 descriptor 加载对 zpub/vpub/ypub/upub 兼容不足，等价 SLIP132 descriptor 可能失败。
- 助记词 slots 以空 passphrase 指纹去重/展示，加载时又使用默认 network/policy/account，可能静默切换签名身份。

P2：

- 牌熵入口需要明确重复牌检测和测试向量。
- D6/D20/牌熵算法需要公开 golden vectors，防止用户无法复现或后续算法漂移。
- 核心路径 parser 只接受小写 `h` 和 `'`，建议核心也接受 `H` 或明确 API 约束。
- xpub/zpub 导出支持范围有限，BIP49 ypub/upub、BIP86 导出需要补全或 UI 标注。
- Compact SeedQR 只支持 12/24 词，导入 UI 需要明示格式，避免普通 16/32 字符串被误识别。

建议测试向量：

- `PSBT-UTXO-MISMATCH`：keypath 正确但 UTXO script 换成非预期脚本，期望拒签。
- `PSBT-NETWORK-UNKNOWN`：无 derivation path 的 PSBT，期望 unknown 并拒签。
- `PSBT-NETWORK-MIXED`：主网/测试网 keypath 混合，期望 mixed 并拒签。
- `PSBT-BIP86`：Taproot PSBT 要么正确 Schnorr 签名，要么明确显示不支持。
- `PASSPHRASE-FP`：确认页指纹必须等于最终加载后的指纹。
- `DESCRIPTOR-SLIP132`：等价 zpub descriptor 应 normalize 后通过，或明确不支持。
- `CARD-DUP`：重复牌必须拒绝。

### QR / 相机 / 嵌入式稳定性主线扫描

该专家组未在等待窗口内返回；以下为主线扫描结论。

P1：

- `qr/scanner.c`、`capture_entropy.c` 仍有 `ESP_ERROR_CHECK()` 用在相机回调注册和 buffer 设置等路径，相机驱动异常时可能直接 abort/蓝屏。
- 相机/QR 页面涉及任务、队列、PPA、显示 buffer 和 LVGL 对象交错释放，需要真机压测“进入扫码、快速返回、再进入、连续扫错码”。
- 多处 `lv_refr_now(NULL)` 在回调里直接刷新，若与页面销毁/相机帧操作交错，有 UI 崩溃风险。

P2：

- PSBT 审查、QR 分片、descriptor UR、加密备份路径有较多大内存分配，需要低内存压测。
- Web3 签名请求目前安全地只识别不签名，但错误二维码和超长 payload 需要真机压测。

## 总体结论

当前代码可以作为“刷机测试/验收版”继续真机验证，但不能称为“生产真钱包正式版”。生产版至少需要先解决以下阻断项：

1. 强制生产级固件完整性：Secure Boot、Flash Encryption、NVS/敏感分区加密、release/CI fail-closed 校验。
2. PSBT 签名前强制输入所有权分类：所有输入必须 `owned && verified`，unknown/mismatch/mixed network 默认拒签。
3. 修复备份直达二次 PIN/危险确认绕过，所有秘密导出入口统一风险门禁。
4. 修复 PIN 设置和 eFuse/HMAC/反钓鱼流程，生产模式不可 fallback 到公开固定 salt。
5. 统一敏感输入和助记词释放的 secure wipe。

## 建议优先修复顺序

1. P0：PSBT 签名前硬门禁和网络 tri-state。
2. P0：生产构建安全配置和发布包口径。
3. P1：备份直达二次 PIN、PIN 设置/eFuse、敏感内存擦除。
4. P1：passphrase 指纹显示一致、slots 身份、BTC signmessage path policy。
5. P1：钱包首页 2x3、连接钱包分类、Web3 菜单、二维码页减负。
6. P2：entropy golden tests、点阵板布局、发布签名、手机钱包实扫记录。
