# OKX 圆点动态二维码扫码事故复盘

日期：2026-05-23

目的：记录这次 OKX Wallet 圆点/圆圈样式二维码从“完全没反应”到“静态转账和动态签名都成功”的完整踩坑过程。以后再改扫码、Web3 UR、字体或多语言时，先按本文排查，不要重复盲刷。

日常排障先看：[QR_CAMERA_TROUBLESHOOTING.zh-CN.md](QR_CAMERA_TROUBLESHOOTING.zh-CN.md)。本文是事故复盘和证据记录，细节多，不适合当第一入口。

## 最终结论

- Waveshare ESP32-P4 4.3 开发板第一次使用相机前，必须先做物理对焦检查。部分 OV5647 摄像头出厂焦点不在手机/电脑屏幕扫码距离上，需要捏住镜头外圈小幅旋转，让 10-20 cm 距离的二维码边缘变清楚。
- OKX 的圆点/圆圈样式签名码不是一套新的签名协议，payload 仍然是标准 `ur:eth-sign-request/...`。
- 问题主要分两层：
  - 硬件成像层：摄像头焦点、排线安装、镜头脏污、手机屏幕反光都会让开发板完全扫不到。
  - 图像识别层：`quirc` 对圆点渲染、高密度、小尺寸码不够稳，需要 ZBar fallback、高分辨率解码帧和适度圆点增强。
  - 协议解析层：当前 `qr_parser` 能把 OKX 动态 UR 拼成 `eth-sign-request`，CBOR 解析和签名流程已经跑通。
- 不要看到圆点定位符就先写新协议或改签名算法。先用电脑端解码工具验证二维码内容。
- 字体乱码和二维码协议无关。本次乱码来自生成字体使用压缩位图，板端显示异常；已改成非压缩 LVGL 字体。

## 已验证状态

真机结果：

- OKX 静态转账签名成功。
- OKX 动态二维码签名成功。
- 签名后能跳出确认和签名结果。
- 屏幕中文不再满屏乱码。

本地资产：

```text
/home/ak/456/photo_2026-05-23_12-10-08.jpg
/home/ak/456/video_2026-05-23_14-34-22.mp4
```

本地验证结果：

```text
OKX 图片 + 1 fps mp4 抽帧：12/12 可被 ZBar 识别
全帧压力抽样：256/257 可被 ZBar 识别，唯一失败帧是过渡/模糊帧
OKX 动态 UR：收齐 5 片后得到 type=eth-sign-request，CBOR 长度 809 字节
```

## 关键代码位置

扫码图像层：

- `main/qr/scanner.c`
- `components/zbar_qr/`
- `components/k_quirc/`

UR 拼包和格式识别：

- `main/qr/parser.c`
- `main/qr/parser.h`
- `components/cUR/`

Web3 请求解析和签名：

- `main/pages/scan/scan.c`
- `web3_parse_eth_sign_request_cbor()`
- `web3_build_eth_signature_ur()`

字体和多语言：

- `tools/bake_signer_cn_fonts.py`
- `main/ui/assets/signer_cn_20.c`
- `main/ui/assets/signer_cn_28.c`
- `main/i18n/i18n.c`

## 这次的症状和判断

| 现象 | 正确判断 | 不要误判成 |
| --- | --- | --- |
| OKX 圆点码完全没反应 | 图像解码器没认出 QR | OKX 协议不支持 |
| 底部没有进度 | 没有拿到任何 payload | Web3 解析失败 |
| 底部进度变化但不跳签名 | 已识别 QR，继续查 UR 拼包和 CBOR 解析 | 摄像头坏了 |
| 普通码也不灵敏、数字很难跳 | 先查摄像头焦距、镜头脏污、距离和反光 | 继续刷协议修复 |
| 静态转账图能签名 | 签名主流程是通的 | 所有动态码都已稳 |
| 动态码偶尔成功但很慢 | 解码任务太重或帧率太低 | 签名算法错 |
| 刷完黑屏但串口还启动 | 先查显示初始化和启动日志 | 直接判定没刷进去 |
| 满屏乱码 | 字体生成或 LVGL 字体配置问题 | 二维码协议问题 |

## 首次使用先做摄像头对焦

Waveshare ESP32-P4 4.3 开发板的 OV5647 摄像头不是手机自动对焦。第一次装好摄像头、换排线、装外壳或移动摄像头位置后，先做这一步，再排查固件。

对焦步骤：

1. 打开 `自检 -> 外设 -> 相机` 或任意扫码页面。
2. 在手机或电脑上显示一个普通黑白二维码，先不要用 OKX 圆点动态码。
3. 距离保持 10-20 cm，屏幕亮度调高，避开反光。
4. 捏住摄像头镜头外圈，小幅旋转镜头，让二维码黑白边缘和小格子最清楚。
5. 不要拧排线，不要扯摄像头主板，不要让金手指松动。
6. 普通二维码能稳定识别后，再测 OKX、Bitget、TokenPocket 这类高密度动态码。

