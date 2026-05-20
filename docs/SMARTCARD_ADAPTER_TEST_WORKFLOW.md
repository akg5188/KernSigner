# 智能卡读卡器转接线测试流程

目标很窄：证明 Waveshare ESP32-P4 能通过外接供电链路把 ACR39U-NF Pocketmate II 读卡器跑通。当前 Kern 已接入安全检测页，可测 USB 枚举、CCID 接口、ATR、Satochip/SeedKeeper AID 识别和只读状态 APDU；Satochip Web3 连接/签名、路径地址和 BTC 观察公钥走单独的智能卡钱包入口。检测页本身不写卡、不改 PIN、不重置。

## 当前准备状态

- 探针项目：`/home/ak/123/seedsigner/esp32_p4_ccid_probe`
- Kern 总入口：`/home/ak/123/Kern/tools/run_ccid_adapter_probe.sh`
- 日志目录：`/home/ak/123/Kern/docs/logs`
- 默认监控时间：90 秒
- 默认编译并发：2，避免电脑卡死
- 探针会在启动时清掉旧结果，避免把上一次 PASS 当成本次结果
- 如果用充电器离线测试，插回电脑后用 `read` 模式读取上次 NVS 结果
- Kern 真机入口：`设备检查 -> 智能卡检测`

## 先验收软件

转接线没到也可以先跑一次编译，不刷机：

```bash
cd /home/ak/123/Kern
tools/run_ccid_adapter_probe.sh build
```

这一步只确认探针固件能编译。通过后等转接线到了再跑真机。

## 到货后的接线

1. 开发板的 USB-to-UART/下载口接电脑，用来刷机和看串口日志。
2. 开发板的 USB OTG Type-C 口接转接线。
3. ACR39U-NF 读卡器接到转接线的设备端。
4. 智能卡先插进读卡器。
5. 如果用带电 Hub，Hub 只能放在 OTG 转接线后面，不能替代 OTG 主机方向。

不要把读卡器插到 USB-to-UART/下载口。那个口主要给电脑识别串口，不是给 ESP32-P4 当 USB Host 用。
如果 OTG 侧没有外接供电，这条链路通常不会稳定工作。优先使用带电 OTG 转接线或外接供电 Hub。

## 一键测试

接好线后执行：

```bash
cd /home/ak/123/Kern
MONITOR_SECONDS=90 tools/run_ccid_adapter_probe.sh run
```

如果串口自动识别错了，指定端口：

```bash
cd /home/ak/123/Kern
ESPPORT=/dev/ttyACM0 MONITOR_SECONDS=90 tools/run_ccid_adapter_probe.sh run
```

如果已经刷过探针固件，只想重新看日志：

```bash
cd /home/ak/123/Kern
NO_FLASH=1 MONITOR_SECONDS=90 tools/run_ccid_adapter_probe.sh monitor
```

如果开发板当时不是接电脑，而是用手机充电器供电完成离线测试，之后插回电脑读取上次结果：

```bash
cd /home/ak/123/Kern
tools/run_ccid_adapter_probe.sh read
```

`read` 模式默认只看 20 秒左右，会优先解析启动日志里的 `Previous report`。

## 结果判定

- `state=PASS`：USB 枚举、CCID、ATR、一条 APDU 全部通过，可以进入下一阶段。
- `state=ATR_OK`：读卡器和卡片上电已经通，硬件路径基本成立，后面排 APDU 细节。
- `state=CCID_READY`：读卡器识别到了，但没有拿到 ATR，先重新插拔卡片或换供电。
- `state=HUB`：只看到 Hub，没看到读卡器，检查读卡器是否插在 Hub 下游。
- `state=TIMEOUT reason=no_usb`：ESP32-P4 没看到任何 USB 设备，优先查转接线方向、VBUS、OTG 口。
- `state=FAIL reason=non_ccid`：看到 USB 设备，但不是 CCID 读卡器，可能插错设备或只识别到其他桥接设备。
- `state=FAIL reason=power_on_*`：CCID 已经通，但卡片上电失败，查卡是否插好、供电是否足。
- `state=FAIL reason=apdu_*`：ATR 已过或接近通过，后面重点改 CCID/APDU 传输。

只要没有 `state=PASS` 或 `state=ATR_OK`，不要测试钱包级智能卡功能。当前 Kern 的钱包级入口只允许已经实现的 Satochip Web3 连接/签名、路径地址和 BTC 观察公钥读取。

## 现场操作顺序

1. 插电脑下载口。
2. 插 OTG 转接线和读卡器。
3. 插智能卡。
4. 跑 `tools/run_ccid_adapter_probe.sh run`。
5. 等 90 秒自动退出。
6. 看终端最后一行 `Final report`。
7. 把最新日志保留在 `docs/logs/ccid_probe_*.log`。

如果第一次是 `CCID_READY` 但没有 ATR，不要马上改代码。先不拔开发板，只拔插智能卡一次，再跑 `NO_FLASH=1 MONITOR_SECONDS=90 tools/run_ccid_adapter_probe.sh monitor`。

如果现场只能用手机充电器给开发板供电，就先让探针固件跑 60 秒以上，再拔电插回电脑，执行 `tools/run_ccid_adapter_probe.sh read`。这种模式只适合看上一次自动测试结果，不适合实时调试。

## 通过后的下一步

通过标准是至少拿到 `ATR_OK`，最好是 `PASS`。当前已经完成：

1. 把探针里的 CCID 传输层抽成 Kern 可用组件。
2. 开放 `智能卡检测`：检测读卡器、读取 ATR、识别 Satochip/SeedKeeper、读取只读状态。

后续再做：

1. 多次真机热插拔回归，确认外接供电链路稳定。
2. 持续回归已接入的 Satochip Web3 连接/签名、路径地址和 BTC 观察公钥读取。
3. SeedKeeper 先只保留检测识别；列表、导入、导出、写入必须另走安全设计。
4. 最后才评估写卡、PIN 管理、重置和 Satochip BTC 卡片签名。

这个顺序能避免又陷入“钱包功能写了很多，但读卡器根本没通”的坑。
