# Kern/Krux 真钱包功能版交付状态与验收清单

> 2026-05-22 智能卡迁移更新：外接供电后 ACR39U-NF + Satochip/SeedKeeper 已跑通。Satochip 已可用于 Web3 连接码、OKX/Bitget 测试转账签名、路径地址和 BTC 观察公钥；SeedKeeper 已可测设置 PIN、改 PIN、写入助记词、查看/导入条目和重置。新版 SeedKeeper 重置不走旧 `B0 FF`，必须用 `错PIN一步` 和 `错PUK一步` 到 `FF00`。当前仍是测试资金验收版，不是已审计量产资金版。

本文档面向 Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 真机交付验收，记录当前 Kern/Krux 真钱包功能版的可交付范围、真机验收步骤和下一阶段建议。

交付前先看：

- `docs/README_FIRST_DELIVERY.md`
- `docs/FLASH_PRECHECK.md`
- `docs/REAL_DEVICE_ACCEPTANCE_CHECKLIST.md`
- `docs/SMARTCARD_CAPABILITY_BOUNDARY.md`
- `docs/SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md`
- `docs/SMARTCARD_REAL_DEVICE_ACCEPTANCE.md`
- `docs/SMARTCARD_TEST_VECTORS_AND_EVIDENCE.md`
- `docs/SMARTCARD_HIDDEN_FEATURES_ACCEPTANCE.md`
- `docs/UNOPENED_FEATURES.md`
- `docs/COMMERCIAL_RELEASE_GATE.md`

## 交付结论

当前版本可作为 Kern/Krux `0.0.7-rc1` 的真钱包功能版继续验收。它保留本地 Krux/Amigo 风格中文 UI、触摸交互、屏幕亮度、相机预览、二维码分类识别、文本生成二维码、存储卡读写测试/只读浏览、关于页与基础设备测试，同时已经把旧 Kern 钱包核心和真钱包入口编译进 ESP32-P4 真机固件。

2026-05-19 商业钱包安全加固：

- PSBT 签名底层已强制调用输入归属分类，所有输入必须 `owned && verified`，并且网络 coin type 匹配，否则 `psbt_sign()` 直接拒签。
- 新增 PSBT 攻击测试：正确 keypath 但 UTXO 脚本被替换、未知派生路径都必须拒签。
- BTC 消息签名只允许当前网络的 BIP84 路径，例如 `m/84'/0'/...` 或测试网 `m/84'/1'/...`；EVM `60'`、legacy `44'`、taproot `86'` 暂不允许走当前消息签名实现。
- 备份导出直达入口现在也必须经过 PIN 和敏感数据确认，不能绕过备份菜单保护。
- 助记词确认页、相机生成助记词缓存、通用文本输入销毁路径已补安全擦除；密码短语输入默认隐藏。
- 相机/二维码初始化中的 abort 型 `ESP_ERROR_CHECK` 已改为错误返回，减少硬件异常导致蓝屏重启。
- 新增并加严 `tools/kern_delivery.sh prodcheck` / `tools/kern_production_check.sh`，商业真钱包生产发布前会检查 Secure Boot、Flash Encryption、NVS 加密、蓝牙/WiFi 关闭、USB Serial/JTAG 关闭、GDB stub 关闭、UART/USB 控制台关闭、panic 静默重启、WDT panic 和发布工作区干净等硬条件。
- Waveshare 4.3 寸显示底包已收紧启动失败路径：显示、触摸、LVGL 适配层初始化失败时不再走未初始化句柄、`assert` 或直接 abort，而是记录日志并由主程序可控重启。

2026-05-22 智能卡同步说明：用户确认外接供电后 ACR39U-NF 读卡器可识别，Satochip 卡已可生成 Web3 连接码并完成 OKX/Bitget Web3 转账签名。SeedKeeper 已修正为新版重置流程：旧 `B0 FF` 返回 `9C20` 时不是驱动坏，而是应使用错 PIN/错 PUK 流程直到 `FF00`。当前智能卡菜单包含 Satochip 和 SeedKeeper 两类：Satochip 侧用于连接、签名、地址、公钥、PIN/维护；SeedKeeper 侧用于设置 PIN、改 PIN、保存和查看秘密、导入本机、重置。

