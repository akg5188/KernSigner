# KernSigner 大资金安全版目标

日期：2026-05-27

这份文件把“大资金安全版”拆成可执行门禁。它不是安全承诺，也不是审计报告；只有全部门禁、真机证据和独立审计通过后，才允许考虑大额资金。

## 当前结论

当前源码仍是测试资金/研究版。`tools/signer_production_check.sh sdkconfig.release.wave_43` 现在会失败，主要缺口包括：

- Secure Boot 未启用。
- Flash Encryption 未启用。
- NVS Encryption 未启用。
- `CONFIG_KSIG_PRODUCTION_REQUIRE_PIN_HMAC` 未启用。
- USB Serial/JTAG console、UART console、GDB stub 未全部关闭。
- ETH/LWIP 网络栈仍在 release 配置里出现。
- task watchdog panic 未启用。
- 工作区不是 clean 状态。

## 新增目标配置

本次新增：

```text
sdkconfig.defaults.high_value
```

构建目标配置：

```bash
idf.py -B build_high_value_wave_43 \
  -D SDKCONFIG=build_high_value_wave_43/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43;sdkconfig.defaults.high_value' \
  build
```

构建后必须跑：

```bash
tools/signer_production_check.sh build_high_value_wave_43/sdkconfig
```

`prodcheck` 不通过，就不是大资金安全版。

## 第一阶段：先做只读硬化版

目标：把设备收敛成最小签名器，不开放维护/写入/实验入口。

必须做到：

- 默认只允许 QR 输入输出和明确白名单的签名流程。
- NFC 只允许在独立 bring-up 工程里测试，不进入大资金版主固件。
- USB CCID 智能卡只保留已经真机验收的只读/签名路径。
- 禁止默认开放 SeedKeeper 写入助记词、重置、改 PIN、证书导入、NFC policy、2FA policy 等维护入口。
- 禁止 Web3 盲签：不能本机解析展示的交易必须拒签。
- EIP-712/TypedData 没有完整结构化显示和测试向量前必须拒签。
- SD 卡只作为受控导入/更新介质，所有文件输入必须长度检查和格式检查。

## 第二阶段：芯片级安全

必须在测试板完整演练后才能对正式设备执行：

- 生成并离线备份 Secure Boot 签名私钥。
- 烧录 Secure Boot digest eFuse。
- 启用 Secure Boot。
- 启用 Flash Encryption。
- 启用 NVS Encryption。
- 关闭 UART console、USB Serial/JTAG console、GDB stub。
- 关闭无线、网络栈和不需要的外设入口。
- 启用 anti-rollback。
- 记录 eFuse 摘要、构建 commit、固件 SHA256、烧录日志。

提醒：eFuse 操作不可逆，必须先用牺牲板演练。

## 第三阶段：交易安全

BTC：

- PSBT 必须显示输入来源、输出地址、找零判断、手续费、手续费率。
- 找零地址必须和当前钱包派生规则匹配，否则提示高风险或拒签。
- 非标准脚本、多签、未知派生路径必须明确显示并默认拒签。

Web3：

- 必须显示 chainId、from、to、value、nonce、gas、max fee、合约方法摘要。
- ERC-20 transfer/approve 必须显示 token、数量、spender。
- 无限授权必须高风险确认。
- 不能解析的 calldata 默认拒签。

## 第四阶段：真机验收

至少完成：

- 断电/重启/返回/取消/超时后，助记词、PIN、签名请求缓存清理。
- PIN 连续失败、达到阈值、重启中断、断电中断全部验证。
- 二维码畸形输入、超长输入、重复分片、乱序分片全部验证。
- 智能卡错 PIN、拔卡、读卡器掉电、APDU 超时全部验证。
- 连续签名 50 次，相机和 UI 不死锁、不泄漏敏感文本。
- 同一测试向量在设备、参考钱包、离线脚本结果一致。

## 第五阶段：外部审计

大资金版至少需要独立审计这些部分：

- 助记词生成、导入、存储、擦除。
- PIN、anti-phishing、eFuse HMAC。
- PSBT/交易解析和拒签策略。
- Satochip/SeedKeeper APDU 和 Secure Channel。
- QR/UR/BBQR parser。
- 构建、发布、签名和刷机流程。

## 不允许的捷径

- 不能因为 `prodcheck` 通过就直接放大资金。
- 不能因为 OKX/Bitget/Rabby 测试成功就宣称生产安全。
- 不能在 NFC 刚跑通 UID 后就开放 NFC 签名。
- 不能把开发板、杜邦线、未封胶飞线版本当最终大资金硬件。
- 不能用真实大额助记词做首轮验收。

## 可接受的最终说法

只有全部通过后，才可以说：

```text
KernSigner high-value candidate build
```

仍不建议说：

```text
绝对安全
银行级
审计通过
可无条件放大资金
```

安全版的目标是把风险降到可审查、可复现、可回归，而不是靠一句话保证。
