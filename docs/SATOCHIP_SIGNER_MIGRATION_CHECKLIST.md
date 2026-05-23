# satochip-signer 功能迁移检查清单

日期：2026-05-20

如果你要先看“参考项目到底有哪些智能卡能力”，请先看：

- [satochip-signer 全量智能卡功能目录](SATOCHIP_SIGNER_FULL_FUNCTION_CATALOG_20260520.md)

> 状态说明：这是迁移对照清单，不是生产放行报告。当前最新菜单和发布边界以 `docs/README_FIRST_DELIVERY.md`、`docs/SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md`、`docs/SMARTCARD_CAPABILITY_BOUNDARY.md` 和 `docs/RELEASE_POINTERS_AND_HISTORY.md` 为准。只要没有重新出包并更新验收截图，本文件中旧截图、旧入口或历史计划都不能当作当前可刷固件状态。

本清单只对照：

- `/home/ak/123/satochip-signer`
- `/home/ak/123/Kern`

目标不是写概念说明，而是回答 4 个实际问题：

1. 哪些功能已经和 `satochip-signer` 基本对齐
2. 哪些功能底层已有一部分，但入口、交互或范围还不对
3. 哪些功能还完全没迁
4. 后续应该按什么顺序补

---

## 1. 当前总判断

`Kern` 现在已经不是空壳。

它已经有：

- 中文主菜单
- 扫码签名主路径
- 助记词创建/导入/备份主路径
- BTC / Web3 观察账户连接主路径
- PIN / 锁屏 / 自动关机 / 自检 / 相机 / 存储卡这些硬件底包能力

但它也还不是 `satochip-signer` 的完整移植版。

最关键还没补完的是：

- `SeedKeeper`
- `Satochip PSBT/BTC 消息签名`
- `写卡 / 改 PIN / 重置`
- `更完整的导入恢复链排版和导航`

2026-05-22 更新：

- ACR39U-NF 外接供电后已经识别，Satochip Web3 连接码和 OKX/Bitget Web3 签名已经跑通。
- Kern 已新增 Satochip 按路径查看地址、BTC xpub/ypub/zpub/tpub/upub/vpub 读取流程。
- SeedKeeper 设置 PIN、改 PIN、写入助记词、查看/导入条目和新版重置流程已经进入测试卡验收范围。
- 写卡、改 PIN、重置、SeedKeeper 管理仍按高风险测试卡功能处理，不能宣传为已审计生产能力。

所以现在最准确的定位是：

- `Kern = Krux 风格中文 UI + 旧 Kern 真钱包主路径 + ESP32-P4 硬件底包`
- 还不是 `satochip-signer` 全功能正式版

---

## 2. 已基本对齐

这些功能已经能和 `satochip-signer` 的树莓派固件主线大致对齐，至少不是假入口。

### 首页结构

`satochip-signer` 主页主线：

- 扫码签名
- 助记词工具
- 智能卡工具
- 固件自检

`Kern` 现在主页：

- 扫码签名
- 连接钱包
- 助记词工具
- 设置
- 固件自检

结论：

- 主体方向已经对齐
- 只是 `Kern` 额外把 `连接钱包` 和 `设置` 提到一级菜单
- `智能卡工具` 已开放 Satochip 可用主线；写卡、改 PIN、重置、SeedKeeper 和 BTC 卡片签名仍隐藏

### 扫码签名主路径

`Kern` 已接真实入口：

- 扫码签名
- 交易二维码解析
- 交易审查
- 已签名二维码导出
- 消息签名主路径

对应代码：

- [sign_psbt_qr](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:340)
- [sign_psbt_review](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:358)
- [sign_psbt_export](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:367)
- [sign_message](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:376)

结论：

- `BlueWallet BTC PSBT` 这类本机助记词 BTC 扫码签名主线，已经有基础
- `TokenPocket`/OKX/Bitget 一类智能卡 Web3 扫码签名主线已跑通常见转账，复杂格式继续回归

### 助记词创建

`Kern` 现在已经接上的创建类入口：

- 扑克牌创建
- 16进制创建
- 骰子创建
- D20 骰子
- 手动单词
- 拍照创建

对应条目：

- [new_cards_entropy](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:156)
- [new_hex_entropy](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:165)
- [new_dice_d6](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:183)
- [new_dice_d20](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:192)
- [new_words_select](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:174)
- [new_camera_entropy](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:147)

结论：

- “创建助记词”这条树干已经搭起来了
- 但是其中部分分支交互还没做到 `satochip-signer` 那么完整顺手

### 助记词导入主路径

已经接上：

- 手动单词
- 编号导入
- 扫码导入
- 存储卡文件导入
- 加密备份导入
- 钢板数字恢复
- 点阵和1248导入
- TinySeed恢复
- 1248恢复

对应条目：

- [load_manual](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:92)
- [load_digits](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:101)
- [load_camera](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:74)
- [load_sd](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:83)
- [load_encrypted_kef](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:128)

结论：

