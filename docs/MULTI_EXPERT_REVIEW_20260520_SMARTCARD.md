# 多专家智能卡复查记录

日期：2026-05-20

范围：对照 `/home/ak/123/satochip-signer` 复查 Kern 当前智能卡迁移状态、商业交付风险、UI 文案、Web3 二维码互通、构建稳定性和后续文档。本轮未编译固件、未复制 bin、未刷机。

## 总结

Kern 已经迁入 Satochip 智能卡 Web3 主线，但不是完整的 `satochip-signer` 移植，也不是商业真钱包生产版。

可以继续验收：

- ACR39U-NF 外接供电读卡器检测。
- CCID/APDU、ATR、Satochip/SeedKeeper AID 识别。
- Satochip 只读状态。
- OKX/Bitget/MetaMask/Rabby/TokenPocket/通用地址连接码。
- Satochip Web3 常见 EVM 请求签名。
- personalSign 基础路径。
- Satochip 路径地址。
- BTC `xpub/ypub/zpub/tpub/upub/vpub` 观察公钥。

必须继续隐藏或拒签：

- TypedData/EIP-712。
- TokenPocket 原生 `signTransaction` 完整解析。
- Satochip BTC PSBT 签名。
- Satochip BTC 消息签名。
- SeedKeeper 管理。
- 写卡、改 PIN、重置。
- 卡片真伪检查。
- 2FA 管理。

## 专家结论

### 协议迁移

- 当前是安全子集，不是全量迁移。
- `satochip-signer` 里的 SeedKeeper、写卡、卡 PIN 管理、重置、BTC 卡签名仍未迁移。
- 后续必须先做卡片真伪检查、Secure Channel 审计，再开放更多卡功能。

### 安全

- 当前不能作为商业真钱包生产版。
- 生产配置仍未开启 Secure Boot、Flash Encryption、NVS 加密。
- Web3 交易详情没有本机可读解析，真实资金仍有盲签风险。
- Secure Channel 的 authentikey 绑定和响应完整性需要继续审计。

### 产品和 UI

- 智能卡菜单应保持简短：`扫码 Web3 / 生成连接码 / 路径地址 / 观察公钥 / 读卡状态`。
- 连接钱包来源命名应区分 `已加载助记词` 和 `智能卡账户`。
- TypedData 不支持必须提前提示，不能让用户误以为能签。
- 二维码页不要显示原始 payload，减少隐私泄露和视觉拥挤。
- `连接钱包` 第一层应收敛为 `Web3钱包 / 比特币钱包 / 自定义路径`，避免把 EVM 钱包、BTC 观察码和路径工具混在一起。

### Web3 互通

- OKX/Bitget 多账户连接码方向正确。
- MetaMask/Rabby/TokenPocket HDKey 连接码方向正确。
- `eth-sign-request` 和 `request_id` 回填已接。
- 签名结果二维码应统一大写，提升 OKX/Bitget 扫码兼容性。
- TypedData 和 TokenPocket 原生交易二维码仍是缺口。

### 构建和运行稳定性

- 异步任务不能让 UI poll 线程清掉 worker 删除方式标志，否则 PSRAM stack 任务可能用错删除 API。
- `data=` query 解析必须按 `&` 截断。
- libc `strndup()` 在嵌入式/模拟器环境不稳，已替换成项目内函数。
- `libwally-core` 缺依赖时必须 fail closed，不能继续生成半成品钱包固件。

### 文档

- 需要把商业发布门禁、智能卡真机验收、能力边界、供电排障、参考项目映射写清楚。
- 文档必须明确：当前是测试资金验收版，不是生产资金版。

## 本轮已落地修复

- Web3 签名结果 `eth-signature` 二维码转大写。
- Web3 query `data=` 只取当前参数。
- UR bytes 和 BIP21 地址截取不再调用 libc `strndup()`。
- Satochip 连接/工具/Web3 签名任务修复 `vTaskDeleteWithCaps()` 竞态。
- Web3/Satochip 默认 INFO 日志脱敏降级。
- 连接码页隐藏原始 payload，只显示扫描提示。
- 动态二维码轮播从 900ms 调整到 1200ms。
- 相机复用路径增加恢复保护，降低连续扫码黑屏风险。
- 连接钱包入口已重排：`Web3钱包` 下选择 OKX/Bitget/MetaMask/Rabby/TokenPocket/通用地址，`比特币钱包` 下选择已加载助记词或智能卡账户。
- 智能卡 BTC 连接钱包只开放 BlueWallet zpub/xpub；ypub/tpub/upub/vpub 继续留在智能卡观察公钥菜单。
- 相机关闭超时返回 `ESP_ERR_TIMEOUT`，扫码页可走保守清理路径，减少错误释放资源。
- 扫码页返回按钮在 Web3 智能卡签名任务运行中会提示等待，避免后台任务继续操作旧页面。
- `libwally-core` 依赖缺失时 CMake 直接失败。
- Satochip APDU 成功响应和 secure channel payload 长度日志从 INFO 降到 DEBUG。
- 新增商业发布门禁、刷机前检查、整机真机验收、能力边界、未开放红线、供电排障、参考项目映射、智能卡测试向量和隐藏功能验收文档。

## 验证

- `git diff --check`：通过。
- 三个被改 C 文件按 `build_wave_43/compile_commands.json` 执行 `-fsyntax-only`：通过。
- `tools/kern_delivery.sh prodcheck`：失败，符合预期；失败项包括 Secure Boot、Flash Encryption、NVS 加密、生产 PIN HMAC、ETH/LWIP、USB Serial JTAG、GDB Stub、console、WDT panic 和 clean worktree。

## 交付判断

当前不是商业生产版。可以继续作为 Satochip Web3 测试资金验收版，等用户确认后再出固件。
