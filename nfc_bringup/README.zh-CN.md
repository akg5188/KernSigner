# KernSigner NFC PN532 独立跑通工程

这个目录是独立 ESP-IDF 小工程，用来先跑通红色 PN532 V3 NFC 模块。它不改 KernSigner 主固件，不写卡，不改 PIN，不重置卡，不签名，只做第一阶段硬件 bring-up：

- 扫描 I2C 总线。
- 读取 PN532 固件版本。
- 轮询 ISO14443A 卡片。
- 打印卡片 UID。

跑通这些之后，再把 NFC APDU 后端接回 KernSigner 主项目。

## 当前默认接法

当前测试固件使用 I2C，不是 SPI。现在默认使用独立 I2C 引脚，和主固件保持一致：

```text
SDA = GPIO28
SCL = GPIO31
```

接线按名字对名字：

```text
PN532 VCC -> 开发板 3V3
PN532 GND -> 开发板 GND
PN532 SDA -> 开发板 GPIO28
PN532 SCL -> 开发板 GPIO31
PN532 IRQ -> 不接
PN532 RSTO -> 不接
```

上电前安全检查：

```text
1. 用 3V3，不要接 5V。即使模块页面写工作电压到 5.5V，也不要用 5V 供电；很多 PN532 红板会把 I2C 上拉到供电电压，5V 可能伤 ESP32-P4 的 SDA/SCL。
2. GND 一定要接，PN532 和开发板必须共地。
3. 第一阶段只需要 VCC/GND/SDA/SCL 这 4 根线。
4. 先裸接跑通，不要先装进外壳。
5. 如果模块有两排接口，要插带 SDA/SCL/VCC/GND 字样的 I2C 那组，不要插 SCK/MISO/MOSI/SS 那组 SPI 口。
6. 不要按杜邦线颜色判断，必须按模块和开发板丝印名字接。
7. 确认没有歪插、跨脚、错排、反插。
8. 如果有万用表，蜂鸣档确认 PN532 VCC 和 GND 不短路。
9. 第一次上电时盯着模块；一发热、冒烟、异味、反复掉电，马上拔 USB。
```

如果 PN532 已经明显发热、冒烟、有异味、灯不亮或芯片鼓包，不要再把它接回开发板。

## PN532 模块模式

PN532 V3 红板一般支持 I2C、SPI、HSU/UART 三种模式，模式由模块上的拨码开关或焊盘选择。

当前测试要把模块设为 `I2C` 模式。很多红色 PN532 V3 模块的常见拨码是：

```text
SW1 = ON
SW2 = OFF
```

但不同卖家丝印可能写成 `I0/I1`、`SET0/SET1`、`DIP1/DIP2`，方向也可能不一样。最终以你买的模块背面丝印或卖家页面为准。

改拨码后建议完全断电再重新插 USB，让 PN532 重新按新模式启动。

## 现在这块板测到的现象

当前固件会先扫描 I2C 总线。正常启动时会看到类似：

```text
KernSigner NFC PN532 I2C bring-up
I2C port=-1 SDA=28 SCL=31 Hz=100000 address=0x24
Scanning I2C bus...
```

如果接线和模式正确，应能扫到 PN532：

```text
I2C device found at 7-bit address 0x24
PN532 firmware: IC=0x32 Ver=1.6 Support=0x07
Waiting for ISO14443A card...
```

有些资料把 PN532 I2C 地址写成 `0x48`，那通常是 8-bit write address。ESP-IDF 这里用 7-bit 地址，所以代码里写 `0x24`。

之前在这块 Waveshare 板子的板载共享 `SDA/SCL` (`GPIO7/GPIO8`) 上扫到过这些设备：

```text
0x18
0x36
0x40
0x5D
```

这说明开发板板载共享 I2C 总线是活的，但 PN532 现在默认改走 `GPIO28/GPIO31` 专用 I2C。当前接法如果没有出现 `0x24`，优先检查 PN532 模块本身：

```text
1. PN532 是否真的拨到 I2C 模式。
2. 是否插在 PN532 的 SDA/SCL/VCC/GND 那组接口。
3. VCC 是否接 3V3。
4. GND 是否接 GND。
5. SDA/SCL 是否插反或松动。
6. 改拨码后是否断电重启过。
```