交付包中的 `kern.bin` 是 app 分区升级固件，不是空白板完整 factory 刷机包。正常升级请使用本文的 `appflash` 命令，不要全擦后只刷 `kern.bin`。

交付包中的 `PROJECT_PROGRESS_AND_PLAN.md` 是当前总控计划，记录已经完成的硬件底包、中文 UI、低风险工具、旧 Kern 钱包核心接入、自动验收，以及后续 USB CCID、真实钱包真机验收和生产审计阶段。

`ACCEPTANCE_REPORT.txt` 的 `FINAL: PASS` 代表模拟器页面、首屏截图、可滚动页面底部截图、关键页面、中文缺字、UI 烟测、返回/回首页/关键按钮点击验收通过。模拟器环境会把旧 Kern 钱包入口映射到对应的 Krux Shell 页面；真机固件中的同一入口会进入旧 Kern 真实钱包页面。真实钱包创建、导入、备份或签名流程仍需按本文“真机验收步骤”逐项点测。

当前版本已经不是空壳或演示页：旧 Kern 钱包核心单测通过，真机固件已有加载助记词、创建助记词、钱包首页、扩展公钥、地址、备份和扫码签名入口，并新增智能卡读卡器检测、Satochip 连接/签名/公钥读取和 SeedKeeper 管理基础。但在主网真实资金使用前，仍必须完成真机全流程复核、生产安全审计、`prodcheck` 通过和回归测试。Satochip PSBT/BTC 消息签名仍未作为生产能力开放。

## 当前可交付功能

- 中文 UI：首页与加载助记词、创建助记词、连接钱包、设置、设备检查等主菜单已具备中文显示与基础导航；连接钱包第一层为 `Web3钱包 / 比特币钱包 / 自定义路径`。
- 钱包入口：真机固件已接入旧 Kern 钱包页面，可从“加载助记词”或“创建助记词”进入测试助记词流程，再进入钱包首页、扩展公钥、地址、备份和扫码签名流程。
- 钱包核心测试：`make -C main/core/test run` 已覆盖派生、白名单、注册表、PSBT 分类等核心逻辑并通过。
- 触摸交互：支持 4.3 寸触摸屏菜单点击、返回和页面切换。
- 相机预览：可进入相机相关页面查看实时预览，用于验证摄像头链路、画面方向和曝光可用性。
- 二维码分类识别：工具页支持普通文本、URL、公开收款 URI 的显示；SeedQR、PSBT 和消息签名请从“加载助记词”或“扫码签名”真钱包入口进入真实流程。
- 文本生成二维码：支持输入普通公开文本或网址生成二维码，不保存输入内容，不接入钱包流程。
- 存储卡测试：支持存储卡读写测试，以及只读文件浏览路径验收；浏览页会排序显示根目录文件，统计目录数量、总大小，并过滤不安全条目。
- 亮度设置：支持屏幕亮度调整，并可通过真机肉眼确认亮度变化。
- 相机参数设置：提供相机相关参数设置入口，用于联调预览效果。
- 助记词导入恢复：当前已接入 `手动单词`、`编号导入`、`钢板数字恢复`、`点阵和1248导入`、`点阵板恢复`、`1248恢复`。
- 助记词核对与备份：当前已接入 `BIP39 自检`、`BIP39序号`、`原始熵`、`钢板打孔`、`点阵板`、`1248打孔`、`二维码备份`、`加密备份`。
- 地址与连接：当前已接入 `自定义路径`，可查看 BTC Legacy/Nested/Native/Taproot 与 EVM 地址，并显示二维码。
- 智能卡检测与 Satochip/SeedKeeper：当前已接入 `设备检查 -> 智能卡检测`，可检测 ACR39U-NF 等 CCID 读卡器、读取 ATR、识别 Satochip/SeedKeeper，并显示状态 APDU 结果；已接入 Satochip Web3 连接/签名、按路径查看地址、`连接钱包 -> 比特币钱包 -> 智能卡账户` 下 BlueWallet zpub/xpub，`扫码签名 -> 智能卡 -> 观察公钥` 下 BTC xpub/ypub/zpub/tpub/upub/vpub 读取；SeedKeeper 已接入设置 PIN、改 PIN、写入助记词、查看/导入卡内条目、保存密码/描述符和新版重置流程。
- 设备检查/交付验收页：集中展示固件信息、硬件快照和可用范围，并提供扫码、二维码、存储、触摸、亮度和钱包验收入口。
- 自动验收：模拟器生成 `manifest.tsv`、`glyph_check.tsv`、`smoke_check.tsv`、`scroll_check.tsv`、`interaction_check.tsv`、首屏/底部截图、关键截图 PNG、全页面拼图和 `ACCEPTANCE_REPORT.txt`。
- 未接专项：BIP85 的密码/原始熵、打印机、Satochip PSBT/BTC 消息签名等未放进本轮生产可交付范围，不冒充已完成。智能卡维护和写卡功能只按测试卡验收能力说明。
- 开发者交付脚本：`tools/kern_delivery.sh` 提供常用检查、刷写和串口观察命令。
- 生产发布检查：`tools/kern_delivery.sh prodcheck` 会阻止未启用 Secure Boot、Flash Encryption、NVS 加密、仍开启调试/控制台通道或工作区未提交的固件被标记为商业资金版。

