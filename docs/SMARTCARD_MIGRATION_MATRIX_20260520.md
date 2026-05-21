# 智能卡迁移矩阵与防踩坑记录

日期：2026-05-20

本文件是后续继续迁移 `/home/ak/123/satochip-signer` 时的对照清单。目的不是宣传已经完成，而是防止以后把“能识别读卡器”误当成“完整智能卡钱包已经商业交付”。

本轮没有编译固件、没有复制 bin、没有刷机。

## 当前结论

Kern 当前只适合作为 Satochip Web3 测试资金验收版，不是商业真钱包生产版。

已可测主线：

- ACR39U-NF 外接供电读卡器检测。
- CCID、ATR、Satochip/SeedKeeper AID 识别。
- Satochip 只读状态。
- Satochip Web3 连接码。
- Satochip 常见 EVM 请求签名。
- Satochip personalSign 基础路径。
- Satochip 按路径查看 EVM/BTC 地址。
- Satochip BTC xpub/ypub/zpub/tpub/upub/vpub 观察公钥。

必须隐藏或拒绝：

- SeedKeeper 列表、导入、导出、删除、写入。
- 写入助记词到 Satochip 或 SeedKeeper。
- 更改 Satochip/SeedKeeper PIN。
- 重置 Satochip/SeedKeeper。
- Satochip BTC PSBT 签名。
- Satochip BTC 消息签名。
- TypedData/EIP-712。
- 2FA 卡签名。
- 卡片真伪证书校验还没接入前，不能宣传为供应链安全已完成。

## 参考项目入口

`satochip-signer` 关键文件：

- `/home/ak/123/satochip-signer/seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/views/tp_views.py`
- `/home/ak/123/satochip-signer/seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/views/tools_views.py`
- `/home/ak/123/satochip-signer/seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/helpers/seedkeeper_utils.py`
- `/home/ak/123/satochip-signer/seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/helpers/satochip_signer.py`
- `/home/ak/123/satochip-signer/pi-signer-py/vendor/pysatochip/CardConnector.py`

Kern 当前关键文件：

- `/home/ak/123/Kern/main/smartcard/smartcard_ccid.c`
- `/home/ak/123/Kern/main/smartcard/smartcard_satochip.c`
- `/home/ak/123/Kern/main/smartcard/smartcard_satochip.h`
- `/home/ak/123/Kern/main/pages/scan/scan.c`
- `/home/ak/123/Kern/main/pages/krux_shell/krux_shell.c`
- `/home/ak/123/Kern/main/core/evm.c`
- `/home/ak/123/Kern/main/krux_port/krux_feature_catalog.c`
- `/home/ak/123/Kern/main/qr/scanner.c`

## 功能迁移状态

| 功能 | satochip-signer 参考 | Kern 当前状态 | 交付判断 |
|---|---|---|---|
| CCID 读卡器枚举 | CardConnector / PCSC | 已接 USB Host CCID | 可测 |
| ATR / AID 识别 | CardConnector select | 已接 Satochip/SeedKeeper 检测 | 可测 |
| Satochip GET_STATUS | `card_get_status` | 已接只读状态 | 可测 |
| Secure Channel | `card_initiate_secure_channel` | 已接基础通道 | 需安全审计 |
| Satochip PIN verify | `card_verify_PIN` | 已接连接/签名前 PIN | 可测，但需异常回归 |
| Web3 连接码 | `ToolsTpWeb3ConnectRunView` | 已接 OKX/Bitget/MetaMask/Rabby/TokenPocket/通用地址 | 可测 |
| Web3 EVM 签名 | `_satochip_sign_evm_digest` | 已接 eth-sign-request/eth-signature | 测试资金可测 |
| personalSign | `_sign_web3_eth_request_with_satochip` | 已接基础 digest | 需样本回归 |
| TypedData | `signTypedData` 路径 | 当前拒签 | 正确隐藏 |
| BTC xpub/zpub | `ToolsTpBtcXpubRunView` | 已接 xpub/ypub/zpub/tpub/upub/vpub | 可测 |
| 按路径看地址 | `ToolsTpSmartcardAddressRunView` | 已接 EVM/BTC 地址，BTC 要完整地址路径 | 可测 |
| BTC PSBT 卡签名 | `sign_psbt_with_satochip` | 未接 | P0 缺口 |
| BTC 消息卡签名 | `sign_message_with_satochip` | 未接 | P1 缺口 |
| SeedKeeper 列表/导出/导入 | `seedkeeper_list_secret_headers` / `seedkeeper_export_secret` / `seedkeeper_import_secret` | 未接 | P0/P1 缺口，继续隐藏 |
| 写入助记词到卡 | `card_bip32_import_seed` | 未接 | 高风险，继续隐藏 |
| 改 PIN | `card_change_PIN` | 未接 | 高风险，需单独设计 |
| 重置卡 | `card_unblock_PIN` / factory reset APDU | 未接 | 极高风险，继续隐藏 |
| 2FA | `card_set_2FA_key` 等 | 当前拒绝 2FA 卡 | 正确保护 |
| 真伪检查 | `card_verify_authenticity` | 未接 | 商业阻断项之一 |

