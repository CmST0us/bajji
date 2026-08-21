# 交接笔记

多个 agent 会话共用这个仓库、彼此不共享上下文，这里是它们之间的公共便签。规则见 [`AGENTS.md`](AGENTS.md)。

**开工先读这里。收工前更新。条目带日期、写短、新的放最上面。不再成立的直接删掉——过期的交接笔记比没有更糟。**

某条笔记如果已经不只是"此刻的状态"、而变成了通用经验，挪到 [`wiki/`](wiki/README.md) 去。

---

## 2026-08-21 · GIF 帧率根因已修，最终代理尺寸待新图实测

真机聚合探针确认当前 240×259 GIF 被 LVGL 放大到 466×466：源帧延迟约 75–100 ms、解码约 15–20 ms/帧，但抗锯齿缩放后只有约 3.7 FPS；关抗锯齿约 4.8–5.2 FPS。根因和排除项已写进 [`wiki/gif-frame-rate.md`](wiki/gif-frame-rate.md)。

代理现在按显示模式输出目标尺寸（cover 466/outside，fit 328/inside，不再 `we`），避免逐帧缩放；直连回退用无抗锯齿兜底。host tests、O2 完整构建通过，最终无探针 app 已烧写并正常启动。**新代理尺寸的最终 FPS 还要等手机连接后刷新出一张新 GIF 再测；旧缓存不会自动重编码。**

## 2026-08-21 · 壁纸分区换成了 FAT，首次开机会重新格式化

`partitions.csv` 里 `wallpaper` 分区的 subtype 从 `spiffs` 改成了 `fat`。**必须整片烧写**（`idf.py flash` 会带上分区表），而且改完第一次开机挂载会失败并自动格式化，缓存的壁纸丢一次。

读那次开机的日志时要把这点考虑进去——之前有一轮就是因为这个，界面根本没走到壁纸页，探针一条都没打出来，白烧了。

背景在 [`wiki/wallpaper-cache-filesystem.md`](wiki/wallpaper-cache-filesystem.md)。

## 2026-08-21 · 并发会话：这个工作区同时有别人在动

这一天里两个会话互相删掉过对方未提交的代码，双向都发生了。事故经过和应对规矩在 [`wiki/concurrent-agent-sessions.md`](wiki/concurrent-agent-sessions.md)。

要点：用锚定的最小改动，别拿旧副本整体重写文件（尤其 `deps.lock.json` 这类），早点提交，改动前后都查 `git status`。
