# 智能卡供电和 OTG 排障

日期：2026-05-22

ACR39U-NF 读卡器在 ESP32-P4 上不稳定时，优先排查供电和 OTG 方向。不要一开始就判断驱动坏了。

实测结论：Waveshare ESP32-P4 4.3 开发板的 OTG 口不能稳定给 ACR39U-NF Pocketmate II 提供 5V 外设供电。智能卡功能要正常工作，必须使用带供电 OTG 转接线，或者真正外接供电的 USB Hub。

![带供电 OTG 智能卡接法实拍图](screens/powered_otg_smartcard_setup.jpg)

## 正确链路

```text
ESP32-P4 USB OTG 口
-> 带供电 OTG 转接线或真正外接供电的 Hub
-> ACR39U-NF 读卡器
-> Satochip 或 SeedKeeper 卡
```

下载口只用于电脑串口和刷机，不是给 ESP32-P4 当 USB Host 的口。

## 常见现象

- 读卡器灯只闪一下：供电不足或 VBUS 不稳。
- Hub 能识别但读卡器不出现：Hub 下游供电或转接方向错误。
- 有 ATR 但 APDU 失败：供电边缘、卡没插牢、CCID 传输细节或超时。
- 第一次能签，第二次相机黑屏：优先看相机资源释放和重新进入流程。
- 电脑直供可以开机但读卡器不亮：开机电流和 USB Host 外设电流不是一回事。
- 开发板能开机不代表 OTG 口能给读卡器供 5V；这是两条不同的供电路径。

## 排查顺序

1. 换带供电 OTG 转接线，或换真正外接供电的 Hub。
2. 确认读卡器在带电 OTG/Hub 下游，不在下载口。
3. 先跑 `设备检查 -> 智能卡检测`。
4. 看到 ATR 后再测 Satochip 状态。
5. 状态成功后再测连接码。
6. 连接码成功后再测签名。
7. 签名一次后立刻再扫码一次，检查相机恢复。

## 不要再踩的坑

- 不要把“键盘灯不亮”直接等同于 C 口坏；可能是 Host 供电策略问题。
- 不要把“电脑能识别读卡器”直接等同于 ESP32-P4 能直插供电。
- 不要把“有 Hub”当成一定有供电；Hub 自身必须真的外接供电。
- 不要把 ATR 成功当成签名安全完成。
- 不要把 Satochip Web3 成功当成 SeedKeeper 已完成。

## SeedKeeper 重置不是供电问题

如果 SeedKeeper 重置返回 `9C20`，通常不是读卡器供电问题，也不是 CCID 驱动坏了。新版 SeedKeeper 不支持旧的 `B0 FF` 恢复出厂流程，应使用：

```text
智能卡 -> SeedKeeper -> 重置 -> 错PIN一步 -> 错PUK一步
```

直到返回 `FF00`，才代表恢复为空白卡。详细步骤看 [Satochip / SeedKeeper 智能卡实测操作手册](SMARTCARD_SATOCHIP_SEEDKEEPER_OPERATION_GUIDE.zh-CN.md)。
