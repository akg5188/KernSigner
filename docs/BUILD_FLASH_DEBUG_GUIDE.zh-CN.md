# 构建、刷机与调试指南

日期：2026-05-21

这份文档给开发者和交付人员看。目标是把“改了代码后怎么编译进去、怎么刷、怎么抓日志、怎么避免刷错板”讲清楚。

## 当前常用目录

本仓库常用路径：

```bash
cd /home/ak/123/Kern
```

如果你在别的窗口编译，先确认是不是同一个目录：

```bash
pwd
rg -n "Kern Documentation" docs/README.md
git status --short
```

如果你看不到刚改的文件内容，说明你不在同一份代码里。

## 常见构建方式

### 使用已有 build 目录增量构建

当前工作区已有 `build/` 时，可以直接：

```bash
cd /home/ak/123/Kern
cmake --build build
```

构建成功后会生成：

```text
build/kernsigner.bin
```

### 使用 ESP-IDF 构建 4.3 寸目标板

先加载 ESP-IDF：

```bash
source /home/ak/esp-idf-v5.5.4/export.sh
```

当前项目只适配：

```text
ESP32-P4-WiFi6-Touch-LCD-4.3
```

构建时固定使用 `wave_43`：

```bash
idf.py -B build_wave_43_fresh \
  -D SDKCONFIG=build_wave_43_fresh/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.wave_43' \
  build
```

不要把 `wave_43` 换成其他板型配置。其他 Waveshare ESP32-P4 屏幕板没有完成本项目适配和验收，直接编译或刷机可能黑屏、触摸错位、相机异常或 UI 布局错误。

## 只检查某个 C 文件是否能编译

改 UI 或页面后，可以用 `build/compile_commands.json` 做语法检查。

示例：

```bash
cd /home/ak/123/Kern
file=/home/ak/123/Kern/main/pages/capture_entropy.c
cmd=$(jq -r --arg f "$file" '.[] | select(.file == $f) | .command' build/compile_commands.json)
eval "$cmd -fsyntax-only"
```

多个文件：

```bash
cd /home/ak/123/Kern
for file in \
  /home/ak/123/Kern/main/pages/capture_entropy.c \
  /home/ak/123/Kern/main/pages/pin/pin_page.c \
  /home/ak/123/Kern/main/pages/krux_shell/krux_shell.c
do
  echo "syntax-check ${file#/home/ak/123/Kern/}"
  cmd=$(jq -r --arg f "$file" '.[] | select(.file == $f) | .command' build/compile_commands.json)
  eval "$cmd -fsyntax-only"
done
```

语法检查通过不等于完整固件通过。出固件前还要跑完整构建。

## 刷机前必须确认

先看 [FLASH_PRECHECK.md](FLASH_PRECHECK.md)。

重点确认：

| 项目 | 怎么确认 |
| --- | --- |
| 目标板 | 必须是 ESP32-P4-WiFi6-Touch-LCD-4.3 |
| 串口 | 下载口通常是 `/dev/ttyACM0`，不要选 OTG 读卡器口 |
| 固件类型 | app-only 不能刷到空白板 |
| NVS | 没有明确要求，不要全擦 |
| 安全定位 | 测试资金版和商业生产版不能混说 |

## app-only 刷机

适合设备已经有正确 bootloader 和分区表，只升级应用。

```bash
cd /home/ak/123/Kern
ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh appflash
```

如果手动用 esptool：

```bash
python3 -m esptool --chip esp32p4 -p /dev/ttyACM0 -b 115200 \
  --before default_reset --after hard_reset write_flash 0x20000 build/kernsigner.bin
```

地址 `0x20000` 只适用于当前分区布局。换分区表前必须重新确认。

## 全量刷机

全量刷机需要 bootloader、partition table、app 等完整镜像。不要把 `kernsigner.bin` 当成 factory 全量包。

如果是发布包，优先按发布包里的说明操作。没有明确 factory 包时，不做全擦。

## 抓串口日志

常用：

```bash
idf.py -B build_wave_43_fresh -p /dev/ttyACM0 monitor
```

或使用交付脚本生成日志：

```bash
cd /home/ak/123/Kern
ESPPORT=/dev/ttyACM0 ESPBAUD=115200 tools/kern_delivery.sh bootlog
```

日志保存到 `docs/logs/` 后，要在验收记录里写明文件名。

## 模拟器

模拟器适合看 UI 和截图，不等于真机验收。

```bash
cd /home/ak/123/Kern/simulator
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j"$(nproc)"
./build/kern_simulator --width 480 --height 800
```

截图验收可以用：

```bash
cd /home/ak/123/Kern
tools/kern_delivery.sh sim
tools/kern_delivery.sh screenshots
tools/kern_delivery.sh verify
```

## 交付脚本

常用命令：

| 命令 | 用途 |
| --- | --- |
| `tools/kern_delivery.sh build` | 构建 4.3 寸验收固件 |
| `tools/kern_delivery.sh check` | 跑交付检查 |
| `tools/kern_delivery.sh final` | 生成最终交付材料 |
| `tools/kern_delivery.sh appflash` | app-only 刷机 |
| `tools/kern_delivery.sh prodcheck` | 生产门禁检查 |

生产门禁失败并不代表开发构建失败。它表示还不能宣传为商业真钱包。

## 代码改了但没编译进去

按这个顺序查：

1. `pwd` 是否是 `/home/ak/123/Kern`。
2. `rg` 能不能搜到你刚改的文字。
3. 另一个窗口是不是用了另一个 build 目录。
4. 是否只编译了 simulator，没有编译固件。
5. 是否刷的是旧发布包，不是当前 `build/kernsigner.bin`。
6. 是否 app-only 刷到了错误分区地址。
7. 刷完是否设备实际重启。

最短确认命令：

```bash
cd /home/ak/123/Kern
rg -n "本机助记词|拍照生成随机熵|pin_unlock_textarea_y" main docs
cmake --build build
sha256sum build/kernsigner.bin
```

刷机后记录新的 SHA256。

## 出固件前检查

最低要求：

```bash
cd /home/ak/123/Kern
git diff --check
cmake --build build
tools/kern_delivery.sh prodcheck
```

如果只是测试资金验收版，`prodcheck` 可以失败，但必须在交付说明里写清楚“不是商业生产版”。

商业生产版必须：

- 生产安全配置通过。
- 工作区干净且有可追溯 commit。
- Secure Boot / Flash Encryption / NVS encryption 流程记录完整。
- 真机验收记录完整。
- 发布包 SHA256 和版本号写入文档。
