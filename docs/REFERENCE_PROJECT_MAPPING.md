# satochip-signer 参考项目映射

日期：2026-05-20

参考项目路径：`/home/ak/123/satochip-signer`

## 参考文件

- 树莓派页面：`seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/views/tp_views.py`
- 智能卡工具页：`seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/views/tools_views.py`
- SeedKeeper 工具：`seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/helpers/seedkeeper_utils.py`
- Satochip 签名器：`seedsigner-os/opt/rootfs-overlay/opt/src/seedsigner/helpers/satochip_signer.py`
- Python CardConnector：`pi-signer-py/vendor/pysatochip/CardConnector.py`
- Kotlin Satochip 签名参考：`app/src/main/java/com/tpsigner/SatochipSigner.kt`
- Kotlin EVM 编码参考：`app/src/main/java/com/tpsigner/EvmTxEncoder.kt`

## Kern 对应文件

- CCID/APDU：`main/smartcard/smartcard_ccid.c`
- Satochip 协议：`main/smartcard/smartcard_satochip.c`
- Satochip 头文件：`main/smartcard/smartcard_satochip.h`
- Web3 扫码签名：`main/pages/scan/scan.c`
- Krux 风格菜单和智能卡页：`main/pages/signer_shell/signer_shell.c`
- Web3 连接码：`main/core/evm.c`
- 功能目录和文案：`main/signer_port/signer_feature_catalog.c`

## 已迁移方向

- Web3 钱包选择：OKX、Bitget、MetaMask、Rabby、TokenPocket、Keystone。
- 连接码类型：OKX/Bitget 多账户码，imToken/MetaMask/Rabby/TokenPocket HDKey 码。
- Web3 请求：`eth-sign-request`、钱包签名请求封装、personalSign 基础路径。
- 签名结果：`eth-signature`。
- imToken / MetaMask 已有实测金标准：`docs/WEB3_IMTOKEN_METAMASK_PROVEN_FLOW.zh-CN.md`。
- Satochip PIN 后签名。
- 地址比对后才返回签名。
- BTC 观察公钥读取。

## 未迁移方向

- EIP-712 TypedData。
- TokenPocket 原生 `signTransaction` 完整解析。
- BTC PSBT 卡签名。
- BTC 消息卡签名。
- SeedKeeper 管理。
- 写卡、改 PIN、重置。
- 卡片真伪检查。
- 2FA 管理。

## 迁移原则

- 能拒签就不要盲签。
- 能隐藏就不要放说明页冒充完成。
- 所有写卡和 PIN 管理必须单独设计确认页和失败恢复流程。
- 所有智能卡 APDU 都要考虑断电、拔卡、超时和迟到响应。
- 每迁移一个功能，必须补真机验收步骤和失败处理文案。
