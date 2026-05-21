# 多角色项目评测报告

日期：2026-05-19

> 历史报告说明：本报告反映 2026-05-19 当时的菜单和风险状态。2026-05-20 之后智能卡、连接钱包和发布边界已调整；当前入口和交付判断以 `docs/README_FIRST_DELIVERY.md`、`docs/SATOCHIP_COMMERCIAL_REVIEW_20260520.md` 和 `docs/RELEASE_POINTERS_AND_HISTORY.md` 为准。

评测角色：

- UI / 美工 / 交互
- 钱包安全
- 嵌入式 C / LVGL / 编程质量
- 中文文案 / 信息架构
- 产品完整性汇总

本报告只做评测和修复排序，不表示当前版本已经达到正式资金生产级交付。

## 总结

当前固件已经能编译出包，主菜单、助记词、连接钱包、扫码签名、备份、设置等主线入口也已经接入。但从多角色评测看，当前版本还不应该直接宣称为“正式钱包最终版”。

最需要先处理的不是美观问题，而是三个高风险方向：

1. QR 解析存在蓝屏/越界读风险，和之前 Web3/扫码后蓝屏现象高度相关。
2. 中间助记词能力降级不完整，仍可能走到备份/导出/二维码等不该开放的路径。
3. UI 和文案还混有大量测试、开发、验收说明，正式钱包感不够，菜单名也不统一。

建议先修 P0，再做 UI / 文案统一，然后再重新编译出包和真机回归。

## P0 必须先修

### QR 解析可能越界读和蓝屏

文件：

- `/home/ak/123/Kern/main/qr/parser.c`
- `/home/ak/123/Kern/main/qr/scanner.c`

问题：

- `qr_parser_parse()` 直接 `strlen(data)`。
- `qr_parser_parse_with_len()` 虽然接收长度，但内部仍大量按 C 字符串处理。
- 扫描器传入的 QR payload 不保证 NUL 结尾，可能越界读、格式误判、蓝屏闪退。
- `scanner.c` 调用处使用显式长度，风险真实存在。

修复方向：

- `qr_parser_parse_with_len()` 先检查 `parser/data/data_len`。
- 把 payload 复制到带 NUL 结尾的临时 buffer 后再走旧字符串解析，或全面改成长度安全解析。
- 对空内容、内嵌 `\0`、超长内容做测试。

### QR 重复片段更新有 use-after-free 风险

文件：

- `/home/ak/123/Kern/main/qr/parser.c`

问题：

- `add_part()` 更新重复片段时先 `free(old_data)`，再 `malloc(new_data)`。
- 如果新分配失败，结构体里会留下悬空指针。
- 后续 destroy/result 可能 double-free 或 use-after-free。

修复方向：

- 先分配新 buffer。
- 成功后再替换旧指针。
- 失败时保留旧片段不变。

### 中间助记词仍可进入备份/导出路径

文件：

- `/home/ak/123/Kern/main/core/key.c`
- `/home/ak/123/Kern/main/pages/home/backup/mnemonic_words.c`
- `/home/ak/123/Kern/main/pages/home/backup/mnemonic_qr.c`
- `/home/ak/123/Kern/main/pages/home/backup/mnemonic_grid.c`
- `/home/ak/123/Kern/main/pages/home/backup/mnemonic_steel.c`
- `/home/ak/123/Kern/main/pages/home/backup/mnemonic_1248.c`
- `/home/ak/123/Kern/main/pages/krux_shell/krux_shell.c`

问题：

- 当前中间/假助记词可加载用于助记词加减恢复。
- 但部分备份页面只检查 `key_is_loaded()`，没有检查 `key_mnemonic_is_valid()`。
- 这会让中间助记词继续显示序号、明文 QR、SeedQR、点阵、钢板、1248 等。

修复方向：

- 建立统一能力判断：
  - `key_has_signing_key()`：可签名、可派生地址、可导出 xpub。
  - `key_mnemonic_is_valid()`：可备份、可显示原始熵、可生成加密备份。
  - `key_is_loaded() && !key_mnemonic_is_valid()`：只能进入“助记词变换/还原”。
- 所有备份、地址、xpub、Web3、BIP85、密码短语入口都前置能力检查。

### Web3 / 地址 / xpub 对中间助记词门控不够明确

文件：

- `/home/ak/123/Kern/main/pages/krux_shell/krux_shell.c`
- `/home/ak/123/Kern/main/core/evm.c`
- `/home/ak/123/Kern/main/core/key.c`

问题：

- 部分 UI 入口只判断 `key_is_loaded()`。
- 核心层可能会失败返回，但用户看到的是“生成失败”，不是明确禁止。
- 后续改动容易误把中间助记词放行。

修复方向：

- Web3 连接码、EVM 地址、BTC 地址、xpub/zpub、自定义派生路径统一要求 `key_has_signing_key()`。
- 中间助记词提示统一：“临时助记词不能用于签名或派生地址。”

