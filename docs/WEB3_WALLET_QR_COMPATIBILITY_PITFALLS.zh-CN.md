# Web3 钱包二维码兼容踩坑记录

日期：2026-05-23

这份文档专门记录 KernSigner 在 OKX、Bitget、MetaMask、imToken、Rabby、TokenPocket 等钱包上踩过的二维码坑。以后改 Web3 连接码或签名回传码前，必须先看这里，避免把已经真机跑通的格式改坏。

扫码和摄像头排障先看 [QR_CAMERA_TROUBLESHOOTING.zh-CN.md](QR_CAMERA_TROUBLESHOOTING.zh-CN.md)。OKX 圆点动态二维码、ZBar fallback、UR 拼包和字体乱码的事故细节见 [OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md](OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md)。

## 一句话结论

- Waveshare ESP32-P4 4.3 第一次扫 Web3 钱包前，先用普通二维码手动调 OV5647 镜头焦距；焦距没调好时，OKX 圆点码会表现成“完全没反应”。
- 连接钱包和签名回传不是同一种二维码。
- 不同钱包对 `crypto-hdkey`、`crypto-multi-accounts`、`eth-sign-request`、`eth-signature` 的容忍度不一样。
- `eth-signature` 的 CBOR 字段类型必须保留，尤其是 `request_id`。
- 签名值本身正确，不代表钱包一定接受；钱包还会检查 `request_id`、`origin`、路径、账户指纹和二维码显示方式。

## 固件入口

主要代码位置：

- 连接码生成：`main/core/evm.c`
- Web3 扫码、请求解析、签名回传：`main/pages/scan/scan.c`
- Satochip EVM 地址和签名：`main/smartcard/smartcard_satochip.c`
- EIP-712 哈希：`main/core/eip712.c`

不要在全局范围乱改二维码规则。要按钱包分支最小修改。

## 连接码规则

| 钱包 | 已验证连接码 | 不要改成 |
| --- | --- | --- |
| MetaMask | 单账户 `crypto-hdkey` | 不要用 `crypto-multi-accounts` |
| imToken | 单账户 `crypto-hdkey` | 不要用 `crypto-multi-accounts` |
| Rabby | 单账户 `crypto-hdkey` | 不要用普通地址码 |
| TokenPocket | 单账户 `crypto-hdkey` | 不要删 `children` |
| Bitget | `crypto-multi-accounts` | 不要套用 MetaMask 单账户规则 |
| OKX | `crypto-multi-accounts`，可动态多片 | 不要退回普通地址码 |
| Keystone 通用 | 单账户 `crypto-hdkey` | 不要宣称所有钱包都能自动兼容 |

`MetaMask / imToken / Rabby / TokenPocket` 连接码固定要点：

- UR 类型：`ur:crypto-hdkey/...`
- 账户根路径：`m/44'/60'/0'`
- children：`0/*`
- coin type：`60`
- network：`0`
- name：`Keystone`
- note：`account.standard`
- 输出二维码内容统一大写，方便扫描。

## 签名请求规则

钱包给 Kern 的签名请求通常是：

```text
ur:eth-sign-request/...
```

常见 data type：

| data type | 含义 | 摘要算法 |
| --- | --- | --- |
| `1` | Legacy EVM transaction | `keccak256(unsigned_tx_rlp)` |
| `2` | EIP-712 TypedData | `keccak256(0x1901 || domainHash || messageHash)` |
| `3` | personal_sign | `keccak256(\"\\x19Ethereum Signed Message:\\n\" || len || message)` |
| `4` | Typed transaction | `keccak256(type_byte || unsigned_tx_rlp)` |

Kern 的 EIP-712 实现已经和电脑端 Python 标准库对齐过。不要因为 imToken 闪退就先怀疑 `eip712.c`，要先检查回传 CBOR 格式。

## 签名回传金标准

签名回传必须是：

```text
ur:eth-signature/...
```

CBOR map 至少包含：

| key | 字段 | 要求 |
| --- | --- | --- |
| `1` | request id | 必须按原请求类型/tag 回填 |
| `2` | signature | 65 字节 `r || s || recovery_id` |
| `3` | origin | 请求里有且钱包需要时才写 |

签名值：

