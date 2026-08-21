# Bajji 工程 wiki

这里放那些查起来很费劲、但下次还会遇到的东西。判断标准很简单：**如果这篇早就存在，能不能省下我刚才花掉的时间？** 能，就值得写。

只跟某一次会话有关的东西不要放这儿，放 [`../HANDSOFF.md`](../HANDSOFF.md)。

## 现有条目

| 页面 | 讲什么 |
|---|---|
| [lvgl-blur-seam.md](lvgl-blur-seam.md) | `clip_corner` 会把子对象拆成上下两个图层画，把 LVGL 的 IIR 模糊切断了 |
| [instrumenting-over-guessing.md](instrumenting-over-guessing.md) | 上面那条缝是怎么找出来的，以及为什么五个"读源码推出来"的假设全错 |
| [wallpaper-cache-filesystem.md](wallpaper-cache-filesystem.md) | SPIFFS 为什么会在换壁纸时把看门狗喂爆，以及换成 FAT 的经过 |
| [gif-frame-rate.md](gif-frame-rate.md) | 小 GIF 被 LVGL 从 PSRAM 逐帧放大时为什么只剩约 4 FPS |
| [shared-i2c-bus.md](shared-i2c-bus.md) | 共享 I2C 总线上反复创建销毁设备句柄会和别的任务打架 |
| [nimble-coc-buffering.md](nimble-coc-buffering.md) | NimBLE CoC 的 SDU slot、credit 和 mbuf block 为什么不能混为一谈 |
| [vendor-patching.md](vendor-patching.md) | `device/vendor/**` 是怎么固定和打补丁的，直接改为什么会凭空消失 |
| [concurrent-agent-sessions.md](concurrent-agent-sessions.md) | 两个 agent 在同一个工作区里互删对方代码的真实记录 |

## 怎么写一篇

先写你看到的现象，再写根因（带上 `文件:行号`），最后写怎么改的。

**把排除掉的假设也写进去，还有是怎么排除的。** 这部分往往最值钱——下一个人多半会想到同样那几个错误方向，看到"这条路我走过，是死的"能省一大截时间。

哪些是验证过的、哪些只是推测，要分清楚。一篇过度自信的文档比没有更糟。
