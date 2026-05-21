# satochip-signer 全量智能卡功能目录

日期：2026-05-20

这份目录只做一件事：

- 把 `/home/ak/123/satochip-signer` 里和智能卡相关的功能完整摊开

用途：

- 让你一眼看出参考项目到底有多少功能
- 让 Kern 的迁移缺口一目了然
- 防止以后把“能识别读卡器”误当成“完整智能卡钱包已经完成”

> 结论先说：
> `satochip-signer` 的智能卡能力远不止“连接钱包 + 签名”。
> 它至少分成 `通用卡工具 / Satochip / SeedKeeper / Javacard / 2FA / 证书 / NFC / 批量导入导出 / 维护工具` 这几层。
> Kern 当前只真正接上了其中很小一部分可测主线。

## 1. 屏幕菜单级功能

下面是按参考项目实际入口整理的菜单树。

### 1.1 通用卡工具

参考项目可见入口：

- `设备筛选`
- `卡片信息`
- `真伪检查`
- `修改标签`
- `修改 NFC 策略`

这些功能对应的是卡片通用管理能力，不是单纯的签名入口。

### 1.2 Satochip 功能

参考项目的 `Satochip` 菜单核心能力包括：

- `写入助记词`
- `导出公钥`
- `加载描述符`
- `加载 PSBT`
- `BTC PSBT 签名`
- `更改 PIN`
- `重置卡片`
- `高级功能`

#### 1.2.1 写入助记词

参考项目支持从多种来源导入或创建后写入 Satochip：

- 扫描助记词
- 输入 12/15/18/21/24 个单词助记词
- 输入 Electrum 助记词
- 输入 SLIP-39 分片
- 从 SeedKeeper 导入
- 创建助记词

#### 1.2.2 导出公钥

参考项目支持导出不同类型的观察公钥：

- `xpub`
- `ypub`
- `zpub`
- `tpub`
- `upub`
- `vpub`

并支持：

- 选择签名类型
- 选择脚本类型
- 自定义派生路径
- 选择协调器 / 钱包来源

#### 1.2.3 加载描述符

参考项目支持：

- 选择脚本类型
- 自定义派生路径
- 查看描述符详情
- 用二维码显示描述符

#### 1.2.4 加载 PSBT

参考项目支持从 `microSD` 加载 `PSBT`，进入后续签名流程。

### 1.3 Satochip 高级功能

参考项目的高级页包含：

- `启用 2FA`
- `签名性能测试`
- `消息签名性能测试`
- `检查签名偏差`

这说明参考项目不是只做“能签名”，而是还有辅助诊断和风控相关入口。

### 1.4 SeedKeeper 功能

参考项目的 `SeedKeeper` 菜单核心能力包括：

- `智能卡创建`
- `写入到 SeedKeeper`
- `保存二次加密`
- `加载二次加密`
- `更改 PIN`
- `重置卡片`
- `更多功能`

#### 1.4.1 智能卡创建

支持创建长度：

- `12` 词
- `15` 词
- `18` 词
- `21` 词
- `24` 词

#### 1.4.2 写入到 SeedKeeper

支持把当前已加载助记词写入 SeedKeeper。

#### 1.4.3 保存 / 加载二次加密

支持把二次加密后的“假助记词”保存到 SeedKeeper，再从卡里加载回来继续还原。

#### 1.4.4 更多 SeedKeeper 功能

参考项目在更深层还支持：

- 查看空闲空间
- 克隆秘密
- 查看秘密
- 导入密码
- 删除秘密
- 加载描述符
- 保存描述符

### 1.5 Javacard / DIY 工具

参考项目还提供较底层的卡管理能力：

- 构建 applet
- 安装 applet
- 卸载 applet
- 加载密钥
- 保存密钥
- 解锁卡
- 锁卡
- 清空密钥

这部分不是普通钱包主线，但属于参考项目的真实功能树。

### 1.6 维护工具

参考项目还有独立维护入口：

- `MicroSD` 刷写
- `MicroSD` 校验
- `MicroSD` 清零擦除
- `MicroSD` 随机擦除

这些不属于智能卡协议本身，但属于同一参考树里的实际工具。

## 2. 协议级能力

上面是“屏幕上能点到什么”。
下面是 `CardConnector` / `satochip_signer` 里真正存在的卡协议能力。

### 2.1 通用卡管理

参考项目至少有这些底层能力：

