# Satochip 高安全观察钱包（Android）

这个目录对应当前的安卓观察钱包源码。

按当前源码，它的定位已经固定为：

- `Arbitrum One` 高安全观察钱包
- `WalletConnect v2` 高安全协调器
- 树莓派离线签名请求二维码生成器
- 树莓派签名结果回传后的人工确认广播器
- `BTC xpub / ypub / zpub` 观察账户原型
- `OKX Wallet / Bitget Wallet / Keystone` 兼容 Web3 二维码桥接

它不是：

- 热钱包
- 手机本地私钥钱包
- 内置 `Hyperliquid` 浏览器

## 当前能做什么

- 管理多个 `EVM` 观察地址
- 查看 `Arbitrum One` 上的 `ETH / USDC / USDT`
- 生成给树莓派扫描的 EVM 待签名请求
- 扫描树莓派回签结果并人工确认广播
- 通过 `WalletConnect v2` 协调 DApp 请求
- 保留独立钱包连接 DApp 所需的 `WalletConnect / DApp 签名` 入口
- `OKX Wallet / Bitget Wallet` 的 Keystone `crypto-multi-accounts` 连接二维码改由树莓派固件首页 `连接钱包` 生成
- 扫描 `eth-sign-request`，转成低密度树莓派中转二维码
- 扫回树莓派签名结果，再显示 `eth-signature` 给 Web3 钱包扫回去
- 导入 `BTC xpub / ypub / zpub` 观察账户
- 主网 `BTC` 同步优先走 `Electrum`，更快返回余额、活动、下一收款地址
- 生成 `BTC PSBT` 给树莓派冷签

## 当前安全策略

- 强制强生物识别
- 拦截 `ADB / USB 调试`、开发者选项、`Root / Magisk / Hook`、`test-keys`
- 关键联网主机白名单 + 证书 pinning
- `WalletConnect` 仅接受已验证、`HTTPS`、主机合法的 DApp
- Web3 桥接只保存公开地址、xpub、公钥和链码，不保存助记词或私钥
- 锁屏或退后台后主动清理敏感状态

## 当前工作区构建

```bash
./scripts/build_local_ascii.sh debug
```

输出：

- `dist/satochip-wallet-debug.apk`
- `dist/satochip-wallet-debug.apk.sha256`

## 当前 release 构建

```bash
./scripts/build_local_ascii.sh release
```

输出：

- `dist/satochip-wallet-release.apk`
- `dist/satochip-wallet-release.apk.sha256`

release 构建必须先配置 `wallet/keystore.properties`，不会回退到 debug 签名。

## 发布签名规则

当前 `release` 构建不能再回退到 debug 签名。

必须准备：

- `wallet/keystore.properties`

示例：

- `wallet/keystore.properties.example`

如果以后丢了旧 keystore 密码，只能换一把新的发布签名。

这不会影响链上资产安全，因为手机里不存私钥；但旧钱包 APK 将无法直接覆盖升级，必须卸载后重装，再重新导入观察地址、`xpub / ypub / zpub` 和 `WalletConnect` 会话。

本地验证或给自己临时安装时，可以先打 debug 包：

```bash
./gradlew :app:assembleDebug
```

debug 包路径：

- `app/build/outputs/apk/debug/app-debug.apk`

## GitHub 自动验包状态

当前仓库里没有单独的钱包 release 验包工作流，现有工作流仍以整仓库测试为主。

如果以后要新增钱包 release 验包，需要先在仓库 `Secrets` 里配置：

- `WALLET_RELEASE_KEYSTORE_B64`
- `WALLET_RELEASE_STORE_PASSWORD`
- `WALLET_RELEASE_KEY_ALIAS`
- `WALLET_RELEASE_KEY_PASSWORD`

其中：

- `WALLET_RELEASE_KEYSTORE_B64` 是发布 keystore 文件的 `base64` 内容
- 其余 3 个值和 `wallet/keystore.properties` 里的同名字段一一对应

如果这些 secrets 没配，不要把 CI 产物宣传成正式签名 release；本地构建规则仍然不变。

## 文档入口

- [使用教程](docs/使用教程.zh-CN.md)
- [开发维护指南](docs/开发维护指南.zh-CN.md)
- [固定构建与验包流程](docs/固定构建与验包流程.zh-CN.md)
- [安卓构建环境准备](../docs/安卓构建环境准备.zh-CN.md)
- [安卓中转 App 新手教程](../docs/ANDROID_RELAY_WALLET_GUIDE.zh-CN.md)

如果你想看整仓库的其他模块，回到：

- [../README.md](../README.md)
