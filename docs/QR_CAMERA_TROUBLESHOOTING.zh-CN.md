# 扫码与摄像头排障手册

日期：2026-05-23

这份文档是扫码问题的主入口。以后遇到小尺寸高密度二维码、OKX 圆点动态码、TokenPocket/Bitget/MetaMask 等钱包动态码，或者要把这套能力复刻到树莓派摄像头，都先按这里查。

OKX 事故复盘见 [OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md](OKX_QR_SCAN_INCIDENT_20260523.zh-CN.md)。复盘里有完整证据和历史过程，日常排障先看本文。

## 核心结论

OKX 圆点码不是新协议。它看起来像圆点矩阵，但内容仍然是标准 QR，常见 payload 是：

```text
ur:eth-sign-request/...
```

排障顺序固定为：

```text
摄像头成像 -> QR 图像解码 -> UR 分片拼包 -> CBOR/Web3 解析 -> 签名页面
```

图像层还没有解出 payload 时，不要改 Web3 协议、EIP-712、签名算法或回传格式。

## 五分钟判断

| 现象 | 说明 | 先做什么 |
| --- | --- | --- |
| 屏幕完全没有进度 | 没有识别出任何 QR payload | 调焦、调亮度、放大二维码、用桌面 ZBar 验证图片 |
| 有进度但不跳签名 | 图像层已识别，UR/CBOR 可能没完成 | 查 UR 是否收齐、类型是否为 `eth-sign-request` |
| 静态图能签，动态码慢 | 动态分片收齐率不够 | 提高采帧有效率，减少 UI 和每帧重试负担 |
| 只有 OKX 圆点码没反应 | 解码器对圆点/高密度码不稳 | 走 ZBar fallback、高分辨率解码帧、有限圆点增强 |
| 普通二维码也扫不到 | 先别改协议 | 物理对焦、距离、反光、镜头脏污、FFC 排线 |
| 手机能扫、开发板不能扫 | 正常现象 | 手机有自动对焦/HDR/强解码，开发板要靠焦距和算法补 |
| 刷完黑屏但串口正常 | 不是扫码问题 | 查显示、背光、页面初始化和启动日志 |
| 字体乱码 | 不是二维码协议问题 | 查字体烘焙和 `.bitmap_format = 0` |

## 标准排障流程

1. 先扫普通黑白二维码，确认摄像头和基础 QR 解码可用。
2. 普通二维码不稳定时，只查硬件成像：焦距、距离、亮度、反光、镜头和排线。
3. 普通二维码稳定后，再测 OKX、Bitget、TokenPocket 这类高密度动态码。
4. 手机钱包二维码太小或太密时，先截图或投屏到电脑屏幕放大。
5. 动态码要从第一帧开始保持几秒，别半路晃动。
6. 仍失败时，把静态图和 5-10 秒 mp4 放到本地目录，例如 `/home/ak/456`。
7. 电脑端先转 PNG，再用 `components/zbar_qr/test` 跑通。
8. 能解出 `ur:eth-sign-request/...` 后，再查 UR 拼包。
9. UR 完整后，再查 `web3_parse_eth_sign_request_cbor()`。
10. 固件修改后，先跑桌面样本、单测、模拟器验收，再刷机。
11. 刷机后看 esptool hash 和启动串口日志，不靠“亮不亮屏”猜。

## ESP32-P4 摄像头对焦

Waveshare ESP32-P4 4.3 的 OV5647 不是手机自动对焦。第一次使用、换过摄像头、换过排线、装外壳或移动摄像头位置后，都要重新确认焦距。

照做：

1. 打开 `首页 -> 自检 -> 外设 -> 相机`，或打开任意扫码页面。
2. 手机或电脑显示一个普通黑白二维码，先不要用 OKX 圆点动态码。
3. 距离保持 10-20 cm，屏幕亮度调高，避开反光。
4. 捏住镜头外圈，小幅旋转到二维码边缘和小格子最清楚。
5. 不要拧 FFC 排线，不要拉摄像头小板，不要让金手指松动。
6. 普通二维码稳定识别后，再测高密度动态码。

判断：

| 结果 | 说明 |
| --- | --- |
| 普通二维码很快识别 | 焦距可用 |
| 普通二维码肉眼清楚但设备无反应 | 继续微调距离、焦距和角度 |
| 靠很近或很远才清楚 | 焦点不在扫码距离，需要重新调 |
| 一碰摄像头就黑屏或花屏 | 查排线方向、锁扣和供电 |

## 小尺寸高密度码

手机钱包的小屏二维码经常比开发板摄像头能力更吃紧。处理顺序：

1. 手机亮度调高，关闭深色模式、护眼模式和低亮度省电。
2. 二维码完整显示，四周留白，不要被按钮、边框、刘海或截图裁边挡住。
3. 开发板和手机尽量平行，不要斜扫。
4. 距离从 10 cm 慢慢拉到 20 cm，找到最清楚的位置。
5. 二维码太小就截图或投屏到电脑，把二维码放大到屏幕中央。
6. 动态码一直差最后几片时，回到钱包重新从第一帧开始扫。
7. 桌面端都扫不到时，先处理图片质量、缩放、曝光和反光。
8. 桌面端能扫、板端扫不到时，再调固件采帧、裁剪、ZBar/quirc 策略。

## OKX 圆点动态码

OKX 动态码通常是 fountain UR 分片，不是扫到一帧就完成。图像层会先输出类似：