- TypedData、personal_sign、typed transaction：最后 1 字节使用 `0/1`
- 不要给 MetaMask/imToken 回传 `27/28`
- Legacy 交易只有 OKX/Bitget 等特殊路径需要按钱包要求处理 EIP-155 v

显示：

- `eth-signature` 显示前统一转大写：`UR:ETH-SIGNATURE/...`
- 使用白底黑码、低纠错、足够静区
- 不要用高纠错把码做得更密

## imToken 最后一步闪退坑

已复现问题：

- MetaMask 能成功。
- imToken 能扫连接码，也能给 Kern 出签名请求。
- Kern 签名后 imToken 扫回 `eth-signature` 闪退。

实锤原因：

- 签名本身是对的。
- `origin` 是对的。
- `signature` 65 字节，最后一字节 `0/1` 也是对的。
- 错在 `request_id` 的 CBOR 类型。

错误开发板回传：

```text
01 78 24 ...    # key 1 = 普通 text string
```

电脑成功回传：

```text
01 d8 25 58 24 ...    # key 1 = tag 37 + bytes
```

所以 imToken 回传必须：

- `origin` 固定为 `imToken`
- 如果请求 id 看起来是字符串，也要按电脑成功方案编码为 `tag 37 + ASCII bytes`
- 不要把它简化成普通 CBOR text string

已验证电脑成功样本：

```text
UR:ETH-SIGNATURE/OTADTPDAHDDKESEMEEEOEMIEIYKPKPJSISDYIEEYJLETEMIMIHHSJSJPIMIYEMETISEHKTHSINETKPIEEHJOAOHDFPNNBDRSLTTOGDNTAXCMRHTPEHBSCWGHHDKKTSFSOTPASFJOLYWKFYEYJEGTGUMYWKIDHFHTFZPDBBPYCFAHWEFTFXDRNSRELACHLRHYBGAAHNWTJKOSPTBNETFHHSISAAAEAXIOINJNGHJLJEIHJTPTHDFSWK
```

关键解码结果：

- UR type：`eth-signature`
- CBOR length：`119`
- key `1`：`tag 37 + 36 bytes`
- key `2`：65 字节签名
- key `3`：`imToken`

## MetaMask 最后一步坑

MetaMask 已验证成功的关键点：

- 连接码使用单账户 `crypto-hdkey`
- 签名回传 `eth-signature`
- request id 保留 tag/类型
- origin 为空时不要强行写 `MetaMask`
- signature 最后一字节为 `0/1`

MetaMask 失败提示“确认您的二维码硬件钱包已使用此账户...”时，优先检查：

- 连接码主指纹是否真实，不要硬编码 `00000000`
- 请求地址和智能卡地址是否一致
- recovery id 是否为能恢复到当前地址的 `0/1`
- request id 是否原样回填

## OKX / Bitget 坑

OKX 和 Bitget 和 MetaMask/imToken 不是一套规则。

常见坑：

- OKX 连接码可能需要 `crypto-multi-accounts` 和动态多片，不要强行改成单账户。
- OKX 签名请求的圆点/圆圈二维码 payload 仍可能是标准 `ur:eth-sign-request/...`，不要先假设它是全新协议。
- OKX 圆点码如果开发板完全没反应，优先查硬件成像和图像解码层：摄像头焦距、FFC 排线、镜头脏污、屏幕反光、`ZBar` fallback、高分辨率解码帧、曝光和距离。
- OKX 动态码如果有进度但不完成，优先查 UR 分片是否收齐，不要先改签名算法。
- OKX 静态转账能签名时，说明 Web3 签名主链路基本是通的；动态问题优先看拼包和采帧效率。
- Bitget 连接码不要乱带过多 BTC 路径，否则可能变成多片或扫描异常。
- OKX/Bitget 签名结果二维码对显示密度敏感，`eth-signature` 大写、低纠错、白底黑码更稳。
- 不要把 OKX 的动态连接码显示规则套到 MetaMask/imToken。

2026-05-23 已验证：

- OKX 静态转账签名成功。
- OKX 动态 `eth-sign-request` 签名成功。
- 电脑端 OKX 图片和 mp4 抽帧可用 `components/zbar_qr/test` 验证。
- 当前字体使用非压缩 LVGL 位图，避免多语言构建后乱码。

## 小尺寸高密度二维码坑

