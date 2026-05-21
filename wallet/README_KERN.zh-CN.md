# Kern 安卓中转 App

这个目录是从树莓派项目复制过来的安卓中转钱包 App。

复制来源：

```text
/home/ak/123/satochip-signer/wallet
```

当前位置：

```text
/home/ak/123/Kern/wallet
```

## 用途

当 `OKX Wallet`、`Bitget Wallet`、`TokenPocket` 等钱包显示的签名二维码太小、太密，`ESP32-P4 Kern` 开发板扫不出来时，用这个安卓 App 做中转。

它会把高密度签名请求二维码转换成开发板更容易扫描的低密度二维码。

## 新手教程

请先看：

```text
/home/ak/123/Kern/docs/ANDROID_RELAY_WALLET_GUIDE.zh-CN.md
```

## 构建 APK

已复制一个历史测试 APK：

```text
/home/ak/123/Kern/wallet/dist/smartcard-compat-v0.1.19-usbt1-release.apk
```

这个包可以先用来测试二维码中转流程。正式使用建议重新构建当前源码。

```bash
cd /home/ak/123/Kern/wallet
./gradlew :app:assembleDebug
```

输出位置：

```text
/home/ak/123/Kern/wallet/app/build/outputs/apk/debug/app-debug.apk
```

## 安全边界

这个 App 不是热钱包。

它不保存助记词和私钥，只负责观察账户、二维码中转和联网广播辅助。

真正签名在 `Kern` 开发板或智能卡里完成。