- 标准 `BIP39` 导入主线已经可用
- 钢板数字恢复、TinySeed恢复、1248恢复都已经有真实入口
- `SeedKeeper` 导入等智能卡来源还没有

### 连接钱包

`Kern` 已经有：

- `Web3钱包`
- `BTC钱包`
- `OKX钱包`
- `Bitget`
- `MetaMask`
- `Rabby`
- `TokenPocket`
- `EVM地址`
- `BlueWallet zpub`
- `BlueWallet xpub`

对应条目：

- [pi_connect_wallet](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:229)
- [btc_wallet](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:303)
- [web3](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:715)

结论：

- 观察账户连接这条线已经基本成型
- 当前更偏“导出连接码”，还不是完整签名链路

### 自检与硬件底包

已经接上：

- 系统检测
- 防篡改检查
- 设备检测
- 相机
- 触摸
- 存储卡
- 亮度

对应条目：

- [pi_self_check](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:238)
- [device_tests](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:852)
- [system_overview](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:917)
- [security_check](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:926)

结论：

- 这块已经比 `satochip-signer` 更像“硬件适配版底包”

---

## 3. 有底层或半成品，但还没完全对齐

这些功能不能算“没有”，但也不能算“迁移完成”。

### 助记词加密 / 还原

现在状态：

- 已有真实算法入口
- 支持手动输入每个词对应的 `+/-` 数字
- 适合做二次加密或还原

对应条目：

- [tools_secondary_mnemonic](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:623)

还差：

- 和 `satochip-signer` 一样清楚的“已加载助记词管理”流程整合
- 更顺手的“直接使用当前助记词”交互
- 和钢板恢复路线联动

### BIP85

现在状态：

- `12/18/24` 词子助记词已经有

对应条目：

- [bip85_mnemonic](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:532)

还差：

- 派生密码
- 派生原始熵
- 写入 `SeedKeeper`
- 和 `satochip-signer` 文档里的完整分仓流程对齐

### 助记词备份

现在状态：

- 文字备份
- 二维码备份
- 加密备份
- 查看 BIP39 序号
- 查看原始熵
- 点阵图备份
- 钢板位权
- TinySeed
- 1248 打孔板

对应条目：

- [backup_seed_words](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:395)
- [backup_entropy](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:404)
- [backup_steel_punch](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:422)
- [backup_stackbit](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:431)
- [backup_tinyseed](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:440)
- [backup_seed_qr](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:440)
- [backup_kef](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:449)

还差：

- 备份页版式和视觉还可继续优化
- 和 `satochip-signer` 一样细的人工核对辅助信息还可继续补

### Web3 钱包连接

现在状态：

- `OKX / Bitget / MetaMask / Rabby / TokenPocket / EVM地址` 都有页面
- 能出连接二维码

对应条目：

- [web3_okx](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:724)
- [web3_bitget](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:733)
- [web3_metamask](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:742)
- [web3_rabby](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:751)
- [web3_tokenpocket](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:760)
- [web3_address](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:769)

还差：

- Web3 请求扫码
- `personal_sign`
- `typed data`
- 交易签名结果回传

### 地址工具

现在状态：

- 收款地址
- 找零地址
- 扫码核对地址
- 地址二维码
- 自定义派生路径查看地址

对应条目：

- [addresses](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:477)
- [custom_derivation](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:330)

还差：

- `satochip-signer` 风格更完整的 `Address Explorer` 交互
- 智能卡来源地址查看

---

## 4. 已开始迁移但钱包级功能仍隐藏

这些是现在和 `satochip-signer` 差距最大的地方。Kern 已开放 `智能卡检测`，可做 CCID、ATR、Satochip/SeedKeeper AID 和只读状态检测；下面这些钱包级功能仍未开放。

### 智能卡工具整条线

`satochip-signer` 里有：

- `智能卡创建`
- `Satochip 功能`
- `SeedKeeper 功能`
- `完整智能卡菜单`
- `写入当前助记词到 SeedKeeper`
- `保存二次加密助记词到 SeedKeeper`
- `从 SeedKeeper 加载二次加密助记词`
- `更改 SeedKeeper PIN`
- `重置 SeedKeeper`

`Kern` 当前状态：

- 读卡器检测、ATR、Satochip/SeedKeeper 识别、只读状态已接入
- 写卡、改 PIN、重置、签名、卡内助记词导入导出、Satocash、Satodime 仍必须隐藏
- 更完整的方法级清单见 [SATOCHIP_SIGNER_FULL_FUNCTION_CATALOG_20260520.md](SATOCHIP_SIGNER_FULL_FUNCTION_CATALOG_20260520.md) 的 2.7 节

对应条目：

- [smartcard_probe](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c)
- [smartcard_tools](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:797)
- [smartcard_reader](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:806)
- [satochip_xpub](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:815)
- [satochip_psbt](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:824)
- [satochip_web3](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:833)
- [seedkeeper_tools](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:842)

### 导入恢复高级链

当前状态：

- `load_punch_grid` 已接真实入口
- `TinySeed恢复` 已是独立恢复流程
- `1248恢复` 已是独立恢复流程
- `钢板数字恢复` 已能先还原出假助记词，再继续进入确认加载链