## 当前 UI 入口

智能卡相关入口应该保持这个结构：

```text
连接钱包
- Web3钱包
  - OKX
  - Bitget
  - MetaMask
  - Rabby
  - TokenPocket
  - 通用地址
- 比特币钱包
  - 已加载助记词
    - BlueWallet zpub
    - BlueWallet xpub
  - 智能卡账户
    - BlueWallet zpub
    - BlueWallet xpub
- 自定义路径

扫码签名
- 本机助记词
- 智能卡

智能卡
- 扫码 Web3
- 生成连接码
- 路径地址
- 观察公钥
- 读卡状态

观察公钥
- 主网 zpub
- 主网 ypub
- 主网 xpub
- 测试 vpub
- 测试 upub
- 测试 tpub
```

不要把写卡、SeedKeeper 管理、改 PIN、重置、BTC PSBT 卡签名直接放到普通用户菜单。没有完整安全实现和真机验收时，宁可隐藏，也不要放说明页冒充功能。

## 商业交付阻断项

当前不能标记为商业生产钱包，至少因为：

- Secure Boot 未启用。
- Flash Encryption 未启用。
- NVS 加密未启用。
- UART/USB Serial JTAG/GDB/console 调试面仍未按生产关闭。
- `prodcheck` 当前会失败。
- EVM 交易签名前没有本机解析收款方、金额、手续费、合约方法和 chainId。
- Secure Channel 认证链和响应完整性还要继续审计。
- 真机连续签名、拔卡、掉电、超时、取消、二次扫码、相机恢复还没形成完整回归报告。
- SeedKeeper 和写卡类高风险功能没有实现。
- 卡片真伪检查未迁移。

## 开发顺序

建议顺序：

1. 先稳定已接入主线：读卡器供电、连接码、Web3 签名、路径地址、观察公钥。
2. 补真机回归：连续签名后相机恢复、错误 PIN、拔卡、Hub 掉电、读卡器热插拔。
3. 做 Web3 可读解析：交易金额、收款方、手续费、chainId、合约方法，无法解析则拒签。
4. 做 Secure Channel 安全审计：authentikey 绑定、响应完整性、错误路径。
5. 做卡片真伪检查。
6. 做 Satochip BTC PSBT 卡签名。
7. 做 Satochip BTC 消息签名。
8. 做 SeedKeeper 只读列表和导出。
9. 最后才考虑写卡、改 PIN、重置和 SeedKeeper 写入。

## 真机验收最低清单

每次出固件前至少确认：

- 开机 PIN 正常，三次错误保护正常。
- 不插读卡器时智能卡页能给出明确错误。
- 外接供电读卡器能稳定识别。
- Satochip 状态读取成功。
- OKX 连接码可被手机钱包识别。
- Bitget 连接码可被手机钱包识别。
- MetaMask/Rabby/TokenPocket 至少不误导，不支持时明确报错。
- OKX/Bitget 测试资金 EVM 转账签名成功。
- personalSign 样本成功或明确记录不支持。
- TypedData 必须拒签。
- 签名一次后再次扫码，相机能恢复。
- PIN 输入页能返回，取消后不保留 PIN。
- 拔卡、断 Hub、电源不足、超时都有中文错误。
- BTC zpub/xpub/ypub/tpub/upub/vpub 可显示和扫码。
- BTC 地址路径必须是完整地址路径，例如 `m/84'/0'/0'/0/0`。

## 不要再踩的坑

- 不要直插读卡器就判断软件不行；ACR39U-NF 在这块板上需要外接供电链路。
- 不要把 `设备检查 -> 智能卡检测` 当成钱包签名可用证明。
- 不要把看到 ATR 当成 Satochip applet 可用。
- 不要把 Satochip Web3 签名成功当成 BTC PSBT 卡签名已完成。
- 不要把识别 SeedKeeper AID 当成 SeedKeeper 管理已完成。
- 不要把观察公钥导出当成私钥或签名能力。
- 不要在未解析 EVM 交易详情时宣传真实资金安全签名。
- 不要在 `prodcheck` 失败时标记商业生产版。
- 不要为赶进度暴露写卡、改 PIN、重置入口。
- 不要把二维码 payload 默认打到 INFO 日志；地址、路径和交易请求都属于隐私。
- 不要用 C 库 `strndup()` 处理 UR bytes，嵌入式/模拟器环境兼容性不稳定。
- 不要在 Web3 query 中把 `data=` 后面的全部字符串当数据；必须按 `&` 截断参数。
- 不要让 UI poll 线程修改 worker 线程删除方式标志；PSRAM stack 任务必须用正确的 `vTaskDeleteWithCaps()`。
- 不要只测第一次扫码；每次出固件都要连续扫码两次以上，验证相机释放和恢复。
