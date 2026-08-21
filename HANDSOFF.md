# 交接笔记

多个 agent 会话共用这个仓库、彼此不共享上下文，这里是它们之间的公共便签。规则见 [`AGENTS.md`](AGENTS.md)。

**开工先读这里。收工前更新。条目带日期、写短、新的放最上面。不再成立的直接删掉——过期的交接笔记比没有更糟。**

某条笔记如果已经不只是"此刻的状态"、而变成了通用经验，挪到 [`wiki/`](wiki/README.md) 去。

---

## 2026-08-21 · 待办：`BMI270_BMM150_Sensor.patch` 还没提交

`device/deps.lock.json` 里已经登记了 `"patch": "BMI270_BMM150_Sensor.patch"` 并且**已随 `97d0955` 提交**，但补丁文件本身 `device/patches/BMI270_BMM150_Sensor.patch` 还是 untracked。

现在这个状态下，别人干净 checkout 之后跑 `fetch_deps.py` 会因为找不到补丁文件而失败。**谁先动这块谁把它 `git add` 上。**

## 2026-08-21 · 临时插桩已全部清理

排查期间在这些地方加过临时日志，**现在都已经还原，`grep TEMPORARY` 是干净的**：

- `device/components/board_hal/board_hal.cpp`
- `device/components/ui/diagnostics_ui.cpp`
- `device/vendor/lvgl/src/core/lv_refr.c`、`src/draw/lv_draw.c`、`src/draw/sw/lv_draw_sw_blur.c`、`src/draw/sw/lv_draw_sw_img.c`

vendor 里剩下的改动（`lvgl/src/libs/tjpgd/tjpgdcnf.h` 等）**不是插桩**，是真实改动，别顺手还原。要查 vendor 状态用：

```sh
for d in device/vendor/*/; do echo "== $d"; git -C "$d" status --short; done
```

## 2026-08-21 · 壁纸分区换成了 FAT，首次开机会重新格式化

`partitions.csv` 里 `wallpaper` 分区的 subtype 从 `spiffs` 改成了 `fat`。**必须整片烧写**（`idf.py flash` 会带上分区表），而且改完第一次开机挂载会失败并自动格式化，缓存的壁纸丢一次。

读那次开机的日志时要把这点考虑进去——之前有一轮就是因为这个，界面根本没走到壁纸页，探针一条都没打出来，白烧了。

背景在 [`wiki/wallpaper-cache-filesystem.md`](wiki/wallpaper-cache-filesystem.md)。

## 2026-08-21 · 并发会话：这个工作区同时有别人在动

这一天里两个会话互相删掉过对方未提交的代码，双向都发生了。事故经过和应对规矩在 [`wiki/concurrent-agent-sessions.md`](wiki/concurrent-agent-sessions.md)。

要点：用锚定的最小改动，别拿旧副本整体重写文件（尤其 `deps.lock.json` 这类），早点提交，改动前后都查 `git status`。
