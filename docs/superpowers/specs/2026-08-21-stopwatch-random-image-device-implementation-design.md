# Bajji StopWatch 随机图片设备端实现设计

- 日期：2026-08-21
- 状态：已确认
- 目标设备：M5Stack StopWatch / ESP32-S3R8 / 16 MiB Flash / 8 MiB PSRAM
- SDK：ESP-IDF 6.0、LVGL 9.5
- Figma：<https://www.figma.com/design/hQ9AKyXEgtDniZU2h29oYf>
- UI/UX 基线：`docs/superpowers/specs/2026-08-20-stopwatch-random-image-ui-ux-design.md`
- 图片接口：<https://uapis.cn/docs/api-reference/get-random-image>

## 1. 目标

在现有 `device/` 固件上完整实现已确认的 StopWatch 产品 UI 与交互：

1. 启动时检查持久化配对状态；未配对时引导用户在手机端发起配对。
2. 手机发起安全配对时显示六位配对码；配对成功后进入设置或图片页。
3. 在设备端选择、保存并持久化 UAPI `category/type` 参数。
4. 所有公网请求继续通过现有 BLE L2CAP CoC 与 iPhone Packet Tunnel 转发；设备不使用 Wi-Fi。
5. 缓存并显示 JPEG、PNG、GIF、静态 WebP 和动画 WebP。
6. 图片页仅保留 Cover 与 Fit+Blur 两种显示模式。
7. KEY A 切换模式，KEY B 刷新，A+B 同时保持 1000 ms 返回设置。
8. 图片页默认纯图片；触摸后显示与实体按键位置、颜色一致的图标控件，3000 ms 后自动隐藏。

本次不增加图片历史、收藏、账号、自定义 URL、定时换图、Wi-Fi 配网或面向用户的内部诊断页。

## 2. 已有能力与实施原则

现有固件已经提供：

- StopWatch 显示、触摸、A/B GPIO、振动、RTC、电源与 LVGL HAL；
- NimBLE Secure Connections、持久化单 Bond、六位显示码与 L2CAP CoC；
- point-to-point lwIP 默认网卡和 iPhone Packet Tunnel 转发；
- `esp_http_client`、证书包、SPIFFS 临时文件与缓存回退；
- Host tests 和 ESP-IDF 6.0 构建流程。

实现采用原位升级，不重写上述稳定基础设施：

- `board_hal` 补齐 A/B 事件，不创建第二套 GPIO 驱动；
- `ble_link` 暴露已存 Bond，不改变当前安全模型和 CoC 协议；
- `wallpaper` 保留后台 task、状态快照和缓存职责，替换 Bing 专用逻辑；
- `ui` 用产品 UI 替换诊断主页；
- `app_main` 只协调快照与事件，不承载下载、解码或页面布局。

## 3. 架构

### 3.1 `board_hal`

HAL 每次 `poll()` 同时采样 KEY A 与 KEY B，并输出一次性事件快照：

- `a_pressed`：A 单键动作；
- `b_pressed`：B 单键动作；
- `chord_started`：两键在 120 ms 窗口内形成组合；
- `chord_progress_ms`：组合保持进度，最大 1000；
- `chord_completed`：同时保持达到 1000 ms；
- `chord_cancelled`：达到阈值前释放任一按键。

形成组合后抑制同一轮按压的 A/B 单键事件。释放全部按键后才能开始下一轮。该状态机保持为纯 C++、无 FreeRTOS 依赖，供 Host test 直接验证。

### 3.2 `ble_link`

`ble_link_status_t` 区分：

- `has_bond`：NimBLE store 中存在持久化配对记录，与当前是否连接无关；
- `connected/encrypted/bonded`：当前 GAP 连接状态；
- `passkey`：当前显示码，非配对阶段为 0；
- `bridge_ready`：安全连接、CoC 与应用 HELLO 已完成。

