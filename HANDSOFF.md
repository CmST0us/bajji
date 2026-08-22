# 交接笔记

多个 agent 会话共用这个仓库、彼此不共享上下文，这里是它们之间的公共便签。规则见 [`AGENTS.md`](AGENTS.md)。

**开工先读这里。收工前更新。条目带日期、写短、新的放最上面。不再成立的直接删掉——过期的交接笔记比没有更糟。**

某条笔记如果已经不只是"此刻的状态"、而变成了通用经验，挪到 [`wiki/`](wiki/README.md) 去。

---

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