## 真实资金使用边界

以下结论必须分清：当前已经不是“纯假界面”，但也还不能承诺“直接放真钱”。主网真实资金使用前必须补齐这些验收：

- 真机创建助记词、导入助记词、恢复、备份、擦除和重启后会话清理。
- 扩展公钥、地址派生、地址二维码和描述符显示与独立工具交叉校验。
- PSBT 解析、审核、找零判断、手续费显示、拒签、签名、导出和取消路径。
- SeedQR、加密备份、KEF、助记词工具等与种子材料相关的功能要单独审查。
- Satochip PSBT/BTC 消息签名、复杂卡片生命周期批量管理和未经审计的生产资金智能卡托管。
- 任何可导致真实比特币或其他链上资产转移的操作，都必须先用测试资金跑通。
- 当前默认开发配置下 `prodcheck` 预期会失败，因为 Secure Boot、Flash Encryption、NVS 加密、调试口/控制台关闭、panic/WDT 策略和 clean release provenance 尚未全部满足；这不是功能测试失败，而是防止误发真钱生产版的安全闸门。

验收时只使用测试助记词、测试 PSBT、公开文本二维码和空白/FAT32 存储卡。看到“真钱包”入口时可以按测试流程验收，但不要导入或签署真实资产。

## 暂不放进真钱包主菜单的专项功能

- Krux 原生专项入口：还没有完全接到旧 Kern 钱包服务的 Web3、BIP85、打印机、加密工具、二级助记词和 Mnemonic XOR 不放进真钱包主菜单。
- SeedQR/文件导入类：SeedQR 和 KEF 已从旧 Kern 钱包流程进入；新增 Krux 原生实现前不另开平行入口。
- 签名类：BTC PSBT 和消息签名走旧 Kern 扫码签名入口；Web3 智能卡签名已接 Satochip，TypedData/复杂交易格式仍需持续回归。
- 智能卡类：Satochip PSBT/BTC 消息签名仍未作为生产能力开放；卡片助记词、写入 SeedKeeper、写入 Satochip、卡片 PIN 管理、重置、SeedKeeper 管理属于测试卡验收能力。
- 高风险工具：加密、二级助记词、Mnemonic XOR、BIP85 等会派生或处理真实秘密材料的路径。

## 真机验收步骤

### 1. 开机

1. 使用 Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 目标板，连接 USB 串口。
2. 刷入当前固件后上电或复位。
3. 确认屏幕亮起，进入 Kern/Krux 中文 Shell 首页或启动页。
4. 串口日志无持续重启、panic、assert 或看门狗反复复位。