启动同步完成后读取 Bond store 并更新 `has_bond`。只有当前连接同时为 `encrypted && bonded` 才触发配对成功 UI。手机暂时断开不会清除 `has_bond`。现有 repeat-pairing 与手机发起的 clear-bond 流程继续用于恢复配对。

### 3.3 `wallpaper`

该组件成为随机图片设置、下载、缓存和媒体元数据的唯一所有者：

- NVS 读取、校验和保存设置；
- 构建 UAPI URL；
- 等待手机网络桥就绪；
- 后台 HTTPS 下载和格式校验；
- 临时文件、正式文件和原子替换；
- 向 UI 提供固定大小、线程安全的 `WallpaperStatus` 快照。

组件只接受 `wallpaper_set_online(ip.link_up && ip.time_valid)` 提供的可用状态，不初始化 Wi-Fi，也不创建其他网络接口。

### 3.4 `ProductUI`

产品 UI 持有单一页面状态机：

```text
startup
  -> unpaired -> pairing_code -> pairing_success
  -> settings -> category_picker / type_picker
  -> first_load / refreshing
  -> image
  -> no_cache_error
```

图片页另有两个正交状态：

- `display_mode`：`cover` 或 `fit_blur`；
- `controls_visible`：触摸后显示，3000 ms 空闲后隐藏。

UI 只消费 Board、BLE、IP 与 Wallpaper 快照以及一次性按键事件。UI 不进行 HTTP、NVS 或文件操作。

### 3.5 `app_main`

主循环保持 100 ms 协调周期：

1. `board.poll()`；
2. 获取 Board/BLE/IP/Wallpaper 快照；
3. 用 `ip.link_up && ip.time_valid` 更新 Wallpaper 在线状态；
4. 在 LVGL mutex 内将快照和按键事件交给 `ProductUI`；
5. 保留必要的串口链路统计日志。

## 4. 本地数据

### 4.1 NVS

使用独立 namespace `bajji_ui`：

| Key | 类型 | 说明 |
| --- | --- | --- |
| `configured` | `u8` | 用户是否保存过设置 |
| `category` | string | 空串表示全局随机 |
| `type` | string | 不支持子分类时为空串 |
| `display_mode` | `u8` | 0=Cover，1=Fit+Blur |

首次打开设置时显示 `bq/eciyuan` 作为草稿，但在用户点击“保存并获取图片”前 `configured` 仍为 false。一次保存使用同一 NVS handle 写入全部字段并执行一次 commit；失败时不发起请求。

允许的参数固定为：

- 主分类：空、`acg`、`landscape`、`anime`、`pc_wallpaper`、`mobile_wallpaper`、`general_anime`、`ai_drawing`、`bq`、`furry`；
- `bq`：`xiongmao`、`waiguoren`、`maomao`、`ikun`、`eciyuan`；
- `acg`：`pc`、`mb`；
- `furry`：`z4k`、`szs8k`、`s4k`、`4k`。

其他分类提交时强制清空 `type`。URL 只能由该白名单生成，不接受任意用户字符串。

### 4.2 SPIFFS

将 `wallpaper` 分区扩大到 8 MiB。压缩媒体单文件上限为 3 MiB，确保正式文件和临时文件可同时存在并留出文件系统余量。

缓存文件不依赖 URL 扩展名。下载完成后先验证临时文件，只有验证成功才将旧正式文件改名为回退文件并原子替换；替换成功后立即删除回退文件。断电或失败恢复时优先选择最后一个可完整验证的文件。

## 5. 网络数据流

唯一网络路径为：

```text
esp_http_client
  -> ESP32 lwIP 默认 point-to-point netif
  -> ip_bridge
  -> NimBLE L2CAP CoC
  -> iPhone Packet Tunnel
  -> 公网 / UAPI
```

约束：

