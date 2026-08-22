# 交接笔记

多个 agent 会话共用这个仓库、彼此不共享上下文，这里是它们之间的公共便签。规则见 [`AGENTS.md`](AGENTS.md)。

**开工先读这里。收工前更新。条目带日期、写短、新的放最上面。不再成立的直接删掉——过期的交接笔记比没有更糟。**

某条笔记如果已经不只是"此刻的状态"、而变成了通用经验，挪到 [`wiki/`](wiki/README.md) 去。

---

## 2026-08-22 · 图片“不倒翁”、性能优化与设置开关已完成，待真机复测

图片页现在以约 30 Hz 单次读取 BMI270 的加速度和陀螺仪数据，用屏幕平面重力角与 `gyroZ` 互补融合，反向旋转前景图片和 fit 模式的 520×520 预模糊背景；离开图片页即停止采样。接近平放（平面投影 `<0.35 g`）时按用户选择立即回零。LVGL 的 `CONTAIN/COVER` 会清除 rotation，因此图片布局已改为等价的自然尺寸居中加显式 scale；旋转只遍历根节点的 image 类，按键 icon、veil 和提示层不动。

设备设置主页已新增“自动旋转”原生开关，默认开启并以 `board/auto_rotate` 持久化；关闭时立即归零并重置姿态滤波器，图片页停止 IMU 采样且主循环回到 100 ms 周期，重新开启后首帧重建绝对姿态。设置主页的 6 行内容放进纵向滚动区域，保存按钮保持固定。首次真机发现“旋转”缺字，现已补进 `bajji_font_16` 子集；软件旋转也已统一关闭抗锯齿，减少每像素源数据读取。

host tests 与 ESP-IDF 6.0 完整构建通过，固件大小 `0x238510`。尚需真机复查开关触摸、滚动、字形、重启持久化、实际 FPS，以及 0°/90°/180°/270° 的世界向上效果；若方向相反只调 `board_math.hpp` 的 `gyro_direction`/安装偏移常量。

## 2026-08-22 · `04 Hi‑Fi UI` 已按 taste review 修正，未改程序代码

Figma 节点 `228:2` 已直接修稿：Device 流程补齐 D02/D03/D06/D07，现在连续覆盖 D01–D11；全部 28 个 iPhone 屏幕补齐状态栏、Home Indicator 和复用现有 Chevron 资产的返回导航；D05、Dark Network 及新增 Wi‑Fi 安全状态的 trailing 胶囊不再溢出。Settings 的 31 个列表行已移除青色外发光，三组首页改成 inset grouped list；图片传输页区分已完成/正在发送/待处理并提供安全取消；解除绑定改成系统式 bottom action sheet，明确重新授权与原图保留结果。

Figma QA：28/28 屏幕为 402×874，Device Row 11 个状态按顺序完整且无越界，所有状态胶囊无裁切，28 个屏幕各有且仅有一组系统状态栏与 Home Indicator，9 个返回动作均为 96×44 pt，字体仍限于 SF Pro Regular/Semibold/Bold，5 个 Section 无 placeholder，Settings Row 与非主按钮无残余发光。修改后截图保存在 `/tmp/bajji-hifi-fix.5rK0oF/`，并与 review 前证据 `/tmp/bajji-hifi-audit.x09Q6A/` 做了同视图对照。**本轮只修改远端 Figma 与本交接条目，没有修改或验证程序代码。**

## 2026-08-22 · Bajji iOS 高保真视觉稿已新增，未改程序代码

Figma 新增 `04 Hi‑Fi UI` Page（`228:2`），在既有 `03 App UX` 交互与状态模型上整理 24 个 402×874 iPhone 高保真界面：Device & Connectivity（`228:4`）、Images（`228:5`）、Settings（`228:6`）以及 Dark Mode & iOS 26（`228:7`）。浅色/深色均使用现有 Bajji 语义变量，统一 SF Pro、48 pt 设备圆角、卡片层级、原生 Figma Glass + 24 px Background Blur；品牌青仅用于关键操作和活动状态，成功/警告/错误保持独立语义色。图片仍使用现有真实壁纸资产，系统 Photo Picker、VPN 同意和 Wi‑Fi 授权继续标为 iOS 系统边界。