判断标准：

| 结果 | 说明 |
| --- | --- |
| 普通二维码很快识别 | 焦距基本可用，可以继续测 Web3 |
| 普通二维码肉眼看着清楚但设备无反应 | 继续微调焦距、距离和角度 |
| 只有靠很近或很远才清楚 | 焦点不在日常扫码距离，必须重新调 |
| 一动摄像头就黑屏或花屏 | 先查 FFC 排线插紧、方向和锁扣 |

这次真正跑通前，摄像头通过手动旋转镜头后，OKX 静态图和动态码才进入可稳定识别范围。以后不要跳过这一步。

## 圆点码的正确处理方式

OKX 这类码看起来像圆点矩阵，但内容仍然可以被标准 QR 解码器读出。电脑端能直接得到：

```text
ur:eth-sign-request/...
```

因此优先路线是：

1. 保留 `quirc`，用于普通方块二维码。
2. 增加 `ZBar` 作为 fallback，用于圆点、高密度、边缘更难识别的二维码。
3. 摄像头预览可以低分辨率，但解码帧要用更高分辨率裁剪。
4. 对圆点码做有限的暗色膨胀增强，不要每帧暴力跑太多次。
5. 扫码页减少渲染负担：少文字、少按钮、少动画，保留必要进度提示即可。

不要优先做这些事：

- 不要重新发明 OKX 专用二维码协议。
- 不要先改 EIP-712、交易哈希或签名回传。
- 不要只看手机屏幕“很清晰”就断定协议错；开发板摄像头和手机扫码能力不是一个级别。

## 桌面端先跑通

以后遇到新的钱包二维码，先把图片或录屏放到本地，再电脑端跑通。

从 mp4 抽帧：

```bash
rm -rf /tmp/okx_video_frames
mkdir -p /tmp/okx_video_frames
gst-launch-1.0 -q uridecodebin uri=file:///home/ak/456/video_2026-05-23_14-34-22.mp4 \
  ! videoconvert ! videorate ! 'video/x-raw,framerate=1/1' \
  ! pngenc snapshot=false ! multifilesink location=/tmp/okx_video_frames/frame_%03d.png
```

跑 ZBar wrapper 测试：

```bash
cd /home/ak/123/Kern
gst-launch-1.0 -q filesrc location=/home/ak/456/photo_2026-05-23_12-10-08.jpg \
  ! jpegdec ! videoconvert ! pngenc snapshot=true ! filesink location=/tmp/okx_photo.png
components/zbar_qr/test/build/zbar_qr_test /tmp/okx_photo.png /tmp/okx_video_frames
```

预期至少看到：

```text
Summary: 12/12 decoded
```

如果 `zbarimg --raw` 都读不出，先处理图片质量、缩放、曝光、反光、裁剪，不要改固件协议。

## UR 拼包验证

OKX 动态码不是扫到一片就完成。本次样本是 5 片 fountain UR，日志里会看到类似：

```text
ur:eth-sign-request/50-5/...
ur:eth-sign-request/54-5/...
ur:eth-sign-request/59-5/...
```

正确结果是：

```text
format=FORMAT_UR
total=5
complete=1
type=eth-sign-request
cbor_len=809
```

如果图像层已经能稳定输出 `ur:eth-sign-request/...`，但真机不跳签名，优先查：

1. `qr_parser_get_ur_result()` 是否返回 true。
2. `ur_type` 是否等于 `eth-sign-request`。
3. `web3_parse_eth_sign_request_cbor()` 是否失败。
4. 当前入口是否是 `扫码签名` / Web3 unified scan，还是普通二维码工具页。
5. 是否被其他窗口的多语言/UI改动破坏了页面流程。

## 扫码效率坑

本次一度为了识别圆点码，每帧最多跑 6 次圆点膨胀重试。结果是：

- 单帧识别机会变多；
- 但解码任务变重；
- 新帧进不来；
- 动态码反而更难收齐。

当前策略：

- 每帧先跑原图。
- ZBar 在原图灰度帧上先尝试。
- `quirc` 再处理。
- 圆点增强按帧轮换半径，只额外跑一次，不要每帧全半径暴力重试。
- 扫码页面保持轻量，不显示大段说明文字，进度条和状态信息尽量简单。
- 动态二维码要优先保证采帧频率和分片收齐率；不要为了单帧成功率把 CPU 都耗在重试上。
- 对手机小屏幕码，解码区域可以偏小、居中，减少无效画面进入解码器，但不能裁掉二维码静区。

以后调参时先看手感和动态码完成率，不要只看单张截图能不能识别。

## 提高扫码成功率的实操顺序

遇到 OKX、Bitget、TokenPocket 或其他钱包动态码扫得慢，按这个顺序处理：