- `card_get_status`
- `card_set_label`
- `card_set_nfc_policy`
- `card_verify_authenticity`
- `card_export_perso_certificate`
- `card_import_perso_certificate`

### 2.2 PIN / 解锁 / 重置

参考项目支持：

- `card_verify_PIN`
- `card_change_PIN`
- `card_unblock_PIN`
- `card_reset_seed`
- `card_reset_factory_signal`

### 2.3 2FA

参考项目支持：

- `card_set_2FA_key`
- `card_reset_2FA_key`
- `card_crypt_transaction_2FA`

### 2.4 Satochip seed / key 管理

参考项目支持：

- `card_bip32_import_seed`
- `card_bip32_get_authentikey`
- `card_bip32_set_authentikey_pubkey`
- `card_bip32_get_extended_key`
- `card_bip32_get_xpub`

### 2.5 签名

参考项目支持：

- `card_sign_transaction_hash`
- `card_sign_message`

并且在 helper / Kotlin 签名器里还有：

- `BTC PSBT` 签名流水线
- `Web3 signTransaction`
- `Web3 personalSign`
- `Web3 signTypedData / EIP-712`

这说明参考项目的“签名”不只是单一 APDU，而是交易解析、签名、回传整个链路。

### 2.6 SeedKeeper 秘密管理

参考项目支持：

- `seedkeeper_get_status`
- `seedkeeper_generate_masterseed`
- `seedkeeper_generate_2FA_secret`
- `seedkeeper_generate_random_secret`
- `seedkeeper_derive_master_password`
- `seedkeeper_import_secret`
- `seedkeeper_export_secret`
- `seedkeeper_export_secret_to_satochip`
- `seedkeeper_list_secret_headers`
- `seedkeeper_reset_secret`
- `seedkeeper_print_logs`

## 2.7 CardConnector 方法级总表

下面把 `CardConnector.py` 里出现过的智能卡相关方法再摊开一层。
这份表的作用是防止只看菜单，不看底层 APDU 能力。

### 2.7.1 识别、通道、元信息

- `card_get_ATR`
- `card_get_CPLC`
- `card_get_IIN`
- `card_get_CIN`
- `card_select`
- `card_select_satochip`
- `card_select_seedkeeper`
- `card_select_satodime`
- `card_select_satocash`
- `card_get_status`
- `card_get_label`
- `card_set_label`
- `card_get_ndef`
- `card_set_ndef`
- `card_set_nfc_policy`
- `card_set_feature_policy`
- `card_setup`
- `card_reset_factory_signal`
- `card_logout_all`
- `card_initiate_secure_channel`
- `card_encrypt_secure_channel`
- `card_decrypt_secure_channel`

### 2.7.2 Satochip 密钥、派生、签名

- `satochip_import_privkey`
- `satochip_reset_privkey`
- `satochip_get_pubkey_from_keyslot`
- `card_bip32_import_seed`
- `card_import_encrypted_secret`
- `card_import_trusted_pubkey`
- `card_export_trusted_pubkey`
- `card_export_authentikey`
- `card_reset_seed`
- `card_bip32_get_authentikey`
- `card_bip32_set_authentikey_pubkey`
- `card_bip32_get_extendedkey`
- `card_bip32_get_xpub`
- `card_bip32_get_xprv`
- `card_bip32_get_liquid_master_blinding_key`
- `card_sign_message`
- `card_parse_transaction`
- `card_sign_transaction`
- `card_sign_transaction_hash`
- `card_sign_schnorr_hash`
- `card_taproot_tweak_privkey`
- `card_musig2_generate_nonce`
- `card_musig2_sign_hash`

### 2.7.3 PIN、解锁、2FA

- `card_create_PIN`
- `card_verify_PIN_deprecated`
- `card_verify_PIN_simple`
- `card_verify_PIN`
- `card_change_PIN`
- `card_unblock_PIN`
- `card_set_2FA_key`
- `card_reset_2FA_key`
- `card_crypt_transaction_2FA`

### 2.7.4 SeedKeeper 管理

- `seedkeeper_get_status`
- `seedkeeper_generate_masterseed`
- `seedkeeper_generate_2FA_secret`
- `seedkeeper_generate_random_secret`
- `seedkeeper_derive_master_password`
- `seedkeeper_import_secret`
- `seedkeeper_export_secret`
- `seedkeeper_export_secret_to_satochip`
- `seedkeeper_list_secret_headers`
- `seedkeeper_reset_secret`
- `seedkeeper_print_logs`

### 2.7.5 证书、真实性、NDEF