- 不启用或初始化 Wi-Fi；
- 只有 IP link up 且时间已同步时才允许 HTTPS；
- 请求入口为 `https://uapis.cn/api/v1/random/image`；
- UAPI 返回 302，允许最多 5 次重定向以兼容外部图床链路；
- 每次跳转继续要求 HTTPS，拒绝降级到 HTTP；
- 始终启用 ESP certificate bundle，不关闭证书和主机名校验；
- 请求超时 15 秒，响应体流式写入临时文件；
- 不设置周期性自动换图。

保存设置、启动时已有设置但无缓存、以及 KEY B 会创建一次刷新请求。若请求仍在进行，新的 B 操作只触发已有加载反馈，不创建并发请求。

网络恢复后，仅尚未真正发出的“等待网络”请求自动继续；已经收到明确 DNS、TLS、HTTP 或媒体错误的请求等待用户按 B 重试。有缓存时恢复网络不会自动替换当前图片。

## 6. 媒体格式与渲染

### 6.1 格式识别

媒体类型由文件头判断：

- JPEG：SOI、段结构、SOF 尺寸与 EOI；
- PNG：签名、IHDR 尺寸；
- GIF：GIF87a/GIF89a、画布尺寸；
- WebP：RIFF/WEBP、VP8/VP8L/VP8X；VP8X animation flag 区分静态与动画。

`Content-Type` 只用于提前拒绝明显非图片响应，不能替代文件头校验。所有宽高必须非零；压缩文件不得超过 3 MiB；解码前必须证明所需缓冲区落在配置上限内。

### 6.2 解码器

- JPEG：复用 LVGL TJPGD，按屏幕目标尺寸降采样；
- PNG：启用 LVGL LodePNG，仅在计算后的解码内存满足上限时打开；
- GIF：启用 LVGL AnimatedGIF，内部 framebuffer 使用 RGB565；
- 静态 WebP：引入固定版本的官方 libwebp decoder，并缩放到屏幕目标尺寸；
- 动画 WebP：使用同一固定版本的 libwebp demux/animation decoder，按原始帧时序循环播放。

libwebp 是完成 WebP 要求所需的唯一新增媒体依赖，通过现有 `deps.lock.json` 与 `tools/fetch_deps.py` 固定来源和版本，不在构建时下载浮动代码。

动态 WebP 原始 RGBA 画布上限为 3 MiB；GIF RGB565 画布上限同为 3 MiB。超过上限的媒体按“不支持当前尺寸”处理，不清空旧图片。

### 6.3 显示模式

所有格式对 UI 暴露当前完整帧：

- Cover：保持比例并居中裁切，填满 466×466 圆屏；
- Fit+Blur：前景保持比例完整显示；背景从同一当前帧降采样为约 1/4 尺寸，Cover 铺满后应用高斯模糊。

静态图只生成一次低分辨率背景。动画仅在帧变化时更新背景，背景更新频率不高于前景帧率。模式切换保留当前动画帧和播放时序，不重新发起网络请求。

新媒体首帧完成解码后才增加 revision；UI 保持旧媒体，在 180 ms 交叉淡入后再释放旧解码资源。动画后续帧按文件时序播放。

## 7. UI 与视觉实现

- 设备画布：466×466，圆形裁切；关键文本和触控目标位于中心 328×328 安全区。
- 背景：`#05070C`；表面：`#0B1522` / `#101A27`；主文字：`#F4F8FF`；次文字：`#A8B4C7`；强调：`#54D7FF`。
- 图片控制 A：屏幕上沿左侧，从圆周向内延伸，`#FFC52F`，仅显示模式图标。
- 图片控制 B：屏幕上沿右侧，从圆周向内延伸，`#2F8FFF`，仅显示刷新图标。
- 控件触控目标不小于 48×48；显示后 3000 ms 空闲，以 180 ms 淡出。
- 中文使用应用所需字形子集的 Noto Sans SC/Source Han Sans SC；数字和英文使用 Montserrat。配对码按 `123 456` 分组并使用等宽布局。
- 配对成功保持 800 ms；页面转场 200 ms；图片切换 180 ms；错误 toast 保持 2000 ms。

页面行为：

