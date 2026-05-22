# Satochip / SeedKeeper 智能卡迁移踩坑记录

日期：2026-05-22

本记录整理智能卡迁移现状和踩坑边界，不代表商业交付完成，不代表生产安全验收完成。2026-05-22 已确认外接供电后 Satochip/SeedKeeper 真机主线能跑通，后续重点是持续回归和生产安全收敛。

## 核心结论

- Satochip 和 SeedKeeper 是两个 applet，不是同一个功能的两个名字。
- Satochip 主要按钱包卡使用：读状态、读公开地址/公钥、Web3 连接码、EVM 摘要签名，以及后续可能的 BTC 卡签名、维护、证书、2FA 等。
- SeedKeeper 主要按秘密管理 applet 使用：状态、空间、列表、生成/导入/导出/删除 secret、密码/描述符/助记词保存等。它不是“把助记词写进 Satochip”的别名。
- SeedKeeper 管理、写卡、改 PIN、重置已经进入测试卡验收范围，但不能写成已审计商业生产能力。Satochip BTC PSBT/消息签名仍未作为生产能力开放。
- ACR39U-NF Pocketmate II 在 Waveshare ESP32-P4 上的关键坑是供电和 USB Host 方向：没有外接供电或 OTG 方向错误时，软件层会表现得像驱动/APDU 问题，但根因可能只是读卡器没有稳定上电。

## ACR39U 供电坑

- ACR39U-NF 是 CCID 读卡器，ESP32-P4 必须走 USB Host/OTG 侧接入，不能插到 USB-to-UART/下载口。
- OTG 侧如果没有稳定 VBUS，常见现象是没有 USB 设备、只看到 Hub、CCID ready 但拿不到 ATR、卡片上电失败或 APDU 超时。
- 现场优先使用带供电的 OTG 转接线，或把带电 Hub 放在 OTG 转接线后面。
- 排障顺序先看硬件链路：OTG 方向、外接供电、读卡器是否在 Hub 下游、卡片是否插紧，再看 CCID/APDU 代码。
- 只有至少达到 `state=ATR_OK`，最好达到 `state=PASS`，才继续测试钱包级智能卡功能。

## SeedKeeper 重置坑

- 新版 SeedKeeper 不使用旧 `B0 FF` 恢复出厂流程。
- 旧流程返回 `9C20` 时，不要判断为读卡器坏或驱动坏。
- 正确流程是故意用错误 PIN 锁 PIN，再故意用错误 PUK 锁 PUK。
- 返回 `FF00` 才代表恢复成空白卡。
- Kern 菜单对应 `SeedKeeper -> 重置 -> 错PIN一步 / 错PUK一步`。
- 如果返回 `9000`，说明输入值可能是正确 PIN/PUK，重置会中止，应该换一个确定错误的值。

## 当前菜单结构

以下记录当前 UI/功能目录结构，用于同步迁移边界。菜单存在不等于真机已完整验收，更不等于可用于生产资金。

### 检测入口

- `设备检测 -> 智能卡检测`
- 目标：USB Host CCID、ATR、Satochip/SeedKeeper AID 识别、只读状态 APDU。
- 边界：检测页不写卡、不改 PIN、不重置。

### 扫码签名入口

- `扫码签名 -> OKX/Bitget/MetaMask/Rabby/TokenPocket -> 智能卡`
- `扫码签名 -> BTC -> 智能卡`
- 签名扫码入口保持在首页，不在智能卡工具里重复放扫码签名。

### 智能卡工具入口

- `智能卡 -> Satochip -> 信息`
- `智能卡 -> Satochip -> 地址`
- `智能卡 -> Satochip -> BTC公钥 -> xpub/ypub/zpub/tpub/upub/vpub`
- `智能卡 -> Satochip -> 维护`
- `智能卡 -> Satochip -> 高级`
- `智能卡 -> Satochip -> 证书`
- `智能卡 -> SeedKeeper`

### 连接钱包入口

- `连接钱包 -> Web3钱包 -> OKX/Bitget/MetaMask/Rabby/TokenPocket/地址 -> 智能卡`
- `连接钱包 -> 比特币钱包 -> 智能卡 -> zpub/xpub`

### Satochip 子菜单