## P1 高优先级

### Passphrase 会话可能被静默丢失

文件：

- `/home/ak/123/Kern/main/core/key.c`
- `/home/ak/123/Kern/main/pages/krux_shell/krux_shell.c`
- `/home/ak/123/Kern/main/core/descriptor_validator.c`

问题：

- 应用 BIP39 密码短语后，`stored_mnemonic` 仍保存原始助记词。
- 后续网络/账户/描述符等流程如果重新加载 mnemonic 且 passphrase 为 NULL，可能静默切回无密码短语钱包。

修复方向：

- 保存“当前会话已应用 passphrase”的状态。
- 禁止在 passphrase 会话里静默重载。
- 重载前必须明确保留 passphrase 或提示用户重新输入。

### 无状态钱包策略还不够彻底

文件：

- `/home/ak/123/Kern/main/core/storage.*`
- `/home/ak/123/Kern/main/pages/store_mnemonic.c`
- `/home/ak/123/Kern/main/pages/load_mnemonic/load_storage.c`

问题：

- 当前仍支持把加密助记词保存到 flash 或 SD。
- 如果目标是严格无状态，设备内部 flash 不应保存任何可恢复钱包材料。

修复方向：

- 发布版禁用 `STORAGE_FLASH` 助记词保存。
- SD 卡导出必须明确用户确认。
- 设置/说明里明确：关机清 RAM，设备本机不保存助记词。

### PMOFN / 多片 QR 校验不严

文件：

- `/home/ak/123/Kern/main/qr/parser.c`

问题：

- PMOFN 用 `atoi()`，没有严格验证 index/total 范围。
- 完整性判断用“索引求和”，错误组合可能被误判完整。

修复方向：

- index/total 严格限制到 `1..RELAY_MAX_PARTS`。
- `index <= total`。
- 完整性检查改成逐个 expected index 查找。

### Taproot 自定义路径地址可能生成错误

文件：

- `/home/ak/123/Kern/main/core/custom_derivation.c`

问题：

- P2TR 地址生成疑似把 scriptPubKey 传给 segwit address API。
- 需要确认 libwally API 需要 witness program 还是完整 scriptPubKey。

修复方向：

- 加 BIP86 测试向量。
- 对比 Sparrow / Electrum / bitcoinjs-lib 输出。

### 未加载钱包时助记词工具页面生命周期不干净

文件：

- `/home/ak/123/Kern/main/pages/shared/mnemonic_tool_page.c`

问题：

- 页面对象创建后才发现钱包未加载，然后直接弹错返回。
- 多次进入可能留下空页面和静态状态。

修复方向：

- 权限检查前置到创建 LVGL 对象前。
- 失败路径必须 destroy/cleanup。

## P2 UI / 交互问题

### 首页没有真正铺满一屏

文件：

- `/home/ak/123/Kern/main/pages/krux_shell/krux_shell.c`

问题：

- 首页 6 项两列布局，单卡高度约 228px。
- 3 行加状态栏和间距后会滚动，不符合“首页铺满不要浪费屏幕空间”。

修复方向：

- 首页改为 2x3 网格。
- 480x800 下卡片高度建议 150-165px。
- 首页只保留 6 个正式入口：
  - 扫码签名
  - 连接钱包
  - 助记词
  - 备份
  - 设置
  - 设备检查

### 返回按钮层级和重复返回

文件：

- `/home/ak/123/Kern/main/ui/menu.c`
- `/home/ak/123/Kern/main/pages/krux_shell/krux_shell.c`

问题：

- 通用菜单返回按钮挂在 parent 上，不随菜单容器隐藏/销毁。
- 部分页面顶部有返回，底部又有返回/回首页，占空间且混乱。

修复方向：

- 返回按钮挂到菜单容器或统一 chrome 容器。
- 所有页面统一左上返回。
- 长页面底部最多保留“回首页”，不要重复“返回”。

### 输入框和键盘不适合 4.3 寸触摸

文件：

- `/home/ak/123/Kern/main/ui/input_helpers.c`
- `/home/ak/123/Kern/main/ui/keyboard.c`
- `/home/ak/123/Kern/main/pages/load_mnemonic/manual_input.c`
- `/home/ak/123/Kern/main/pages/new_mnemonic/dice_rolls.c`

问题：

- 通用输入框高度固定 50px，偏小。
- 助记词键盘第三行 9 个键，单键约 40px，误触风险高。
- 用户要求输入框大一点、支持空格、光标移动、删除。

修复方向：

- 所有熵输入页统一大输入框，90-120px 高。
- 数字/骰子/十六进制/序号只显示必要按键。
- 增加左移、右移、删除、空格。

### QR 备份页顶部控件拥挤

文件：

- `/home/ak/123/Kern/main/pages/home/backup/mnemonic_qr.c`

问题：