## GPIO 是什么

`GPIO` 是 `General Purpose Input/Output`，意思是通用输入输出脚。板子上不一定直接印 `GPIO` 这几个字，常见会写成：

```text
GPIO28
IO28
D28
28
```

这些通常都表示 ESP32-P4 的某个可编程引脚。比如板子丝印是 `GPIO28`、`IO28` 或 `D28`，在配置里一般就填：

```text
28
```

注意 `GPIO` 里的 `O` 是英文字母 O，不是数字 0。`GPIO0` 才是编号为 0 的那个引脚。

## 40PIN 接口注意

Waveshare 这块板背面是双排 `2x20` 扩展口。它看起来像树莓派 40PIN，但不要按树莓派 pinout 经验直接插，要按 Waveshare 这块板自己的丝印和官方资料来。

双排接口最容易出错的是“看的是靠近文字那一排，插到的是旁边那一排”。上电前逐根确认：开发板这一端实际插入的孔位旁边丝印必须就是 `3V3/GND/SDA/SCL`，不能只看模块那边线序。

临时调试常见线材：

```text
开发板 40PIN 是母座/孔 -> 杜邦线公头插开发板
PN532 模块是排针       -> 杜邦线母头插 PN532
```

所以常见组合是：

```text
公对母杜邦线
```

如果觉得直插太高，可以用：

```text
2.54mm 单排 90 度弯针
剪成单根或几小段
再配母对母杜邦线
```

## 编译和烧录

进入这个目录：

```bash
cd /home/ak/123/KernSigner/nfc_bringup
```

编译、烧录、看串口：

```bash
. /home/ak/esp-idf-v5.5.4/export.sh
idf.py -p /dev/ttyACM0 build flash monitor
```

如果你的串口不是 `/dev/ttyACM0`，换成实际端口。

退出 monitor：

```text
Ctrl+]
```

## 成功现象

第一步成功：

```text
I2C device found at 7-bit address 0x24
PN532 firmware: IC=0x32 Ver=1.6 Support=0x07
```

这一步只证明新 PN532 模块供电、I2C 模式和 `SDA/SCL` 接线是对的；不用贴智能卡，也不会写卡。

第二步把卡靠近 PN532 白色天线区域，应该看到类似：

```text
Card detected: UID=04A1B2C3D4E5F6
```

如果看到固件版本，说明：

```text
ESP32-P4 -> I2C -> PN532
```

已经通了。

如果能读 UID，说明：

```text
PN532 -> ISO14443A 卡片
```

也通了。

## 常见问题

### 扫不到 0x24

这次实际测试已经证明开发板 `SDA/SCL` 总线能扫到板载设备。如果扫不到 `0x24`，优先怀疑 PN532 侧：

- 模块没有拨到 I2C。
- 插错到 SPI 那排针。
- `SDA/SCL` 插反。
- `VCC/GND` 没插好。
- 改拨码后没有断电重启。
- 便宜模块虚焊、拨码开关接触不好。

### 能扫到 0x24，但读固件失败

优先检查：

- 线是否太长或接触不稳。
- PN532 是否和金属、主板大面积铜皮贴得太近。
- VCC 是否稳定。
- 模块是否需要接 `RSTO` 后复位。

### 能读固件版本，但读不到卡

优先检查：

- 卡是否贴近白色天线区域。
- PN532 天线面是否朝向卡。
- 卡是否真的是 13.56MHz NFC/ISO14443A。
- 不要把模块贴在金属螺丝、铜柱、电池或主板大面积铜皮旁。

### 放进外壳后距离变短

正常。塑料壳一般不干扰 NFC，主要影响是距离和附近金属/PCB。PN532 天线应尽量贴近外壳外表面，建议只隔 `1-3mm` 塑料。金属壳、金属螺丝、电池、屏蔽罩、大面积铜皮会明显削弱感应距离。

## 下一步

这个工程跑通后，再加第二阶段：

```text
InListPassiveTarget
-> ISO14443-4 target
-> InDataExchange
-> SELECT Satochip / SeedKeeper AID
```

第二阶段成功后，才接回 KernSigner 主项目，让现有 Satochip/SeedKeeper 逻辑复用 NFC APDU 传输层。