### 2. 触摸

1. 在首页点击主要菜单项，例如“扫码签名”“加载助记词”“创建助记词”“连接钱包”“设置”“设备检查”。
2. 确认点击区域与视觉位置一致，无明显偏移。
3. 在二级页面执行返回操作，确认可以回到上级页面或首页。

### 3. 亮度

1. 进入设置中的显示/亮度相关页面。
2. 调整亮度档位或滑动/点击亮度控件。
3. 确认背光亮度有可见变化。
4. 返回首页后确认亮度设置未导致黑屏、花屏或触摸失效。

### 4. 相机预览

1. 进入加载相机、设备测试相机或相机设置相关页面。
2. 对准有明显纹理或文字的物体。
3. 确认预览画面可见、方向合理、刷新稳定。
4. 调整相机参数后确认页面仍可返回，不出现卡死或重启。

### 5. 二维码分类识别

1. 使用手机或电脑显示一个非敏感普通二维码，例如普通文本或 URL。
2. 进入二维码扫描或相机加载相关页面。
3. 对准二维码，确认页面显示二维码类型、长度和安全动作。
4. 普通文本、URL 或公开收款 URI 可显示公开内容；UR、BBQR、PSBT、SeedQR、二进制等高风险内容只能显示分类和拦截提示。
5. 不使用助记词、私钥、PSBT、SeedQR 或任何真实资产二维码验收。

### 6. 存储卡

1. 插入 FAT32 格式的 microSD/TF 卡。
2. 进入存储卡测试或文件管理页面。
3. 执行读写测试，确认测试文件可创建/读取/清理，或按当前 UI 提示完成存储测试。
4. 进入只读浏览路径，确认可以列出普通文件、目录数量、总大小、安全过滤数量并返回。
5. 不在卡中放置真实助记词、私钥、PSBT 或钱包备份文件。

### 7. 关于页

1. 进入“关于”页面。
2. 确认页面可正常显示中文信息、项目/固件标识或版本信息。
3. 确认页面可返回，不出现乱码、截断到不可读或 UI 卡死。

### 8. 设备检查/交付验收页

1. 在首页点击“设备检查”，进入状态总览/交付验收相关页面。
2. 确认页面显示固件版本、IDF 版本、目标板和硬件快照。
3. 依次点击扫码预览、文本二维码、触摸测试、亮度设置、存储卡读写、文件刷新。
4. 确认页面明确提示当前已接真钱包主路径，但还不是生产审计版；智能卡开放检测、Satochip Web3 连接/签名、路径地址、BTC xpub/ypub/zpub/tpub/upub/vpub，以及 SeedKeeper 测试卡维护路径。

### 9. 模拟器覆盖重点

1. 自动截图与按钮验收应覆盖 `自定义路径`、`加载助记词`、`钢板数字恢复`、`点阵和1248导入`，并单独覆盖 `点阵板恢复`、`1248恢复`。
2. `点阵和1248导入`、`点阵板恢复`、`1248恢复` 当前都已纳入自动截图范围；按钮验收对旧页面栈不切 `screen_id` 的入口允许按目标页面关键文字或 `ok_action` 判定通过。
3. 自动截图与按钮验收应覆盖 `备份导出` 下的 `BIP39序号`、`原始熵`、`点阵板`、`钢板打孔`、`1248打孔`、`二维码备份`。
4. 如果后续菜单文案继续调整，需同步更新 `simulator/src/main_sim.c` 和 `tools/kern_delivery.sh` 的按钮文案与关键截图清单。

### 10. 真钱包流程

1. 只使用测试助记词或临时新建助记词，不导入真实资产钱包。
2. 从首页进入“加载助记词”或“创建助记词”，只用测试助记词进入钱包流程。
3. 验证加载/创建流程可进入钱包首页。
4. 验证扩展公钥、地址、备份和扫码签名页面能打开、返回、取消，不崩溃、不黑屏。
5. 用独立工具交叉检查测试助记词派生出的指纹、xpub 和首个地址。
6. PSBT/消息签名只用测试数据，记录是否能完整显示交易、拒签、取消和导出。