视觉稿参考已加入文件的 Apple `iOS and iPadOS 26` Design Resources；由于该外部 library 当前不允许 MCP 直接 upsert，稿件复用了文件内真实组件并用 Figma 原生材质实现。Figma QA：24/24 屏幕尺寸正确、仅使用 SF Pro Regular/Semibold/Bold、5 个 Section 均无 placeholder、4 个 Screen Row 无越界；已将源 UX `198:39` 与高保真 `233:126` 合并同视图做视觉对照，信息层级与状态内容保持一致。**本轮只修改远端 Figma 与本交接条目，没有修改或验证程序代码。**

## 2026-08-22 · Bajji iOS App UX/UI 已在 Figma 完成，未改程序代码

`03 App UX` 已补齐 iOS 设计系统、3 组可复用组件和 25 个 402×874 高保真交互帧。完整原型覆盖：添加/绑定、Wi‑Fi Infrastructure 系统凭据共享、VPN 备用链路与自动回切、受限恢复、自定义图片的系统 Photo Picker/圆形裁切/传输保护，以及 Bajji 亮度、自动换图、刷新间隔、触感反馈、诊断和解除绑定确认。

主节点：App UX Board `173:358`；Device & Connectivity `194:11`；Images `202:47`；Settings `206:65`。状态与工程交接已写入 `07 · State Model & Handoff`（`173:387`），明确 iOS 26.2+、Wi‑Fi Infrastructure entitlement/地区、真机系统授权页和 `10.77.0.0/30` Packet Tunnel 路由等实现门禁。Figma QA：25/25 屏幕尺寸正确、SF Pro 无异常、触控目标均 ≥44 pt、58 条 prototype reactions、无临时 QA 节点或版面重叠。**本轮只修改远端 Figma 与本交接条目，没有修改或验证程序代码。**

色彩已补做 Light/Dark 自适应收敛：Light 使用深品牌青 `#007A8C`、绿 `#1B7F4A`、琥珀 `#9A5B00`、红 `#C42C3A`，Dark 保留原高明度状态色；实色状态胶囊统一使用自适应 `on-accent` 前景，灰色中性状态仍使用 secondary label。Light 对白底对比度为 5.02–5.57:1，Dark 对黑底为 7.62–13.65:1，语义别名、42 个状态胶囊与 58 条原型交互复检通过。

## 2026-08-22 · Wi-Fi Infrastructure 地域门禁已定位，未改程序代码

Xcode 26.5 iPhoneOS SDK 本身只有 link stub/Swift interface；从 iPhone 17 Pro Max、iOS 26.6
（23G71）的 DeviceSupport 符号缓存确认调用链为 `WiFiInfrastructure` → `CWFInterface` →
`CoreWiFi`。实际资格判定在 `CWFXPCRequestProxy` 的 XPC check-in 路径中，通过客户端 audit token
查询 `OSEligibilityQuery`；高置信度方法名是 `-[CWFXPCRequestProxy __checkin:XPCConnection:]`。
`supportsWiFiNetworkSharing` 只是能力结果，`requestAuthorization()` 是后续用户授权入口。类、实现和
判定行为已由二进制验证；因 dyld shared cache 合并 selector 字符串，方法名映射仍标为高置信度推断。
详情见 [`wiki/wifi-infrastructure-provisioning.md`](wiki/wifi-infrastructure-provisioning.md)。未做 Hook、
绕过、程序代码或设备修改。GitHub 检索只找到 macOS `os_eligibility` 的 Apple Intelligence、Xcode
LLM 和 iPhone Mirroring 案例，没有找到 Wi-Fi Infrastructure/CoreWiFi 专用案例；不能直接复用。
iOS 26.6 `OSEligibility` 还确认 TestFlight（`isBeta`）不会触发全地区开发例外；该例外只走已验证的
非 production、非 beta Development build。

## 2026-08-22 · Figma 已补 hostapd / SoftAP Captive Portal STA 配网流程、交互原型与高保真 UI

`02 Product UX` 的 `Product UX Board` 底部新增 `Section / Wi-Fi Provisioning`（节点 `145:266`）：包含完整配网状态机、4 个设备端状态、5 个浏览器 Portal 状态，以及 AP+STA 会话、Portal API、成功/失败和凭据安全规则。流程采用每次会话随机 WPA2 配网密码，5 分钟超时；STA 关联和 DHCP 期间继续保留 AP/Portal；成功后持久化并延迟 10 秒关闭 AP，失败保留 SSID、清空密码并原地重试。

