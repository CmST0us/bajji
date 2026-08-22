# `mbedtls_ssl_setup -0x008D` 是内部 DRAM 分配失败

## 现象

HTTPS 请求在收到任何 HTTP 状态或响应字节前失败，日志连续出现：

```text
esp-tls-mbedtls: mbedtls_ssl_setup returned -0x008D
HTTP_CLIENT: Connection failed, sock < 0
wallpaper: image download failed: ESP_ERR_HTTP_CONNECT status=0 bytes=0
```

立即重试仍会得到同样结果。

## 已验证的根因

当前 ESP-IDF 使用 Mbed TLS 4。`$IDF_PATH/components/mbedtls/mbedtls/include/mbedtls/ssl.h:108`
把 `MBEDTLS_ERR_SSL_ALLOC_FAILED` 定义为 `PSA_ERROR_INSUFFICIENT_MEMORY`，后者的值是 `-141`，
即日志中的 `-0x008D`。

`mbedtls_ssl_setup()` 会先分配 TLS 输入、输出缓冲区，再分配握手状态；任一分配失败都会返回该错误
（`$IDF_PATH/components/mbedtls/mbedtls/library/ssl_tls.c:1169`）。Bajji 当前配置为 16 KiB 输入、
4 KiB 输出，并且关闭了动态缓冲区；加上 TLS 记录开销后，第一块连续内存略大于 16 KiB。

关键是 `device/sdkconfig:3491` 的 `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`：
`$IDF_PATH/components/mbedtls/port/esp_mem.c:14` 会给所有 Mbed TLS 分配强制加上
`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`，因此 8 MB PSRAM 完全不会参与。结论是当前内部 DRAM
余量或最大连续块不足；现有日志不能区分是输入缓冲区、输出缓冲区还是后续握手结构失败。

## 排除项

- 不是证书、DNS、Cloudflare 或 HTTP 状态：错误发生在 `mbedtls_ssl_setup()`，尚未开始 TLS 握手，
  `status=0 bytes=0` 也与此一致。
- `wifi:<ba-add>` 是 Wi-Fi Block Ack 建立日志，不是错误。它可能改变当时的内存余量，但不是返回码含义。
- 五次立即重试不会释放其他任务占用或消除堆碎片，所以不能修复这个确定性的分配失败。

## 采用的处理

把 Mbed TLS 分配模式改成 `CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y`，没有直接强制全部放入 PSRAM。
Bajji 已设置 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`；ESP-IDF 默认分配器会让不超过 16 KiB 的
分配优先走内部内存，让略大于 16 KiB 的 TLS 输入缓冲区优先走 PSRAM，并在首选堆失败后回退
（`$IDF_PATH/components/heap/heap_caps.c:107`）。这比 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` 更少地把
小型握手和密钥相关对象移到外部内存。

真机验证时，在 `esp_http_client_perform()` 前临时记录 internal/PSRAM 的 free 与 largest block；若仍失败，
再启用 Mbed TLS level 1 debug 确认具体失败的分配大小。不要先缩小 16 KiB 输入上限；对端发送完整 TLS
record 时可能产生兼容性问题。