还差：

- 更像 `satochip-signer` 的完整导入排版和提示
- 更完整的“假助记词 -> 二次还原 -> 真助记词”导航和串联

### Web3 完整签名

当前状态：

- [web3_message_sign](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:778)
- [web3_typed_data](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:787)

说明：Satochip Web3 常见转账签名已经跑通；personal_sign、TypedData 和多钱包特殊格式还需要样本回归。

### 文件签名交易

当前没完成：

- [sign_psbt_file](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:349)

### 维护工具 / 加密工具 / 打印机

当前没完成：

- [tools_flash_tools](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:596)
- [tools_encryption](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:605)
- [settings_printer](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c:705)

---

## 5. 功能存在但当前入口不够像 satochip-signer

这些功能不是没有，而是入口组织方式和 `satochip-signer` 还不一致。

### 首页

`satochip-signer` 首页更像：

- 扫码签名
- 助记词工具
- 智能卡工具
- 固件自检

`Kern` 当前：

- 把 `连接钱包` 和 `设置` 也提到一级

对应代码：

- [KRUX_HOME_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:86)

### 助记词工具

当前菜单：

- 创建助记词
- 导入助记词
- 已加载助记词
- 密码短语
- BIP85
- 助记词加密
- 备份助记词

对应代码：

- [KRUX_PI_MNEMONIC_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:117)

还差 `satochip-signer` 里常见的：

- 更像 `satochip-signer` 的统一编排和入口顺序

现在已经有：

- `BIP39 自检`
- `查看 BIP39 序号`
- `查看原始熵`
- `从钢板数字恢复二次助记词`
- `TinySeed恢复`
- `1248恢复`

### 连接钱包

当前菜单：

- Web3钱包
- BTC钱包
- 选择助记词
- 扩展公钥
- 地址核对

对应代码：

- [KRUX_PI_CONNECT_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:127)

还差：

- 智能卡来源选择
- 树莓派固件里更接近 `satochip-signer` 的“来源选择页”

---

## 6. 后续优先开发顺序

如果目标是尽快接近 `satochip-signer` 可交付版，建议按这个顺序补：

### P0：先补“助记词工具闭环”

优先级最高：

1. `TinySeed / 1248` 继续优化版式和输入体验
2. 更完整的钢板恢复链
3. “已加载助记词”继续对齐原项目操作中心
4. `BIP85 / 助记词加密` 和已加载助记词链进一步打通

原因：

- 这几项仍然不依赖智能卡硬件
- 能继续把助记词体系补到更完整

### P1：补备份体系

1. 备份页版式继续优化
2. 钢板打孔人工核对信息继续补细
3. TinySeed 点阵备份继续优化
4. Stackbit 1248 继续优化

原因：

- 这是 `satochip-signer` 很核心的差异化能力

### P2：回归 Web3 完整签名

1. Web3 请求扫码
2. `personal_sign`
3. `typed data`
4. 交易签名回传

原因：

- 常见转账已经完成“连接 + 签名”，还要补齐消息签名、TypedData 和更多钱包样本回归

### P3：补智能卡剩余高风险功能

1. SeedKeeper
2. Satochip BTC PSBT 签名
3. Satochip BTC 消息签名
4. 写卡 / 改 PIN / 重置
5. 错误码、2FA、不供电和二次扫码回归

原因：

- USB Host CCID、APDU、Secure Channel 和 Satochip Web3 主线已接，剩下的是风险最高、最不能做假入口的一块

---

## 7. 当前可作为推进依据的代码位置

可见菜单入口：

- [KRUX_HOME_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:86)
- [KRUX_PI_MNEMONIC_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:117)
- [KRUX_PI_CONNECT_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:127)
- [KRUX_WEB3_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:135)
- [KRUX_SETTINGS_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:186)
- [KRUX_PI_SELF_CHECK_MENU](/home/ak/123/Kern/main/pages/signer_shell/signer_shell.c:144)

功能目录总表：

- [signer_feature_catalog.c](/home/ak/123/Kern/main/signer_port/signer_feature_catalog.c)

参考文档来自外部 `satochip-signer` 项目：

- `docs/离线签名器一页备忘.zh-CN.md`
- `docs/树莓派菜单逐项说明.zh-CN.md`
- `docs/固件页面功能总览.zh-CN.md`

---

## 8. 一句话结论

如果按 `satochip-signer` 的树莓派固件来衡量：

- `Kern` 现在已经有真钱包主线和 Satochip Web3 主线
- 但“助记词核对链 + 钢板/点阵 + SeedKeeper/Satochip 高风险管理 + Web3 复杂格式回归”这四大块还没迁完整

所以现在最合适的推进方式不是继续乱补页面，而是：

- 先把已开放的 Satochip Web3 / xpub / 地址真机回归稳住
- 再补 Web3 消息签名和 TypedData 样本
- 最后再做 SeedKeeper、写卡、改 PIN、重置这些高风险功能
