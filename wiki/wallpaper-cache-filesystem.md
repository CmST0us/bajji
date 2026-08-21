# 壁纸缓存用 FAT，不用 SPIFFS

## 现象

点刷新壁纸，任务看门狗直接炸：

```
E task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E task_wdt:  - IDLE1 (CPU 1)
E task_wdt: CPU 0: ipc0
E task_wdt: CPU 1: wallpaper

fwrite -> SPIFFS_write -> spiffs_object_append -> spiffs_gc_check
      -> spiffs_gc_find_candidate -> esp_partition_read -> esp_flash_read
      -> spi_flash_disable_interrupts_caches_and_other_cpu
```

## 根因

SPIFFS 没有空闲链表。删文件只是把页标记成 deleted，而 `spiffs_gc_check()` 计算可用空间时，**deleted 页和已分配页一样都要扣掉**：

```c
s32_t free_pages =
    (SPIFFS_PAGES_PER_BLOCK(fs) - SPIFFS_OBJ_LOOKUP_PAGES(fs)) * (fs->block_count - 2)
    - fs->stats_p_allocated - fs->stats_p_deleted;
```

所以一个分区如果只存一张几 MB 的图、但每次刷新都重写一遍，几次下载就把可用页耗光了。之后几乎每次 `fwrite` 都要进 GC，而 `spiffs_gc_find_candidate` 会把分区里**每一个块**的 object lookup page 都扫一遍，就为了回收最多 `CONFIG_SPIFFS_GC_MAX_RUNS * 4 kB`。当时分区 8 MB、页大小 256 字节，也就是扫 2048 个块换最多 40 kB。

更要命的是这些 flash 读会**把两个核的 cache 都关掉**——另一个核卡在 `ipc0` 上——期间整台设备什么都干不了。

关键的一点：**单次 `fwrite` 自己就能超过 5 秒的看门狗超时**，所以在写与写之间让出 CPU 是没用的。HTTP 数据回调里本来就有一句 `vTaskDelay(1)`，一点忙都帮不上，因为卡死发生在一次写的内部。

这个取舍 ESP-IDF 自己在 `esp_spiffs.h` 里写得很清楚：

> On one GC iteration, SPIFFS will erase one logical block (4kB). [...]
> increasing `CONFIG_SPIFFS_GC_MAX_RUNS` value increases the maximum amount of
> time for which any SPIFFS GC or write operation may potentially block.

调这个参数只是拿看门狗复位换 `ENOSPC`。问题在于这个负载不适合 SPIFFS，不在配置。

## 改法

换成 FAT + wear levelling。删文件立刻释放簇，没有 GC 这回事，一次写的代价就是更新 FAT 加上写数据扇区。

- `device/partitions.csv`：`wallpaper` 分区的 subtype 是 `fat`，不是 `spiffs`
- `device/components/wallpaper/wallpaper_service.cpp`：`esp_vfs_fat_spiflash_mount_rw_wl()`
- `device/components/wallpaper/CMakeLists.txt`：依赖 `fatfs wear_levelling`

## 几个会咬人的地方

**长文件名。** FATFS 默认只支持 8.3，`wallpaper.webp` 超了，没开 `CONFIG_FATFS_LFN_HEAP` 的话 `fopen` 会**静默失败**。这个项目里它恰好是开的，但别默认如此，动之前先查。

**挂载点还叫 `/spiffs`。** 纯粹是历史遗留，之所以留着是因为 `CONFIG_LV_FS_STDIO_PATH` 把 LVGL 的 `S:` 盘符映射到了它。要改名就得同步改那个 Kconfig，而且对不上时是静默失败——图就是加载不出来，没有任何报错。

**改 subtype 会强制重新格式化。** `format_if_mount_failed = true` 会自动处理，但缓存的壁纸会丢一次，改完第一次开机是没有图可显示的。看那次开机的日志时要把这点考虑进去——我就因为这个白读了一轮日志。

**必须烧分区表。** `idf.py flash` 会烧，只烧 app 不会，那样挂载会拿旧分区表去对，直接失败。

## 相关

"两个核的 cache 一起被关"这个机制，也是为什么动图壁纸必须整个读进内存而不能从文件系统流式播放，以及为什么静态图要先一次性解码进 RGB565 缓冲区、而不是每次重绘都从文件画。

LVGL 的 JPEG 解码器（`lv_tjpgd.c`）只会一块一块地吐 MCU，从不填充图像缓存，所以直接从文件画意味着**每次重绘都把整张图重读重解一遍**。