- `Satochip -> 信息`
- `Satochip -> 地址`
- `Satochip -> BTC公钥`
- `Satochip -> 维护 -> 改PIN/解锁/改标签/NFC/功能/重置/出厂/真伪`
- `Satochip -> 高级 -> 认证钥/2FA/会话`
- `Satochip -> 高级 -> 认证钥 -> 导主钥/写NDEF/写信任钥/导信任钥`
- `Satochip -> 高级 -> 2FA -> 设2FA/清2FA`
- `Satochip -> 高级 -> 会话 -> 登出`
- `Satochip -> 证书 -> 导出/导入`

### SeedKeeper 子菜单

- `SeedKeeper -> 状态`
- `SeedKeeper -> 剩余空间`
- `SeedKeeper -> 列表`
- `SeedKeeper -> 创建`
- `SeedKeeper -> 写入`
- `SeedKeeper -> 查看`
- `SeedKeeper -> 导入`
- `SeedKeeper -> 存密码`
- `SeedKeeper -> 读描述符`
- `SeedKeeper -> 存描述符`
- `SeedKeeper -> 克隆`
- `SeedKeeper -> 改PIN`
- `SeedKeeper -> 重置`
- `SeedKeeper -> 高级 -> 主种子/2FA/派生密码/重置条目`

## 已迁移或已进入可测主线

- CCID 探测链路：USB 枚举、CCID 接口、ATR、Satochip/SeedKeeper AID 识别。
- Satochip 只读状态读取。
- SeedKeeper 状态、设置 PIN、改 PIN、写入助记词、查看/导入条目和重置已进入真卡验收范围。
- Satochip Web3 连接码。
- Satochip 常见 EVM 请求摘要签名，默认 EVM 路径为 `m/44'/60'/0'/0/0`。
- Satochip 按路径查看 EVM/BTC 地址。
- Satochip BTC 观察公钥读取：`xpub`、`ypub`、`zpub`、`tpub`、`upub`、`vpub`。
- Web3 TypedData/EIP-712 当前应拒签或保持隐藏，不能静默当普通摘要签。
- 2FA 卡、未初始化卡、复杂维护路径不能按“菜单存在”直接放开。

## 待真机验证或继续收敛

- ACR39U 外接供电下多轮热插拔：读卡器插拔、卡片插拔、Hub 路径、离线供电后读回日志。
- Satochip Web3 连续签名：扫码、PIN、签名、返回二维码、取消、超时、拔卡、读卡器掉电、二次签名后相机恢复。
- Satochip 地址和观察公钥：主网/测试网、EVM/BTC 路径、账户路径误填、地址路径完整性。
- SeedKeeper 状态、剩余空间、列表、写入/查看/导入/删除、保存密码、保存描述符已经进入测试卡验收范围，但仍需要更多失败恢复设计和 UI 风险提示。
- Satochip 维护类：改 PIN、解锁、改标签、NFC 策略、功能策略、重置种子、出厂、真伪检查、证书导入导出、authentikey/trusted key、2FA、登出。
- Satochip BTC PSBT 签名和 BTC 消息签名仍需独立实现、测试向量和真机验收。
- Secure Channel、authentikey 绑定、响应完整性、错误路径、日志脱敏仍需安全复查。

## 明确不能宣称

- 不能宣称商业交付已完成。
- 不能宣称生产资金版、已审计量产版或真钱包安全最终版。
- 不能宣称 SeedKeeper 管理、写卡、改 PIN、重置已经完成生产审计。
- 不能宣称 Satochip BTC PSBT 签名、BTC 消息签名已完成。
- 不能把菜单截图、UI_READY、函数声明或模拟器页面等同于真机安全验收。
- 不能为了展示完整菜单而暴露会锁卡、清卡、写卡、泄露秘密材料或改变卡生命周期的半成品入口。

## 后续建议顺序

1. 先固定 ACR39U 外接供电和 CCID/ATR 稳定性。
2. 再回归 Satochip 只读能力和 Web3 测试资金签名。
3. 再做 Satochip 地址、公钥、状态、错误路径和日志脱敏验收。
4. 再回归 SeedKeeper 列表、容量、写入、导入、删除、密码、描述符和重置。
5. 最后才评估这些智能卡维护功能能否进入生产资金版，以及 Satochip BTC 卡签名和证书/2FA/高级维护。

这个顺序是为了避免再踩同一个坑：菜单和协议函数写了很多，但读卡器供电、ATR、APDU 稳定性或安全边界还没有先过。