ESP32-P4 这块板子的摄像头不是手机级扫码体验。遇到手机钱包给出的二维码很小、点很密、动态切换很快时，不要一直硬扫。

推荐顺序：

1. 第一次使用先用普通黑白二维码调焦：保持 10 到 20 厘米，捏住 OV5647 镜头外圈小幅旋转到最清楚。
2. 先截图或投屏到电脑，把二维码放大到屏幕中央再扫。
3. 如果电脑放大后仍不稳定，保存清晰照片或录屏，先在电脑端用 `zbarimg` 或 `components/zbar_qr/test` 验证。
4. 动态二维码可以先录屏，再抽帧检查是否能解出 `ur:eth-sign-request/...`。
5. 保持白底黑码、屏幕高亮、无反光，距离大约 10 到 20 厘米。

固件调参原则：

- 扫码页尽量轻：少文字、少按钮、少动画，保留必要进度提示。
- 预览可以低负载，解码帧要保证足够清晰。
- 不要每帧跑太多次圆点增强；动态码更需要持续采到新帧。
- 优先让 ZBar 和 quirc 分工，避免一个解码器卡住整个扫描循环。

这个问题通常是显示密度和摄像头识别能力问题，不一定是协议不支持。

## Rabby / TokenPocket 坑

当前建议：

- 连接码走单账户 `crypto-hdkey`
- 路径跟 MetaMask 一致：`m/44'/60'/0'` + `0/*`
- 签名回传仍走 `eth-signature`
- 如果钱包版本变化导致异常，先抓签名请求 CBOR，不要直接改通用逻辑

## 二维码显示坑

二维码协议正确，不代表手机一定扫得动。

已踩坑：

- 小写 `ur:eth-signature` 可能让二维码更密。
- 大写 `UR:ETH-SIGNATURE` 更容易走 QR alphanumeric mode，模块更少。
- 高纠错不一定更好，常常会变得更密。
- 显示结果码时尽量白底黑码、低纠错、大静区。
- 手机钱包扫屏幕比扫截图更挑剔，亮度和反光会影响结果。

## 调试方法

如果钱包最后一步失败，不要先乱改签名算法。按这个顺序：

1. 让用户用普通扫码工具扫开发板显示的结果码。
2. 保存完整 `UR:ETH-SIGNATURE/...` 文本。
3. 用本地 decoder 解出 CBOR。
4. 对比电脑成功样本：
   - UR type
   - CBOR map key 顺序
   - request id 类型/tag
   - signature 长度和最后一字节
   - origin
5. 只有确认 CBOR 完全一致后，再怀疑签名 digest 或智能卡。

本地 decoder 不应依赖 `/tmp` 临时路径。需要复现时，请把 decoder 源码或构建脚本整理到仓库内的 `tools/`，或明确记录如何用 `components/cUR/src/ur_decoder.c` 编译。

如果问题发生在“开发板完全不知道这是一张二维码”，先看 [QR_CAMERA_TROUBLESHOOTING.zh-CN.md](QR_CAMERA_TROUBLESHOOTING.zh-CN.md) 的图像层排查，不要直接进入 CBOR 和签名算法。

## 回归测试清单

每次改 Web3 二维码后至少测试：

1. MetaMask 连接码能添加账户。
2. MetaMask TypedData 签名最后一步成功。
3. imToken 连接码能添加账户。
4. imToken TypedData 签名最后一步成功，不闪退。
5. OKX 连接码能添加账户。
6. OKX 转账签名能扫回。
7. Bitget 连接码能添加账户。
8. Bitget 转账签名能扫回。
9. Rabby/TokenPocket 至少能扫描连接码。
10. BTC 签名不要被 Web3 改动影响。

## 绝对不要再做的事

- 不要把所有钱包统一成同一种连接码。
- 不要把 request id 转成普通字符串。
- 不要给 MetaMask/imToken 的 `eth-signature` 用 `27/28`。
- 不要给 MetaMask 空 origin 强行写钱包名。
- 不要因为 imToken 闪退就先改 EIP-712 hash。
- 不要在修 Web3 时顺手改 BTC 签名。
- 不要在没有真机验证前更新 GitHub release 固件。
- 不要因为 OKX 二维码是圆点样式就跳过桌面解码验证。
- 不要把字体压缩、翻译文案和扫码协议混在一次无日志刷机里排查。