1. 先确认摄像头焦距：普通二维码必须在 10-20 cm 内稳定识别。
2. 手机亮度调高，关闭深色/护眼导致的低对比显示。
3. 二维码完整露出，周围留白，不要被按钮、边框、刘海或截图裁边挡住。
4. 开发板和手机屏幕保持平行，避免斜扫和反光。
5. 高密度小码先截图或投屏到电脑，放大到屏幕中央再扫。
6. 动态码从第一帧开始保持几秒，不要来回晃动；如果一直缺最后几片，重新从头开始。
7. 如果设备完全没进度，先看图像层和焦距；如果有进度但不跳签名，再看 UR 拼包和 CBOR。
8. 如果桌面端 `zbarimg` 或 `components/zbar_qr/test` 都能识别，板端不行，再调固件扫码性能。

不要把“手机能扫”当成“开发板一定能扫”。手机有自动对焦、HDR、畸变校正和更强的解码器，ESP32-P4 必须靠焦距、光线、解码帧和轻量 UI 一起配合。

## 字体乱码坑

本次多语言字体生成后出现过：

- 开机不稳定；
- 蓝屏/黑屏循环；
- 屏幕满屏乱码；
- 中文显示异常。

根因和处理：

- 生成字体曾经是 `.bitmap_format = 1`，即压缩位图。
- `CONFIG_LV_USE_FONT_COMPRESSED=y` 可以避免缺少解压支持导致崩溃，但板端仍出现乱码风险。
- 已改为 `lv_font_conv --no-compress`，生成 `.bitmap_format = 0`。

每次改 UI 文案或多语言后必须检查：

```bash
cd /home/ak/123/Kern
python3 tools/bake_signer_cn_fonts.py
rg -n "bitmap_format" main/ui/assets/signer_cn_20.c main/ui/assets/signer_cn_28.c
idf.py -B build_wave_43_fresh build
```

预期：

```text
signer_cn_20.c: .bitmap_format = 0
signer_cn_28.c: .bitmap_format = 0
```

如果重新打开字体压缩，必须重新做真机中文显示验收，不能只看编译通过。

## 刷机和启动确认

不要用“屏幕黑了”直接判断有没有刷进去。刷机后必须看这三件事：

1. esptool 是否写入和 hash verify 都成功。
2. 是否 `Hard resetting via RTS pin`。
3. 启动串口是否进入 `app_main()` 并显示屏幕初始化成功。

可接受的启动日志关键字：

```text
Display initialized
LVGL task started successfully
KSIG_MAIN: Display initialized successfully
main_task: Returned from app_main()
```

如果黑屏但串口正常，查显示、背光、页面初始化。
如果串口反复从 ROM boot 开始，查崩溃和 watchdog。
如果刷机命令没有 hash verified，先不要让用户测试。

## 本次最终刷机前检查

本次有效刷机前做过：

```bash
cd /home/ak/123/Kern
source /home/ak/esp-idf-v5.5.4/export.sh
idf.py -B build_wave_43_fresh build
components/zbar_qr/test/build/zbar_qr_test /tmp/okx_photo.png /tmp/okx_video_frames
```

固件体积：

```text
kernsigner.bin size after final rebuild: 0x3b0130
factory app partition: 0x400000
free after final rebuild: 0x4fed0
```

刷机口：

```text
/dev/ttyACM0
```

刷机成功后再抓启动日志。不要只靠用户肉眼反馈“亮没亮”。如果电脑当前看不到 `/dev/ttyACM0`、`/dev/ttyUSB*` 或实际串口，先让开发板重新枚举，不要把还没刷入的构建写成“已刷”。

## 以后遇到同类问题的顺序

1. 先确认摄像头物理对焦，普通二维码必须能稳定扫。
2. 问清楚是“完全没反应”、“有进度但不完成”，还是“完成后不跳签名”。
3. 让用户提供静态照片和动态录屏，放到 `/home/ak/456` 或明确路径。
4. 电脑端先用 `zbarimg` 或 `components/zbar_qr/test` 跑通。
5. 能解出文本后，确认是不是 `ur:eth-sign-request`。
6. 再用 `qr_parser` 验证 UR 是否能完成拼包。
7. 只有 UR 完整后，才查 `web3_parse_eth_sign_request_cbor()`。
8. 静态转账能签时，不要再怀疑整个签名主流程。
9. 改完字体或多语言，必须重新烘焙字体并确认 `.bitmap_format = 0`。
10. 完整构建通过、体积没超分区、桌面样本通过后再刷。
11. 刷完必须确认 esptool hash 和启动串口日志。

## 绝对不要再做

- 不要把圆点二维码直接定性为“非标准二维码协议”。
- 不要在图像层没解出 payload 前改 Web3 CBOR 或签名算法。
- 不要跳过 ESP32-P4 4.3 摄像头首次手动对焦检查。
- 不要每次猜一个方向就刷一版。
- 不要忽略用户提供的真实图片和 mp4。
- 不要让字体压缩改动和扫码协议改动混在一起排查。
- 不要在另一个窗口改多语言时，同时盲刷扫码修复版。
- 不要只测普通二维码就宣称 OKX 动态码已修好。
- 不要用真实资金做首轮回归；确认流程后也要让用户自己最后核对金额、地址和链。
