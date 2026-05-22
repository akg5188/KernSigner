# Satochip 功能迁移与商业交付评审

日期：2026-05-20

本轮只做代码检查、低风险修复和交付边界收敛。未编译固件、未复制 bin、未刷机。

详细迁移矩阵、参考项目入口、真机验收清单和防踩坑记录见：

- `docs/SMARTCARD_MIGRATION_MATRIX_20260520.md`
- `docs/README_FIRST_DELIVERY.md`
- `docs/MULTI_EXPERT_REVIEW_20260520_SMARTCARD_SECOND_PASS.md`
- `docs/FLASH_PRECHECK.md`
- `docs/RELEASE_POINTERS_AND_HISTORY.md`
- `docs/REAL_DEVICE_ACCEPTANCE_CHECKLIST.md`
- `docs/SMARTCARD_CAPABILITY_BOUNDARY.md`
- `docs/SMARTCARD_REAL_DEVICE_ACCEPTANCE.md`
- `docs/SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md`
- `docs/SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md`
- `docs/UNOPENED_FEATURES.md`
- `docs/COMMERCIAL_RELEASE_GATE.md`
- `docs/TROUBLESHOOTING_SMARTCARD_POWER_OTG.md`
- `docs/REFERENCE_PROJECT_MAPPING.md`

## 结论

当前 Kern 已经具备 Satochip 智能卡的可测主线：外接供电 CCID 读卡器检测、Satochip 状态读取、Web3 观察钱包连接码、常见 EVM 请求签名、按路径查看 EVM/BTC 地址、BTC xpub/ypub/zpub/tpub/upub/vpub 观察公钥读取。

当前仍不能标记为商业真钱包生产版。阻断项包括：Secure Channel 认证链未完整产品化、EVM 交易详情没有本机可读解析、生产安全配置未开启、SeedKeeper/写卡/改 PIN/重置/BTC 卡签名等高风险功能尚未迁移或验收。

## 已迁移到 Kern 的能力

- `设备检查 -> 智能卡检测`：USB Host CCID、ATR、Satochip/SeedKeeper AID 识别、只读状态 APDU。
- `连接钱包 -> Web3钱包 -> 钱包名 -> 智能卡账户`：OKX、Bitget、MetaMask、Rabby、TokenPocket、Keystone 连接码。
- `连接钱包 -> 比特币钱包 -> 智能卡账户`：BlueWallet zpub/xpub 只读观察码。
- `扫码签名 -> 智能卡 -> 扫码 Web3`：识别 Web3 请求二维码，输入 Satochip PIN 后返回 `eth-signature` 二维码。
- Satochip EVM 默认路径：`m/44'/60'/0'/0/0`。
- `扫码签名 -> 智能卡 -> 路径地址`：按路径查看 EVM 地址和 BTC 地址。
- BTC 观察公钥：`xpub`、`ypub`、`zpub`、`tpub`、`upub`、`vpub`。
- TypedData/EIP-712 当前明确拒签，不静默降级。
- 2FA 卡当前明确拒绝，不做半成品入口。

## 未迁移或暂隐藏的能力

- Satochip BTC PSBT 签名。
- Satochip BTC 消息签名。
- SeedKeeper 列表、导入、导出、保存二次加密助记词、从卡加载秘密。
- 写入助记词到 Satochip 或 SeedKeeper。
- 更改 Satochip/SeedKeeper PIN。
- 重置 Satochip/SeedKeeper。
- 启用或管理 Satochip 2FA。
- Satocash / Satodime 整组管理。
- 卡片 genuine/certificate 完整校验。
- 未初始化卡的 setup/import seed 流程。
- 完整 EIP-712 TypedData 签名。

这些功能在 `/home/ak/123/satochip-signer` 里存在，但在 ESP32-P4 上都属于高风险迁移，不能做假入口，也不能在没有真机验收和安全审查时暴露给用户。

## 本轮已修复