- Startup：显示启动与配对检查，不接受操作；
- Unpaired：显示手机连接引导，A/B 单键无动作；
- Pairing Code：不可关闭，显示六位码和等待状态；
- Pairing Success：绿色成功反馈后按本地设置分流；
- Settings：分类、子分类、保存按钮和本地保存说明；
- Pickers：全屏可滚动列表，当前项带勾选，选择后返回设置；
- Loading：无旧图时显示进度；有旧图时覆盖“正在换一张…”；
- Image：默认纯图片；触摸显示 A/B 控件；
- Error：无缓存时提示“图片暂时不可用”和“按 B 重试”；有缓存时只显示非阻塞 toast。

正式 UI 不提供内部诊断页入口。硬件、BLE、IP 和媒体错误继续写入串口日志。

## 8. 输入规则

- KEY A：图片页切换 Cover/Fit+Blur；其他页面按页面语义处理或无动作；
- KEY B：图片页刷新；错误页重试；请求中不重复排队；
- A+B：图片页形成组合后显示保持进度，达到 1000 ms 返回设置；提前释放取消；
- 触摸图片：显示控制层并重置 3000 ms 计时；
- 触摸 A/B 屏幕控件：分别执行与实体 A/B 相同的命令；
- 实体键在控制层隐藏时仍始终有效。

## 9. 错误与恢复

- 配对失败或取消：清除 passkey UI，返回未配对页；
- 手机断开：保留 Bond、设置和缓存，显示“等待手机连接”；
- NVS 写入失败：留在设置页，不请求图片；
- DNS/TLS/重定向/超时/HTTP 失败：保留缓存；无缓存时进入错误页；
- 下载中断、大小超限或格式无效：删除临时文件，正式缓存不变；
- 解码失败或内存不足：保留上一张图片；动画中保留最后完整帧；
- UI 永不在 LVGL 线程执行阻塞网络或文件操作。

所有错误状态同时包含可读文案和日志错误码，不能只用颜色表达。

## 10. 验证

### 10.1 Host tests

- A/B 单键、120 ms chord、999/1000 ms 阈值、提前释放和事件抑制；
- 启动、配对、设置、加载、图片、离线与错误状态转换；
- UAPI 参数白名单、type 联动和 URL 构建；
- JPEG/PNG/GIF/WebP 静态与动画识别、尺寸和大小边界；
- 临时文件替换失败时的缓存保留逻辑。

### 10.2 构建检查

- `device/tests/host/run.sh`；
- `source /Users/eki/esp/esp-idf/export.sh && idf.py -C device build`；
- 固件不超过 6 MiB factory app 分区；
- 固件不调用 Wi-Fi 初始化，不依赖 Wi-Fi 网络路径；
- `git diff --check`；
- 不改动用户已有的 iOS scheme 修改和无关文件。

### 10.3 实机验收

Codex 生成固件和验证表，不自行刷写设备。实机需验证：

- 首次配对、六位码、成功转场和重启后 Bond 恢复；
- 无手机时无法联网，连接后 UAPI 流量经 BLE 手机转发；
- 设置和显示模式跨重启保持；
- JPEG、PNG、GIF、静态 WebP、动画 WebP；
- Cover、Fit+Blur、动画帧连续性；
- 触摸显示控件、3000 ms 隐藏、A/B 颜色与位置；
- A、B、A+B 1000 ms 与提前释放；
- 离线缓存、无缓存错误、TLS 失败、损坏媒体和内存限制回退。

## 11. 完成标准

只有以下条件全部成立，设备端实现才算完成：

1. Figma 主流程与全部关键错误状态在代码中可达；
2. 所有公网请求唯一经过 BLE 手机转发；
3. 设置、Bond 和显示模式正确持久化；
4. 五类要求格式可显示，动画按帧时序播放；
5. 两种显示模式、触摸控制层和实体按键规则一致；
6. Host tests、ESP-IDF build、尺寸与静态检查通过；
7. 生成完整实机验收表，并明确尚未执行的物理检查。