```text
ur:eth-sign-request/50-5/...
ur:eth-sign-request/59-5/...
ur:eth-sign-request/100-5/...
```

正确完成时应看到：

```text
format=FORMAT_UR
complete=1
type=eth-sign-request
```

如果图像层能输出 UR，但不跳签名，按这个顺序查：

1. `qr_parser_get_ur_result()` 是否返回 true。
2. UR type 是否是 `eth-sign-request`。
3. CBOR 长度是否合理。
4. `web3_parse_eth_sign_request_cbor()` 是否失败。
5. 当前是否走了 `扫码签名` 入口，而不是普通二维码工具页。

## 桌面端复现

### 准备 ZBar wrapper

```bash
cd /home/ak/123/Kern
cmake -S components/zbar_qr/test -B components/zbar_qr/test/build
cmake --build components/zbar_qr/test/build
```

### 静态图转 PNG

`components/zbar_qr/test` 当前只读 PNG。如果用户给的是 JPG，先转：

```bash
gst-launch-1.0 -q filesrc location=/home/ak/456/photo_2026-05-23_12-10-08.jpg \
  ! jpegdec ! videoconvert ! pngenc snapshot=true ! filesink location=/tmp/okx_photo.png
```

### 动态 mp4 抽帧

先用 1 fps 验证“能不能稳定读出不同片段”：

```bash
rm -rf /tmp/okx_video_frames
mkdir -p /tmp/okx_video_frames
gst-launch-1.0 -q uridecodebin uri=file:///home/ak/456/video_2026-05-23_14-34-22.mp4 \
  ! videoconvert ! videorate ! 'video/x-raw,framerate=1/1' \
  ! pngenc snapshot=false ! multifilesink location=/tmp/okx_video_frames/frame_%03d.png
```

压力测试可以提高抽帧率，但不要把过渡帧失败当成协议失败。

### 跑解码测试

```bash
components/zbar_qr/test/build/zbar_qr_test /tmp/okx_photo.png /tmp/okx_video_frames
```

本次 OKX 样本结果：

```text
静态图 + 1 fps 视频抽帧：12/12 decoded
全帧压力抽样：256/257 decoded
```

只要能看到 `ur:eth-sign-request/...`，就证明二维码图像层已经通了。下一步才看 UR 拼包和签名解析。

## 固件实现要点

ESP32-P4 当前路线：

1. 摄像头预览可以低分辨率，解码帧使用更高分辨率居中裁剪。
2. ZBar 用于 OKX 圆点码、小尺寸高密度码和边缘更难识别的码。
3. `quirc` 保留，用于普通 QR 和已有流程兼容。
4. 圆点增强只能有限重试，不能每帧所有半径暴力跑。
5. 动态码优先保证新帧进入解码任务，队列不要堆旧帧。
6. 扫码页尽量轻：少文字、少按钮、少动画，只保留必要进度。
7. 进度条变动说明图像层已经识别出 payload；完全没进度才重点查摄像头和解码器。

关键代码：

```text
main/qr/scanner.c
main/qr/parser.c
main/pages/scan/scan.c
components/zbar_qr/
components/k_quirc/
components/cUR/
```

不要把扫码 UI 做复杂。小屏动态 QR 的瓶颈通常是采帧和解码，不是页面好不好看。

## 树莓派复刻清单

树莓派算力更宽裕，可以优先用 ZBar、ZXing-C++ 或 OpenCV 处理图像层，但顺序仍然一样：先证明图片能解码，再接 UR 和 Web3。

1. 确认摄像头预览命令可用。
2. 用普通黑白二维码调焦。
3. 拍一张 OKX 静态图，录一段 OKX 动态码视频。
4. 用 ZBar/ZXing-C++ 在树莓派本机或电脑端先解出 `ur:eth-sign-request/...`。
5. 能解出 UR 后，再接 UR fountain 拼包。
6. UR 完整后，再接 CBOR/Web3 签名解析。
7. 最后再优化速度和 UI。

常用命令：

```bash
rpicam-hello -t 0
rpicam-still -o qr_test.jpg
rpicam-vid -t 8000 -o qr_test.h264
```

如果系统没有 `rpicam-*`，再试：

```bash
libcamera-hello -t 0
libcamera-still -o qr_test.jpg
libcamera-vid -t 8000 -o qr_test.h264
```

树莓派上不要一开始就写业务解析。先证明摄像头输出的图片可以被标准 QR 解码器读出。

## 每次改完必须验收

最低验收：

```bash
cd /home/ak/123/Kern
JOBS=2 tools/signer_delivery.sh build
./scripts/test.sh
tools/signer_delivery.sh check
(cd firmware/wave_43 && sha256sum -c SHA256SUMS.txt)
components/zbar_qr/test/build/zbar_qr_test /tmp/okx_photo.png /tmp/okx_video_frames
```

刷机后必须确认：

```text
Hash of data verified
Display initialized
GT911 found
LVGL task started successfully
KSIG_MAIN: Display initialized successfully
Setting LCD backlight
```

## 绝对不要再踩的坑

- 不要把 OKX 圆点码先判定成“非标准二维码协议”。
- 不要图像层没 payload 就改 Web3 解析。
- 不要跳过摄像头手动对焦。
- 不要只看手机能扫就断定开发板也应该能扫。
- 不要为了单帧成功率让每帧跑太多重试，动态码会更难收齐。
- 不要把字体、多语言、扫码解码三个问题混在一起排查。
- 不要只测普通二维码就宣布 OKX 动态码修好了。
- 不要用真实资金做首轮回归。