## 开发者快速命令

以下命令均在仓库根目录执行：

```bash
cd /home/ak/123/Kern
```

### 交付前检查

```bash
tools/kern_delivery.sh check
```

用途：

- 烘焙中文字体子集。
- 构建模拟器。
- 采集当前 Krux 迁移页面截图到 `docs/screens/delivery_YYYYMMDD_HHMMSS`，覆盖 `点阵和1248导入`、`点阵板恢复`、`1248恢复`、`BIP39序号`、`原始熵`、`钢板打孔`、`自定义路径` 等关键页面。
- 生成 `manifest.tsv`、`glyph_check.tsv`、`smoke_check.tsv`、`scroll_check.tsv` 和 `interaction_check.tsv`。
- 校验首屏截图数量、底部截图、截图尺寸、空白图、关键页面、首页标题、UI 烟测和按钮导航。
- `interaction_check.tsv` 需要覆盖 `点阵和1248导入`、`点阵板恢复`、`1248恢复` 相关入口点击结果；对于旧页面栈不切换 `screen_id` 的情况，允许按目标页面关键文字或 `ok_action` 判定通过。
- 校验中文/UI 缺字数量为 0。
- 生成关键页面拼图、全部首屏拼图和全部底部拼图。
- 生成 `ACCEPTANCE_REPORT.txt`，最终结论必须为 `FINAL: PASS`。

### 真机 app-only 刷写

```bash
ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh appflash
```

用途：

- 烘焙中文字体子集。
- 构建 ESP32-P4 固件。
- 仅刷写应用分区。
- 刷写后抓取一段启动串口日志。

如设备端口不同，调整 `ESPPORT`。当前串口刷写速度建议先使用 `115200`，稳定优先。

### 串口观察

```bash
ESPPORT=/dev/ttyACM0 tools/kern_delivery.sh monitor
```

用途：

- 复位目标板。
- 抓取约 14 秒启动日志。
- 保存日志到 `docs/logs/boot_YYYYMMDD_HHMMSS.log`。
- 自动检查 panic、assert、watchdog、brownout 等失败特征。
- 自动确认屏幕、GT911 触摸和背光初始化日志。

### 交付打包

```bash
tools/kern_delivery.sh ship
```

用途：

- 烘焙中文字体子集。
- 构建模拟器并采集首屏/底部截图。
- 校验关键页面截图、UI 烟测、滚动截图和按钮导航。
- 构建最新 ESP32-P4 固件二进制。
- 生成 `_release/kern_delivery_YYYYMMDD_HHMMSS` 交付目录。
- 输出 `RELEASE_SUMMARY.txt`、`SHA256SUMS.txt`、`kern.bin`、关键截图拼图、全页面拼图和完整截图目录。
- 输出 `ACCEPTANCE_REPORT.txt`，并在 `RELEASE_SUMMARY.txt` 写入 `kern.bin SHA256`、源码 git commit 和 worktree 状态。
- 输出 `FINAL_READINESS.txt`，集中记录固件、截图、启动日志和安全边界是否满足最终交付。
- 输出 `README_FIRST.txt`、`FLASH_COMMANDS.txt`、`flash_app_linux.sh`、`flash_app_windows.ps1` 和 `RELEASE_INDEX.tsv`，让交付包打开后就能知道怎么验收、刷机和复核。
- Linux/Windows 刷机脚本会先校验 `kern.bin` SHA256，校验失败拒绝刷机。
- 同时生成 `_release/kern_delivery_YYYYMMDD_HHMMSS.tar.gz`，便于归档或传输。
- 最后自动执行最终交付校验，必须看到 `final verify: PASS`。

如果需要“刷写真机 + 抓启动日志 + 重新打包 + 最终校验”一次完成：

```bash
JOBS=2 ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh shipflash
```

### 最终交付校验

```bash
tools/kern_delivery.sh final
```

用途：