- `card_export_perso_pubkey`
- `card_import_perso_certificate`
- `card_export_perso_certificate`
- `card_import_ndef_authentikey`
- `card_challenge_response_pki`
- `card_verify_authenticity`

### 2.7.6 Satocash / Satodime

- `satocash_get_status`
- `satocash_import_mint`
- `satocash_export_mint`
- `satocash_remove_mint`
- `satocash_import_keyset`
- `satocash_export_keysets`
- `satocash_remove_keyset`
- `satocash_import_proof`
- `satocash_export_proofs`
- `satocash_get_proof_info`
- `satodime_set_unlock_secret`
- `satodime_set_unlock_counter`
- `satodime_increment_unlock_counter`
- `satodime_get_status`
- `satodime_get_keyslot_status`
- `satodime_set_keyslot_status_part0`
- `satodime_set_keyslot_status_part1`
- `satodime_get_pubkey`
- `satodime_get_privkey`
- `satodime_seal_key`
- `satodime_unseal_key`
- `satodime_reset_key`
- `satodime_initiate_ownership_transfer`

### 2.7.7 对 Kern 的直接含义

- Kern 目前只真正接上了少数主线方法：`smartcard_ccid_probe`、`smartcard_ccid_transmit_apdu`、`smartcard_satochip_read_status`、`smartcard_satochip_get_eth_account`、`smartcard_satochip_get_web3_account`、`smartcard_satochip_get_btc_xpub`、`smartcard_satochip_get_btc_address`、`smartcard_satochip_sign_evm_digest`。
- 上面这一整组 `card_change_PIN`、`card_unblock_PIN`、`card_reset_seed`、`card_set_nfc_policy`、`card_verify_authenticity`、所有 `seedkeeper_*`、所有 `satocash_*`、所有 `satodime_*`，在 Kern 里都还没有真正的后端实现，也没有安全验收菜单。
- 所以“菜单看到”不等于“功能迁移完成”，这些方法要么继续隐藏，要么先补后端再补 UI。

## 3. 参考项目里“秘密”到底能装什么

SeedKeeper 不是只装助记词。

参考项目能管理的秘密类型包括：

- `BIP39 mnemonic`
- `Masterseed`
- `Password`
- `Descriptor`
- `Data`
- `Public Key`
- `2FA secret`
- 二次加密助记词记录

这也是为什么 `SeedKeeper` 不能只做一个“助记词保险箱”按钮就算完。

## 4. 参考项目的卡通道

参考项目支持的典型通道/模式有：

- `USB Host CCID`
- `PC/SC`
- `APDU`
- `Secure Channel`
- `NFC` 相关策略

这意味着它不是单纯的 UI 菜单，而是“UI + 协议层 + 读卡器链路 + 安全通道”一起工作。

## 5. Kern 当前状态

Kern 现在只接上了下面这些可测主线：

- CCID 读卡器检测
- ATR 读取
- Satochip / SeedKeeper AID 识别
- Satochip 只读状态
- Satochip Web3 观察钱包连接码
- Satochip 常见 EVM 请求签名
- Satochip personalSign 基础路径
- Satochip 按路径查看 EVM / BTC 地址
- BTC 观察公钥 `xpub/ypub/zpub/tpub/upub/vpub`

Kern 当前仍然没有真正开放的参考项目能力：

- SeedKeeper 列表 / 导入 / 导出 / 删除
- 写入助记词到 Satochip / SeedKeeper
- 更改 Satochip / SeedKeeper PIN
- 重置 Satochip / SeedKeeper
- NFC 策略设置
- 2FA 管理
- Satochip BTC PSBT 签名
- Satochip BTC 消息签名
- 卡片真伪 / 证书完整校验
- 完整 SeedKeeper 密码 / 描述符 / 数据管理

## 6. Kern 下一步如果要“全部弄过来”

建议顺序仍然是：

1. 先补 `只读功能` 之外的安全审计说明
2. 再补 `SeedKeeper` 只读列表和容量查询
3. 再补 `修改标签 / 修改 NFC 策略`
4. 再补 `更改 PIN / 解锁 / 重置`
5. 再补 `写入助记词 / SeedKeeper 导入导出`
6. 最后才考虑 `BTC PSBT`、`BTC 消息签名`、`2FA` 和证书链

原因很简单：

- 这些不是“加个菜单”就能完成
- 每一步都需要真机、断电、拔卡、错误 PIN、回退和数据一致性回归
