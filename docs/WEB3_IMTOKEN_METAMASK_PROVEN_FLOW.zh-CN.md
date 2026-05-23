# imToken / MetaMask 已跑通方案

本文记录 2026-05-23 电脑端实测跑通的 Web3 连接和签名方案。以后修改固件时，imToken 和 MetaMask 必须优先对齐本文，不要再反复尝试已经失败的格式。

## 测试账户

- 测试助记词：`abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about`
- EVM 地址：`0x9858effd232b4033e47d90003d41ec34ecaeda94`
- 默认路径：`m/44'/60'/0'/0/0`
- 账户根路径：`m/44'/60'/0'`
- 主指纹：`73c5da0a`

## 连接码金标准

imToken 和 MetaMask 都使用单账户 `crypto-hdkey`，不要使用 `crypto-multi-accounts`。

已验证通过：

- UR 类型：`ur:crypto-hdkey/...`
- origin/keypath：`m/44'/60'/0'`
- children：`0/*`
- coin type：`60`
- network：`0`
- name：`Keystone`
- note：`account.standard`
- 大写输出：连接码内容统一转大写

已知不要再试：

- 不要给 imToken / MetaMask 使用 `crypto-multi-accounts`
- 不要删除 children 字段
- 不要把 children 改成 `0/0`
- 不要把连接码当普通地址二维码

本地测试文件说明：早期样本曾放在个人下载目录和临时目录中，公开文档不再引用这些绝对路径。需要复现时，请把样本图或生成脚本整理到仓库内的 `docs/web3_fixtures/` 或 `tmp_imtoken_qr/`，并在提交说明里写明 SHA256。

## imToken 签名回传金标准

实测请求：

- 请求类型：`ur:eth-sign-request/...`
- data type：`2`，即 EIP-712 TypedData
- origin：`imToken`
- chainId：请求 CBOR 没有 key `4` 时按 `1` 处理
- 地址：`0x9858effd232b4033e47d90003d41ec34ecaeda94`
- 路径：`m/44'/60'/0'/0/0`

回传必须：

- UR 类型：`ur:eth-signature/...`
- CBOR map 至少包含：
  - `1`: 原请求 request id，保留原 CBOR 类型和 tag；imToken 对这里非常敏感，已验证成功格式是 `tag 37 + bytes`
  - `2`: 65 字节签名 `r || s || recovery_id`
  - `3`: origin，imToken 请求有 origin 时保留 `imToken`
- recovery id 使用 `0/1`，不要使用 `27/28`
- 输出二维码内容统一转大写

真机踩坑记录：

- imToken 最后一步闪退时，先不要怀疑签名或 EIP-712 hash。
- 已确认 Kern C 代码的 EIP-712 digest 和电脑端 Python 标准库一致。
- 闪退实锤原因之一是把 request id 回传成普通 CBOR text string。
- 正确格式示例：`01 d8 25 58 24 ...`，即 key `1` 后面是 `tag 37 + bytes`。
- 错误格式示例：`01 78 24 ...`，即 key `1` 后面是普通 text string。

电脑端实测输出：imToken 扫描后 DApp 成功收到签名。样本图如需入库，应放在 `docs/web3_fixtures/` 并记录 SHA256，不引用个人下载目录。

## MetaMask 签名回传金标准

实测请求：

- 请求类型：`ur:eth-sign-request/...`
- data type：`2`，即 EIP-712 TypedData
- origin：空
- chainId：`1`
- 地址：`0x9858effd232b4033e47d90003d41ec34ecaeda94`
- 路径：`m/44'/60'/0'/0/0`

回传必须：

- UR 类型：`ur:eth-signature/...`
- CBOR map 至少包含：
  - `1`: 原请求 request id，保留原 CBOR 类型和 tag
  - `2`: 65 字节签名 `r || s || recovery_id`
- MetaMask 请求 origin 为空时，不要强行写钱包名
- recovery id 使用 `0/1`，不要使用 `27/28`
- 输出二维码内容统一转大写

电脑端实测输出：MetaMask 扫描后 DApp 成功收到签名。样本图如需入库，应放在 `docs/web3_fixtures/` 并记录 SHA256，不引用个人下载目录。

## TypedData 哈希

EIP-712 TypedData 哈希必须按标准计算：

```text
digest = keccak256(0x1901 || hashDomain(domain) || hashStruct(primaryType, message))
```

签名前必须核对：

- 请求地址和本机派生地址一致
- 请求路径和用户选择的助记词/智能卡来源一致
- TypedData 内容由用户主动发起

## 固件移植检查点

修改固件时只允许在对应钱包路径内做最小改动：

- imToken / MetaMask 连接码：保持单账户 `crypto-hdkey`
- imToken / MetaMask 签名：保持 `eth-sign-request -> eth-signature`
- request id：必须原样回填
- imToken request id：即使扫码解析成字符串，回传也要按 `tag 37 + ASCII bytes` 编码
- recovery id：必须是 `0/1`
- origin：有就保留，没有就不写
- 签名结果二维码：大写输出

更多防踩坑记录见：

```text
docs/WEB3_WALLET_QR_COMPATIBILITY_PITFALLS.zh-CN.md
```

回归测试至少做：

1. imToken 扫连接码添加测试账户
2. imToken 发起 TypedData 签名，扫码回传成功
3. MetaMask 扫连接码添加测试账户
4. MetaMask 发起 TypedData 签名，扫码回传成功
5. 确认地址仍为 `0x9858effd232b4033e47d90003d41ec34ecaeda94`
