# Cloudflare Images Worker 与 UAPI 随机图片

## 现象

2026-08-22 把壁纸代理从公共 `images.weserv.nl` 换到
`cloudflare/image-proxy/worker.mjs`。Worker 最初部署在纯 `workers.dev` 域名，随后绑定
自定义域名 `bajji-image-proxy.eric3u.cc` 供固件访问。

生产端实测 `cover` 返回 466×466 WebP；`fit` 返回不超过 328×328 的 WebP。GIF 和
WebP 动画用 `anim: true` 保留。`acg`、`bq`、`furry` 共 14 组稳定参数在两种模式下
都能通过；其余 7 个分类恢复后再测 cover/fit 共 14 次，本轮均首次成功，但仍依靠
下述重试吸收已观察到的随机失败。

把之前隐藏的外部来源分类也加入矩阵时，Cloudflare 不再报 wsrv 的域名策略错误，
但 `general_anime` 和 `ai_drawing` 仍会偶发失败：Worker 内部已经换 URL 重抓三次，
针对四个易错分类再测 24 次仍有 2 次返回 502。

## 根因

`cf.image` 的远程 URL 方式受 zone 的 Transformations/Sources 设置约束；最初的纯
`workers.dev` 部署没有可配置的自有 zone。Images binding 则直接接收 Worker fetch
到的字节，不需要把第三方 URL暴露给转换接口。配置在
`cloudflare/image-proxy/wrangler.toml`，调用在
`cloudflare/image-proxy/worker.mjs`。

Cloudflare 的 [Images binding 文档](https://developers.cloudflare.com/images/optimization/binding/)
还明确了两个边界：输入最多 20 MB，输出默认不会自动缓存。因此 Worker 对源响应和
转换失败一起重试，并启用 Workers Cache。

剩余 502 不是 Cloudflare 拒绝重定向。UAPI 的这些分类会随机跳到外部提供商；同一个
分类的相邻请求可以一张成功、一张源站失败。换代理只能去掉 wsrv 的域名过滤，不能让
第三方源站变稳定。

另外，Cloudflare 转 JPEG 时默认生成渐进式 JPEG，而设备的 TJpgDec 不支持。Worker
统一输出设备已有解码器支持的 WebP，避免重新引入 JPEG 兼容问题；格式行为见
[Cloudflare limits and formats](https://developers.cloudflare.com/images/get-started/limits/)。

## 排除过的方向

- 不是 Worker 查询校验普遍拦截。`furry/4k` 最初确实因为类型以数字开头被误拦，修正
  后连续三轮的 cover/fit 都返回 200；其他外部分类的失败发生在源图 fetch 或转换阶段。
- 不是 cover/fit 参数非法。同一套固定转换对当前 14 组参数成功，失败分类也能在相邻
  请求中成功。
- 不是重新允许任意代理来源就能解决。Worker 自己构造唯一的 UAPI URL；放开来源只会
  增加免费转换额度被滥用的风险。

## 改法

Worker 只接受 `/cover` 和 `/fit`，只复制经过字符和 nonce 校验的 UAPI 参数，并最多
换源三次。设备允许全部 21 组设置；代理失败后，固件按
`device/components/wallpaper/wallpaper_service.cpp:467-486` 的策略最多换四次 nonce 重试；
五次尝试全部经过 Worker，不再降级直连原图。