页面右侧另有 6 个顶层 Portal 原型帧（`164:307`–`164:347`），入口为 `Portal · Wi-Fi 配网` 和 `Portal · 失败重试`：点击 Home-5G → 空密码/禁用按钮 → 点击密码框模拟填写 → 保存并连接 → 1.8 秒连接中 → 成功；失败页可切换 SSID、重填密码并重试。成功勾改为可编辑 SVG，避免字符字形不稳定。

Portal 已完成高保真升级并同步回 `浏览器 Captive Portal` 展示区（`145:272`）：使用现有 app 分组背景、表面、主/次文字、品牌青色和状态色变量；补齐移动浏览器安全地址栏、独立热点行、扫描时间、表单卡片、三步连接进度、网络明细及聚焦的错误恢复提示。Select、Input、Button 仍为 Simple Design System 组件实例。Figma QA 已确认 6 个原型均为 390×800、连线未丢失、无占位 shimmer、无塌缩文本，字体仅为 Noto Sans SC、Montserrat 和组件自带 Inter。

设备端 `Settings Home` 的静态稿与两套可点击状态已新增 `Wi-Fi 配网 / 未配置` 菜单行；6 项设置收进 328×256 的纵向滚动列表，入口固定在首屏第 3 行，原分类、类型、配对、亮度和定时刷新入口均保留。点击进入新的顶层设备原型 `▶ Prototype / Wi-Fi Provisioning`（`219:358`），再点“启动配网”进入 `▶ Prototype / Wi-Fi AP Ready`（`219:365`）；停止配网和 A 键返回均已连线。Figma QA 已确认 4 个 Settings Home 状态均为 466×466、列表可滚动、入口目标一致、旧交互未丢失，新增设备原型无标题遮挡且字体仍限于 Noto Sans SC 与 Montserrat。

`Section / Wi-Fi Provisioning` 原有 5 个展示容器误留了 Figma Frame 默认白底，现已修正：Heading 绑定 `Bajji/Color/Bg Surface`（`VariableID:4:3`），流程、设备状态、Portal 状态与运行规则内容区绑定 `Bajji/Color/Bg Base`（`VariableID:4:2`）。整段复检已无大尺寸未绑定白底、无 placeholder；Portal 页面自身的浅色浏览器 UI 保持不变。

## 2026-08-22 · Wi-Fi Infrastructure 配网与 Wi-Fi 优先链路已真机打通

设备新增 `wifi_link`：保存 Apple Wi-Fi Infrastructure 下发的个人网络配置，取得 IP 后选择 Wi-Fi 默认路由，断开后立即恢复 `10.77.0.2/30` BLE 网桥，并通过 SNTP 补齐不依赖手机的时间同步。配网 payload 走现有加密、已绑定的 GATT service；C/Swift 两端都有同一组长度与安全类型校验。

iPhone 16 Pro Max / iOS 26.6 带日志版本实测：宿主 `requestAuthorization()` 稳定返回 `automatic`，Wi-Fi Sharing Extension 进程已启动，设备侧也出现 `encrypted=1 bonded=1`，但始终没有 GATT 配网写入。对照 Xcode 26.5 官方 ATE 模板，发现 extension `Info.plist` 错用了 `NSBluetoothServices`；现已改为 AccessorySetupKit 要求的 `NSAccessorySetupBluetoothServices`，plist lint 与 iPhoneOS 26.5 完整构建通过。**修复版尚未重新安装真机验证**。宿主日志后半段另出现 `session_accessories=2`，若仍失败需先清理重复配件。

修 key 后的新日志又确认 `CBCentralManager` 初始化会短暂走 `unknown(0) → poweredOff(4) → poweredOn(5)`；宿主原先在 `4` 就清掉 pending 并误报 Bluetooth unavailable，ATE writer 也有相同问题。两处现均在 `.poweredOff` 保留请求并等待后续回调，只对 `.unauthorized/.unsupported` 立即失败；iPhoneOS 完整构建通过，**仍待安装真机验证**。

后续日志确认 Share 已到 `poweredOn(5)` 并开始连接，但此时反复点 Add 会报 `CBManagers active with global permissions`。根因是 Share 创建的全局权限 `CBCentralManager` 仍存活，且 UI 没有互斥两个操作。现已让 Add/Share 共用 `isBusy`，Share 前检查 `CBManager.authorization`（首次由系统弹窗申请，拒绝/受限时提示 Settings），并在 Share 结束或失败时取消连接、释放 manager；Add 继续交给 AccessorySetupKit picker 获取自身授权，避免为它预先创建全局 manager。`swift test` 11/11 和 iPhoneOS 完整构建通过，**仍待重新安装真机验证**。当前 session 已有 3 条 accessory 记录，本改动不会自动清理旧记录。

