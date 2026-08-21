# GIF 帧率：让代理输出显示尺寸，别在设备上逐帧缩放

## 现象

一张缓存 GIF 看起来一卡一卡。真机在 ESP32-S3 240 MHz、`-O2` 下的聚合探针结果是：

- GIF 自带的帧延迟约 75–100 ms，也就是源动画期望约 10–13 FPS。
- `GIF_playFrame()` 和 RGB565 合成每帧约 15–20 ms。
- 实际只播放约 3.7 FPS。

探针再打印尺寸后，缓存文件是 240×259，cover 图像对象是 466×466，LVGL 算出的 scale 是 497（`LV_SCALE_NONE` 是 256），即每帧放大约 1.94 倍。设备经 USB reset 时保存的 PC 也两次落在 `lv_draw_sw_transform.c:656-740` 的 `transform_rgb565a8()` 内；这不是崩溃回溯，但和聚合计时指向同一段热点。

## 根因

旧代理参数在 `device/components/wallpaper/wallpaper_format.c:249-267` 使用 `we`，意思是禁止放大小图。这省了下载字节，却把放大工作留给手表。

`LV_IMAGE_ALIGN_COVER` / `CONTAIN` 会在 `device/vendor/lvgl/src/widgets/image/lv_image.c:1003-1027` 根据对象和源图尺寸设置 scale。只要 scale 不是 256，每次 GIF 在 `device/vendor/lvgl/src/widgets/gif/lv_gif.c:636-711` 解出新帧并 invalidate，软件绘制就重新跑整块 RGB565 transform。这个负载的源和目标缓冲区都很大，通常落在 PSRAM；逐目标像素的读取、插值和写入比 GIF 解码本身贵得多。

## 排除过的方向

| 假设 | 怎么排除的 |
|---|---|
| GIF 解码慢 | 聚合探针测到每帧只有约 15–20 ms，远小于丢掉的约 180 ms。 |
| 每帧从文件系统读取 | UI 已经在 `diagnostics_ui.cpp:319-337` 把整个 GIF 读进 RAM，`lv_gif` 走 `GIF_openRAM()`。 |
| 模糊背景每帧重算 | 预渲染模糊背景后，cover 模式仍只有约 3.7 FPS；当前样本根本没有模糊背景。 |
| 只关抗锯齿就够了 | `lv_image_set_antialias(false)` 把同一缓存提高到约 4.8–5.2 FPS，但仍明显低于源动画。它只适合作为直连回退的兜底。 |

## 改法

让现有图片代理直接产出 UI 的目标尺寸，并允许它放大小图：

- cover：短边 466，`fit=outside`；LVGL 只裁掉长边，不缩放。
- fit：长边 328，`fit=inside`；LVGL 只居中，不缩放。
- 保留 `n=-1`，否则代理会把动画压成第一帧。

直连回退仍可能拿到任意尺寸，所以 GIF widget 同时关闭缩放抗锯齿作为兜底。URL 分支有 host test；新代理尺寸的最终真机帧率必须在刷新出一张新 GIF 后测，旧缓存不会被固件自动重编码。

## 播放时输入为何也会慢

触摸采样和 GIF 解码、绘制都由 `device/components/board_hal/board_hal.cpp:367-390` 的同一次 `lv_timer_handler()` 执行，并且整段持有 `lvgl_mutex`。旧缓存逐帧缩放时，一次绘制会让触摸等到当前帧结束。

物理按键原来由优先级 1 的 `main_task` 调 `BoardHal::poll()` 采样，而 LVGL 任务优先级是 3。真机探针测到 GIF 播放时主循环采样间隔反复达到 174–1137 ms；短按完全落在空洞里时，后面的事件缓存当然无从生效。A 切换显示模式还会同步重建图片约 627 ms，在这段时间内主循环同样不能采样。

现在 GPIO 和 `ButtonState` 由 ESP 定时器任务固定每 10 ms 采样，以短临界区和主循环交换事件；LVGL 任务降到与 `main_task` 相同的优先级。有输入待处理时，主循环会等待当前 LVGL 帧释放锁，而不是用非阻塞抢锁反复错过空档。控件显示和按键处理不再暂停前景 GIF。

真机验证中，定时器采样没有一次超过 30 ms，并在主循环阻塞 627 ms 的窗口内完整捕获了一次 A 的按下、抬起和去抖事件，主循环恢复后成功投递。临时日志已移除，host tests、O2 完整构建和最终无探针固件启动均通过。
