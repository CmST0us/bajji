# Bajji — agent 指南

> `CLAUDE.md` 和 `AGENTS.md` 内容完全一致。改动请两个文件一起改，或者改完一个复制过去。

## 这个项目是干什么的

Bajji 让 M5Stack StopWatch 通过 iPhone 上网。手表除了 BLE 没有别的射频，所以由手机跑一个 Packet Tunnel 扩展，把 IPv4 承载在 LE credit-based L2CAP 通道上。有了这条链路，手表就能自己去拉壁纸显示。

三块：

- **`device/`** —— StopWatch 的 ESP-IDF v6.0 固件（ESP32-S3R8，16 MB flash，8 MB OPI PSRAM @ 80 MHz）。负责 BLE 从机、IP bridge 端点（`10.77.0.2/30`）、LVGL 界面和壁纸缓存。
- **`ios/`** —— SwiftUI 宿主 App 加一个 `PacketTunnel` 网络扩展。CoreBluetooth 只归扩展管，路由只处理 `10.77.0.0/30`。
- **`protocol/`** —— 两端共用的线协议，规范在 [`protocol/bridge-v1.md`](protocol/bridge-v1.md)，附测试向量。

其余目录：`docs/`（硬件说明、验证模板）、`wiki/`（工程经验，见下）、`scripts/`、`tools/`。

## 硬件

M5Stack StopWatch —— <https://docs.m5stack.com/en/core/StopWatch>

| 部件 | 说明 |
|---|---|
| SoC | ESP32-S3R8，240 MHz，OCT PSRAM |
| 屏幕 | 圆形 AMOLED，CO5300 走 QSPI，按 468×468 驱动，RGB565 |
| 触摸 | I2C，地址 `0x15` |
| IO 扩展 | M5IOE1 —— 屏幕/触摸复位、电源轨、马达 PWM |
| PMIC | M5PM1 —— 电池、充电状态 |
| IMU | BMI270 + BMM150 |
| 音频 | ES8311 走 I2S，功放由 IO 扩展控制 |
| RTC | I2C，地址 `0x32` |

除屏幕外**全部挂在同一条 I2C 总线**（`I2C_NUM_0`）上，而且是多个任务在用。见 [`wiki/shared-i2c-bus.md`](wiki/shared-i2c-bus.md)。

## `device/` 的结构

| 路径 | 内容 |
|---|---|
| `main/` | `app_main.cpp`，顶层循环 |
| `components/board_hal/` | 硬件初始化、屏幕与 LVGL 接线、触摸、按键、电量 |
| `components/ble_link/` | NimBLE 从机、配对绑定、L2CAP CoC |
| `components/bridge_protocol/` | Bridge v1 帧格式，和 host 测试共用 |
| `components/ip_bridge/` | 架在 CoC 上的 lwip netif |
| `components/ui/` | LVGL 界面（`diagnostics_ui.cpp`） |
| `components/wallpaper/` | 壁纸的下载、校验、缓存、解码 |
| `components/webp_decoder/` | libwebp 封装 |
| `vendor/` | 固定版本的第三方源码，**已 gitignore，是生成物** |
| `patches/` | checkout 之后打到 `vendor/` 上的补丁 |
| `tests/host/` | 主机侧单元测试，不需要硬件 |

## 常用命令

```sh
# 固件
cd device
python3 tools/fetch_deps.py            # 拉起 vendor/，干净 checkout 后必须先跑
idf.py build
idf.py -p <PORT> flash monitor         # 会同时烧 bootloader、分区表和 app

# 主机侧测试，不需要硬件
bash device/tests/host/run.sh

# iOS
python3 ios/tools/fetch_forwarder_deps.py
swift test --package-path ios
xcodebuild -project ios/Bajji.xcodeproj -scheme BajjiBridge \
  -configuration Debug -sdk iphoneos -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO build
```

`device/sdkconfig` 是生成的，已 gitignore；`device/sdkconfig.defaults` 才纳入版本管理。改 Kconfig 要落在 `sdkconfig.defaults` 里，同时手动同步进生成的 `sdkconfig`，否则不重新 configure 就不生效。

## 约定

**注释和文档一律用英文**，哪怕这个文件里的 UI 字符串是中文。风格上跟着周围代码走——注释密度、命名、写法都是。（`wiki/` 和本文件是例外，面向人读，用中文。）

**别改了 `device/vendor/**` 就完事。** 那个目录是 gitignore 的生成物。要么还原，要么打包成补丁并登记进 `deps.lock.json`。见 [`wiki/vendor-patching.md`](wiki/vendor-patching.md)。

**动了 `partitions.csv` 就必须整片烧写**，只烧 app 不行，而且通常会导致数据分区在首次开机时被重新格式化。

**不显然的东西要写注释，并且给出处。** 这个代码库里有相当一部分是针对 LVGL、ESP-IDF 或 M5GFX 某一行行为的规避。一个光秃秃的修复等于在邀请后人把它改回去。写清楚 `file.c:line`，以及不这么做会坏在哪。

能在主机上测的就用 `device/tests/host/`，几秒钟跑完。

## 规则：把值钱的经验写进 `wiki/`

某个东西查得很费劲，那就在继续往下做之前，把它写进 [`wiki/`](wiki/README.md)。判断标准是：**这篇要是早就存在，能不能省下我刚才花掉的时间？**

值得写的：

- 试了好几轮才找到的根因
- 光读源码看不出来的平台行为——尤其是那种"必须实测、推不出来"的
- 容易违反、而且违反了还静默失效的约定

写法是：先写观察到的现象，再写根因（带 `文件:行号`），最后写改法。**把排除掉的假设和排除方式也写上**——下一个人多半会想到同样的错误方向。验证过的和推测的要分开写，一篇过度自信的文档比没有更糟。

changelog、任务清单、以及只跟这一次会话有关的东西，不要放 `wiki/`，放 `HANDSOFF.md`。

## 规则：交接笔记写进 `HANDSOFF.md`

这个仓库同时有好几个 agent 会话在动，彼此不共享上下文。[`HANDSOFF.md`](HANDSOFF.md) 就是它们之间的公共便签。

**开工时先读它。收工前更新它；工作区被你留成了别人可能会误读的状态时，也随手更新。**

该写进去的：

- 进行到一半的事，还差什么，包括任何只做了一半的改动
- **编译不过或者跑不起来的状态，以及到底缺什么。** 这条最有价值
- 你加的临时插桩，以及加在哪——尤其是 `device/vendor/**` 里的，顶层 `git status` 看不见
- 还没提交、被别人覆盖就没了的东西
- 烧一轮固件才换来的实测结论——日志、测出来的数、排除掉的假设
- 你**故意**没碰的地方，以及为什么

条目带日期、写短、新的放上面。不再成立的条目直接删掉——过期的交接笔记是有害的。某条笔记如果已经不只是"此刻的状态"、而变成了通用经验，那就挪到 `wiki/` 去。

## 关于并发会话

如果另一个会话可能在动同一个工作区，就当它一定在动。优先用锚定的最小改动而不是整文件重写，早点提交好让 git 能兜底，一批改动前后都查一下 `git status`。**覆盖了别人的东西要立刻说出来**——对方可能还能原样吐回来。完整记录见 [`wiki/concurrent-agent-sessions.md`](wiki/concurrent-agent-sessions.md)。