最新真机日志已经看到 19-byte provisioning GATT write、WPA3 payload 解码与配置保存，证明 ASK/Wi-Fi Infrastructure/ATE/BLE 下发链路已完整打通。随后持续 `reason=211`，ESP-IDF 定义为 AP 被 `threshold.authmode` 过滤；根因是 Apple `securityPolicy` 是允许模式集合，iOS 却优先取最强的 WPA3，使 WPA2/WPA3 transition 网络被当成 WPA3-only。现已改为带密码网络取最低允许模式作为 scan threshold，OWE 按 ESP-IDF 要求使用 open threshold 并保留 `owe_enabled`。另在成功发起连接时取消旧 reconnect timer，避免日志中的 `sta is connecting / ESP_ERR_WIFI_CONN` 竞争；新增 iOS policy 集合、设备 reason 名称/RSSI/auth threshold 日志。host tests、Swift 11/11、iPhoneOS 和 ESP-IDF 完整构建均通过，固件大小 `0x2370a0`；重新安装和烧写后，用户已确认配网与连接逻辑打通。

按本轮调试要求临时开启了明文 Wi-Fi 凭据日志：iOS 仅 `DEBUG` 编译输出；固件由 `CONFIG_BAJJI_WIFI_LOG_CREDENTIALS` 控制，仓库默认关闭，但当前生成的 `device/sdkconfig` 已设为 `y`。日志前缀为 `SENSITIVE debug credentials`。定位完成后应关闭开关并清理已采集日志，避免 SSID/密码泄露。

设备 disconnect reason `534` 为本地主机终止，`531` 为 iPhone 终止，随后都成功重连，不是 Wi-Fi 配网根因。完整观察、排除项和下一步见 [`wiki/wifi-infrastructure-provisioning.md`](wiki/wifi-infrastructure-provisioning.md)。

## 2026-08-22 · 图片页解码结果改成跨页面复用，未编译未烧写

`ProductUI` 新增 `media_revision_`：`show()` 只在壁纸 revision 变了才销毁 `webp_player_` / `still_image_` / `gif_source_` / `blurred_background_`，否则只调 `detach_webp_player()` 摘掉定时器和 widget 记录。`create_webp_player()` 拆出了 `attach_webp_player()`，所以 A 键切 cover/fit、以及设置页往返回图片页，只重建控件，不再重读文件、重解码 WebP、重做 520×520 模糊。代价是在别的页面上也一直占着约 2 MB PSRAM（共 8 MB）。

顺带两处：模糊质量从 `LV_BLUR_QUALITY_PRECISION` 改回默认 AUTO（`lv_draw_sw_blur.c:77-86`，radius 24 时 skip_cnt 从 1 变 2，省 4 倍）；`BoardHal::poll()` 删掉 4 Hz 的 IMU 采样，`status_.imu_sample` 全仓库没有读者，而它每 250 ms 要抢触摸所在的那条 I2C 总线。PMIC/RTC 的 1 Hz 轮询**故意留着**，量小而且加电量/时间显示时就要用。

**host tests 通过；随后的统一检查在混合工作树上跑完了完整 `idf.py build`（1836/1836，exit 0），但没有烧写真机。** 动画 WebP 的 fit 模式、以及 GIF/JPEG 回退路径需要真机各看一眼。

## 2026-08-22 · BLE 15 ms 间隔请求解除 PHY 依赖、TX 队列缩到 16，未编译未烧写

`device/components/ble_link/ble_link.c`：15 ms 连接间隔请求抽成 `request_fast_connection_interval()`，PHY_UPDATE_COMPLETE 的成功/失败两条路径都发，`ble_gap_set_prefered_le_phy()` 同步失败时也立刻发（原来这三种情况会整条连接停在 iOS 默认的约 30 ms）。`kQueueCapacity` 32 → 16，省约 20.6 KB 内部 .bss；`kReceiveBufferCount` 之前已经是 2。

host tests 通过；随后的统一检查在混合工作树上完整 `idf.py build` 通过，编译无恙。**没烧真机**——15 ms 间隔在 1M PHY 老设备上的实际效果、以及 TX 队列缩容后的高压场景要真机看。

