# iOS PhotosPicker 不能只靠 UTI 识别实况照片

## 现象

真机从 Bajji 的系统照片选择器选中一张实况照片后，App 进入了普通图片裁切并生成 PNG，没有进入动态片段截选和
GIF 导出。旧实现位于 `ios/BajjiApp/WallpaperMedia.swift:277`，先检查
`PhotosPickerItem.supportedContentTypes`，再决定加载图片、视频或 `PHLivePhoto`。

## 根因

已验证的是：`supportedContentTypes` 是可加载类型的偏好列表，不是可靠的媒体身份；Apple 的
[Selecting Photos and Videos in iOS](https://developer.apple.com/documentation/photokit/selecting-photos-and-videos-in-ios)
示例会先询问 item provider 能否加载 `PHLivePhoto`，成功后才考虑其他表示。Apple 也建议对支持任意原始编码的
App 使用 `.current`，尽量避免选择器转码。

本次没有在故障真机上记录具体 UTI 列表，因此“系统返回了纯图片 UTI”仍是基于实际分支结果的高置信度推断；但
可以确定，按 UTI 先分流会让可加载的 `PHLivePhoto` 永远没有被尝试。

已经排除设备端 GIF 协议和 GIF 导出器：故障发生在进入截选页之前，旧代码根本没有调用
`importLivePhoto`；设备端 format 3 也已经由普通视频 GIF 共用。

## 改法

`ios/BajjiApp/WallpaperMedia.swift:277` 现在对每个选择结果优先调用
`loadTransferable(type: PHLivePhoto.self)`。返回对象时直接提取 `fullSizePairedVideo`，再回退
`pairedVideo`；只有明确返回 `nil` 才按 UTI 走普通视频或图片。若 UTI 声明为实况但对象加载失败，则显示错误，
不再静默降级成 PNG。

`ios/BajjiApp/ContentView.swift:814` 同时设置 `preferredItemEncoding: .current`。iPhoneOS Debug build 和
`build-for-testing` 已通过；仍需用包含本机及 iCloud 实况照片的真机各重测一次系统选择流程。