- 修复 CCID APDU 超时后的迟到回调风险：超时后清空响应指针，迟到响应不再写入调用者已经返回的缓冲区。
- 关闭默认开机后台智能卡探测：避免锁屏前自动启动 USB Host 和输出完整读卡报告。
- 修正 Satochip PIN 输入清理：不再提前把 textarea 置空，让统一输入组件先覆写当前文本再销毁。
- 降低 Satochip 签名任务启动日志等级，减少默认串口信息量。
- 更新状态页和文档：明确 Satochip Web3 已可测，写卡、改 PIN、重置、SeedKeeper、BTC 卡签名和 TypedData 仍隐藏。
- 收紧 Satochip BTC 地址路径校验：按路径看 BTC 地址时必须输入完整地址路径，例如 `m/84'/0'/0'/0/0`，避免账户路径被误当成收款地址。
- 优化智能卡菜单：签名入口只保留扫码 Web3、连接钱包、路径地址、观察公钥、读卡状态；BTC 六种观察公钥收进二级菜单，并强制单列显示。
- 修复 Web3 签名结果二维码兼容性：`eth-signature` 输出统一转大写，降低 OKX/Bitget 扫码失败概率。
- 修复 Web3 query `data=` 解析：只读取当前参数，不再把后续 `&` 参数拼进待签名数据。
- 替换 UR bytes 分支的 `strndup()`，减少不同 newlib/模拟器环境下的兼容风险。
- 修复 Satochip/Web3 异步任务删除竞态：worker 线程用本地记录决定 `vTaskDeleteWithCaps()`，避免 UI poll 把标志清零后用错删除 API。
- 降低连接码、签名路径、卡片工具日志到 DEBUG，并隐藏二维码 payload 预览，减少串口隐私暴露。
- 连接码二维码轮播间隔从 900ms 调整为 1200ms，提高手机扫码稳定性。
- 相机复用路径增加 `CAMERA_EVENT_DELETE` 清理和流恢复保护，降低连续扫码后黑屏概率。
- `libwally-core` 依赖缺失改为配置阶段 fail closed，不再生成缺失钱包密码学组件的半成品固件。
- Satochip APDU 成功响应、secure channel payload 长度、authentikey 解析等默认 INFO 日志降为 DEBUG，降低串口隐私和协议细节暴露。
- 补齐 `README_FIRST_DELIVERY.md`、`FLASH_PRECHECK.md`、`REAL_DEVICE_ACCEPTANCE_CHECKLIST.md`、`UNOPENED_FEATURES.md`，把刷机、真机验收和未开放红线做成独立入口。
- 连接钱包入口重排为 `Web3钱包/比特币钱包/自定义路径`，比特币再选择 `已加载助记词/智能卡账户`，避免把 EVM 钱包、BTC 观察码和任意路径混在同一层。
- 智能卡 BTC 连接钱包只开放 BlueWallet zpub/xpub 观察码；ypub/tpub/upub/vpub 继续放在 `扫码签名 -> 智能卡 -> 观察公钥` 下作为高级只读检查。
- 相机关闭超时现在返回 `ESP_ERR_TIMEOUT`，让扫码页走保守清理路径，降低连续扫码时错误释放资源的风险。
- 扫码页返回按钮在 Web3 智能卡签名任务运行中会提示等待，不再直接导航导致后台任务操作旧 UI。

## 商业交付阻断项

- 生产配置仍未达标：Secure Boot、Flash Encryption、NVS 加密、调试口/控制台/GDB 关闭和 eFuse 流程必须完成。
- Web3 交易签名前本机只展示钱包、类型、链、路径、地址和摘要，未解析收款方、金额、手续费、合约方法和 chainId，真实资金场景仍有盲签风险。
- Secure Channel 需要进一步确认 authentikey 绑定、响应完整性校验和错误路径，不能只按“能签名”判断安全。
- 智能卡 PIN 和任务状态仍需要真机异常路径回归：取消、超时、拔卡、读卡器掉电、二次签名、相机恢复、连续签名。
- SeedKeeper 和写卡类功能未进入设计验收，商业版必须继续隐藏。

## 交付建议

- 当前版本定位：Satochip Web3 测试资金验收版。
- 下一步优先级：连续签名相机恢复、EVM 请求可读解析、Secure Channel 认证审计、生产安全配置。
- 用户确认前不要出固件；确认后再编译、复制 bin 和刷机。