- 返回、类型下拉、网格、全屏按钮挤在同一行。
- 480px 宽容易遮挡。

修复方向：

- 顶部只保留返回 + 类型。
- 全屏 / 低密度 / 网格放到底部大按钮。
- QR 本体建议白底黑码，提高打印/扫码成功率。

## P2 文案 / 信息架构问题

### 正式钱包混入开发/测试/验收文案

文件：

- `/home/ak/123/Kern/main/pages/krux_shell/krux_shell.c`

问题：

- “集中检查固件、硬件和钱包入口”
- “生成安全测试二维码”
- “保存到 NVS”
- “Kern 钱包核心已经接到 Krux 菜单”
- “建议顺序”
- “来源文件 / 功能编号”

这些文字会让用户觉得是测试固件，不是正式钱包。

修复方向：

- 正式页面只写用户动作和结果。
- 开发说明移到 docs 或隐藏调试模式。

### 菜单命名不统一

建议统一：

| 当前混用 | 统一命名 |
|---|---|
| 新助记词 / 创建助记词 / 创建钱包 | 创建助记词 |
| 导入 / 加载 / 载入 | 导入 |
| 手动输入 | 输入单词 |
| 序号 | 输入序号 |
| 二维码 | 扫描二维码 |
| 原始熵 | 随机数 |
| 16进制创建 / Hex | 十六进制 |
| D20 骰子 | 20面骰 |
| 骰子创建 | 骰子 |
| 扑克牌创建 | 扑克牌 |
| BIP85 子助记词 | 派生助记词 |
| 助记词 XOR | 异或助记词 |
| 助记词加密 | 变换助记词 |
| 修改PIN / PIN 设置 | PIN 码 |
| 固件自检 / 系统检测 / 设备检测 | 设备检查 |

### 风险提示太多，真正危险提示被稀释

原则：

- 普通扫码、普通二维码工具，不反复提示“不读取钱包数据”。
- 高风险动作必须提示：
  - 显示助记词
  - 显示二维码备份
  - 导出加密备份
  - 签名交易
  - 清除会话
  - 修改 PIN
  - 写入硬件安全设置

### 对话框按钮需要动作化

文件：

- `/home/ak/123/Kern/main/ui/dialog.c`

问题：

- “是 / 否”不适合危险操作。

建议：

- “取消 / 继续”
- “取消 / 确认清除”
- “返回 / 我已备份”
- “取消 / 确认签名”

## 建议菜单结构

首页 6 项：

1. 扫码签名
2. 连接钱包
3. 助记词
4. 备份
5. 设置
6. 设备检查

助记词：

- 创建助记词
- 导入助记词
- 已导入
- 高级工具

创建助记词：

- 骰子
- 20面骰
- 扑克牌
- 十六进制
- 拍照
- 输入单词

导入助记词：

- 输入单词
- 输入序号
- 扫描二维码
- 从存储卡导入
- 导入加密备份
- 钢板恢复

高级工具：

- 变换助记词
- 异或助记词
- 派生助记词
- BIP39 检查

连接钱包：

- BTC 钱包
- Web3 钱包
- 扩展公钥
- 地址核对
- 自定义路径

备份：

- 助记词序号
- 二维码备份
- 加密备份
- 钢板备份

设置：

- PIN 码
- 屏幕
- 相机
- 安全
- 关于

设备检查：

- 屏幕
- 相机
- 存储卡
- 电源

## 推荐修复顺序

### 第一轮：先防崩溃和安全绕过

1. 修 QR parser 长度安全和重复片段 UAF。
2. 修 PMOFN index/total 校验和完整性判断。
3. 建统一 capability gate，阻断中间助记词进入备份、地址、xpub、Web3、BIP85、passphrase。
4. 修中间助记词阻塞回调跳转到不存在页面的问题。
5. 修 scanner result_len 未初始化风险。

### 第二轮：修真钱包正确性

1. 修 passphrase 会话重载丢失。
2. 明确无状态策略，禁用本机 flash 保存助记词。
3. 加 Taproot/BIP86 地址测试向量。
4. 修助记词工具页面失败生命周期。
5. 限制助记词变换超长输入。

### 第三轮：统一 UI 和文案

1. 首页改 2x3 一屏铺满。
2. 统一返回按钮。
3. 输入框和键盘重做。
4. QR 备份页重排。
5. 删除开发/测试/验收文案。
6. 统一菜单命名。
7. 重建中文字库并做缺字扫描。

### 第四轮：重新验收

1. 离线单测：助记词、QR、派生路径、Web3、地址。
2. QR fuzz：非 NUL、内嵌 NUL、空内容、超长、乱序、重复片。
3. 模拟器截图：首页、全部二级菜单、输入页、备份页、错误页。
4. 真机测试：扫码、输入、导入、创建、备份显示、锁屏、PIN 错误 3 次保护。
5. 再出新固件包。