## 2026-08-22 · 最新 Figma 设置与异常流程已实现，待烧写真机

提交 `be43db1`、`3cd8d8c` 已实现参数设置主页、分类/类型、配对管理与解除确认、三档亮度、定时刷新预设/自定义间隔、加载取消、配对码失效恢复和手机桥接不可用页面；定时刷新按设备本地持久化并继续通过 BLE 手机网络请求。`ccad04a` 将箭头/对号改为 LVGL 线条绘制，并为中文 16/20/24 px 字体补回退与 `·` 圆点映射；`56ee335` 又将加载页 `bq · eciyuan` 的分隔点改为实体圆点，完全绕开 Montserrat 14 的缺字。图片页实体按键映射仍位于 Figma 指定的顶部位置。host tests 与 ESP-IDF 完整构建通过，app 大小 `0x1ea580`，尚未烧写真机做触摸、实体按键和视觉验收。

## 2026-08-22 · 壁纸代理已切到 Cloudflare Images Worker，待烧写真机

已部署 `cloudflare/image-proxy/worker.mjs` 并绑定自定义域名 `https://bajji-image-proxy.eric3u.cc`，本机 Wrangler OAuth 登录态可继续部署。Worker 用 Images binding 输出 WebP：cover 466×466/q90，fit 328 contain/q85，保留动画，源图或转换失败最多换源三次；固件已改用自定义域名，不再依赖 `workers.dev`。

当前 14 组稳定设置的 cover/fit 生产矩阵 28/28 返回 200 WebP；全部 21 组分类已按用户选择恢复。`general_anime`/`ai_drawing` 在 Worker 内三次重抓后曾有 2/24 次 502，但固件最多用五个新 nonce 全程通过 Worker 重试，不再降级直连原图；恢复的 7 个分类再测 cover/fit 共 14 次，本轮均首次成功。自定义域名 cover/fit 冒烟测试 2/2 返回 200 WebP；切换后的 Wrangler dry-run、Worker 单测、固件 host tests 和 ESP-IDF 完整构建均通过。结论见 [`wiki/cloudflare-images-uapis.md`](wiki/cloudflare-images-uapis.md)。尚未烧写真机。

## 2026-08-22 · 换图菊花已改为固定弧长匀速旋转，待真机确认观感

`device/components/ui/diagnostics_ui.cpp` 不再使用会让弧长和角速度一起变化的 `lv_spinner`，改为 92° 固定圆弧做 900 ms/圈线性旋转；隐藏时仍会删除无限动画。ESP-IDF 完整构建通过，尚未烧写真机。

## 2026-08-21 · GIF 帧率根因已修，最终代理尺寸待新图实测

真机聚合探针确认当前 240×259 GIF 被 LVGL 放大到 466×466：源帧延迟约 75–100 ms、解码约 15–20 ms/帧，但抗锯齿缩放后只有约 3.7 FPS；关抗锯齿约 4.8–5.2 FPS。根因和排除项已写进 [`wiki/gif-frame-rate.md`](wiki/gif-frame-rate.md)。

代理现在按显示模式输出目标尺寸（cover 466×466，fit 328 contain），避免逐帧缩放。host tests、O2 完整构建通过，最终无探针 app 已烧写并正常启动。**Cloudflare 新代理的最终动画 FPS 还要等刷新出一张新动画再测；旧缓存不会自动重编码。**

## 2026-08-21 · 壁纸分区换成了 FAT，首次开机会重新格式化

`partitions.csv` 里 `wallpaper` 分区的 subtype 从 `spiffs` 改成了 `fat`。**必须整片烧写**（`idf.py flash` 会带上分区表），而且改完第一次开机挂载会失败并自动格式化，缓存的壁纸丢一次。

读那次开机的日志时要把这点考虑进去——之前有一轮就是因为这个，界面根本没走到壁纸页，探针一条都没打出来，白烧了。

背景在 [`wiki/wallpaper-cache-filesystem.md`](wiki/wallpaper-cache-filesystem.md)。

## 2026-08-21 · 并发会话：这个工作区同时有别人在动

这一天里两个会话互相删掉过对方未提交的代码，双向都发生了。事故经过和应对规矩在 [`wiki/concurrent-agent-sessions.md`](wiki/concurrent-agent-sessions.md)。

要点：用锚定的最小改动，别拿旧副本整体重写文件（尤其 `deps.lock.json` 这类），早点提交，改动前后都查 `git status`。