- 自动读取 `_release/LATEST_RELEASE.txt` 指向的最新交付包。
- 校验 `SHA256SUMS.txt`，确认包内文件没有被修改或损坏。
- 校验 `ACCEPTANCE_REPORT.txt` 必须是 `FINAL: PASS`，且缺字、UI 烟测、滚动、按钮导航失败数均为 0。
- 校验 `boot.log` 必须包含当前 app 版本、屏幕初始化、GT911 触摸和背光初始化信息。
- 校验 `kern.bin` 的 SHA256 与 `RELEASE_SUMMARY.txt` 一致。
- 校验完整截图索引、按钮验收表、全页面拼图和交付文档都存在。
- 校验 Linux/Windows 刷机脚本存在，确保交付包支持刷前 SHA256 校验。

## 已知风险

- USB CCID 读卡器已接入安全检测页；Satochip Web3 连接/签名、路径地址、BTC xpub/ypub/zpub/tpub/upub/vpub 已进入可测范围；SeedKeeper 设置/改 PIN、写入、查看/导入和重置已进入测试卡验收范围。Satochip PSBT/BTC 消息签名不能作为可用功能验收。
- 钱包逻辑已接入：旧 Kern 钱包核心和入口已编译进真机固件，但缺少完整真机钱包流程验收和生产安全审计，不能直接放真钱。
- 串口刷写低速：当前建议使用低速稳定刷写，后续再优化高速下载稳定性。
- 屏幕中文新增需烘焙字体：新增中文文案后必须重新执行字体烘焙，否则可能出现缺字、方块或显示异常。
- 生产审计仍需持续回归：后续开放生产资金版前，要确保测试资金、生产资金、开发者模式和智能卡实验模式边界清晰。
- 已知非阻塞日志：`GPIO 26 is not usable`、I2C pull-up 提示、flash 32MB 与镜像头 16MB 提示、单点触摸提示当前不阻塞本轮交付，但明早验收时需要确认屏幕、触摸和亮度实际可用。

## 下一阶段建议

1. 完成真机验收记录：按本文清单补充实拍照片、串口日志和验收结论。
2. 扩展 USB CCID 智能卡：在当前枚举、ATR、Satochip Web3、xpub/地址、SeedKeeper 写入/查看/重置稳定后，再评估 Satochip PSBT/BTC 消息签名和更完整的卡生命周期管理。
3. 完成真钱包流程验收：创建、导入、xpub、地址、备份、擦除、PSBT、消息签名、取消和错误路径。
4. 建立安全开关矩阵：把生产资金版、测试资金版、开发者模式和智能卡实验模式分别做成可审计配置。
5. 固化中文字体流程：新增中文文案时自动检查缺字并触发字体烘焙。
6. 优化刷写链路：在稳定性验证后提高串口波特率或补充更可靠的下载说明。
7. 增加真机硬件回归脚本：在当前模拟器按钮验收基础上，覆盖触摸、背光、相机、存储卡、关于页、真钱包流程和未接专项页面。
# 2026-05-19 17:34 集成交付记录

- 三路评测已合并：安全状态、UI/文案、缺口扫描。
- 正式 UI 已开放 `智能卡检测`、Satochip Web3 连接/签名、路径地址、`观察公钥` 下 BTC xpub/ypub/zpub/tpub/upub/vpub，并开放 SeedKeeper 测试卡设置 PIN、改 PIN、写入、查看/导入和重置。
- 临时/无校验助记词只允许助记词变换和还原，核心层禁止签名、地址派生、xpub、Web3、备份导出。
- 助记词本机 Flash 保存被存储层拒绝，保留 SD 卡加密备份路径。
- passphrase 会话在合法重载/切换路径中保留，避免静默变成无口令钱包。
- 当前首页六入口：扫码签名、加载助记词、创建助记词、连接钱包、设置、设备检查。
- 已通过 `make -C main/core/test run`、`git diff --check`、`cmake --build build_wave_43 -- -j2`。
- 已生成下载目录刷机包：`/home/ak/Downloads/kern_wave43_20260519_173451.zip`。
- 未刷机。
