# images.weserv.nl 与 UAPI 随机图片分类的兼容性

> 这是切换代理前的历史结论。当前实现使用 Cloudflare Images binding；新的实测边界见
> [`cloudflare-images-uapis.md`](cloudflare-images-uapis.md)。

## 现象

同一套 weserv 缩放参数下，`bq/eciyuan` 能返回图片，但有些 UAPI `category` 会返回 400 或 404。给 URL 加随机 `nonce`、重试，结果仍会随分类变化。

2026-08-22 把 UI 当时暴露的 21 组 `category/type` 逐项请求；验证后的稳定子集现在位于 `device/components/ui/diagnostics_ui.cpp:48-61`：

- UAPI 自托管的 `acg`、`bq`、`furry` 及其全部子类型共 14 组，连续三轮全部由 weserv 返回图片；
- `landscape` 为 2/4 成功，`mobile_wallpaper` 为 3/4 成功；
- `general_anime` 为 0/4，均返回 400；
- `ai_drawing` 为 0/4，返回 400 或 404；
- `anime`、`pc_wallpaper` 和不带 category 的全局随机在这四轮成功，但它们仍会抽到下述外部来源，所以不能据此当成稳定组合。

成功响应使用 Range 探测，因此状态码是 206；不带 Range 时对应 200。两者都验证了响应是 `image/jpeg`、`image/png`、`image/gif` 或 `image/webp`，不是只看状态码。

## 根因

切换前由 `wallpaper_build_random_url()` 生成 UAPI URL，再由
`device/components/wallpaper/wallpaper_service.cpp:472-487` 把它交给 weserv。UAPI 的[随机图片文档](https://uapis.cn/docs/api-reference/get-random-image.md)说明外部图床分类会 302 到第三方来源。实测失败分类的第一跳包括：

- `mobile_wallpaper` → `https://t.alcy.cc/mp`
- `general_anime` → `http://www.98qy.com/sjbz/api.php`、`https://api.boxmoe.com/random.php` 等随机来源
- `ai_drawing` → `https://rpic.origz.com/api.php?category=ai`

weserv 会继续跟随这些重定向，但它的[官方 FAQ](https://images.weserv.nl/faq/)明确说公共服务按源域名过滤。失败响应也直接给出 `Domain or TLD blocked by policy` 或上游 404。因此同一个 UAPI category 可能因这次抽到的外部来源不同而成功或失败。

## 排除过的方向

- **不是 UAPI 参数非法。** 失败组合首先都从 UAPI 得到 302 和合法 `Location`，不是 UAPI 的 category 404。
- **不是嵌套 query 没有编码。** 切换前的 `wallpaper_build_proxy_url()` 会编码 `?`、
  `&` 和 `=`；weserv [文档](https://images.weserv.nl/docs/)要求的正是这种编码。相同 URL
  形状下，14 个自托管组合全部成功。这段编码已随 weserv 代理一起删除。
- **不是 `w`、`h`、`fit` 或 `n` 参数。** 完全相同的代理参数对自托管组合成功，错误体指向源域名策略或上游状态。
- **随机 nonce 和普通重试不能保证修复。** nonce 只避开 weserv 缓存；它不会改变 weserv 的域名策略。随机来源偶尔成功也不能让该 category 变成可靠组合。

## 改法

使用 weserv 时，设备曾只暴露 UAPI 自托管的 `acg`、`bq`、`furry` 及其文档列出的
子类型。切换 Cloudflare 后已恢复全部分类，失败交给 Worker 和固件重试。

以后如果必须恢复外部图床分类，不要再靠增加重试次数：要么自建无该域名过滤的图片代理，要么换成能直接返回图片字节、不会再跳到随机第三方域名的上游。
