# Apple Wi-Fi Infrastructure 配网必须经过 Accessory Transport Extension

## 现象

只在宿主 App 中创建 `WINetworkSharingController` 可以发起用户授权，但真正读取共享网络的
`WINetworkSharingProvider` 不能放在 App 或 Packet Tunnel 里。即使 Swift API 能被编译器看到，
系统也不会把 Wi-Fi 凭据交给这些进程。

## 根因

Apple 把凭据读取限制在 Accessory Transport Extension（ATE）中。宿主 App 先用
AccessorySetupKit 建立受系统管理的 accessory，再由系统启动带
`com.apple.accessory-transport-extension` extension point 的 ATE。ATE 同时需要：

- `com.apple.developer.accessory-transport-extension`；
- `com.apple.developer.wifi-infrastructure = WiFiNetworkSharing`；
- 与 AccessorySetupKit 匹配的 Bluetooth service UUID。

这个 service UUID 必须放在 ATE `Info.plist` 的
`NSAccessorySetupBluetoothServices` 中。Xcode 26.5 自带的 Accessory Transport Extension 模板在
`Accessory Transport Extension.xctemplate/TemplateInfo.plist:92-96` 明确生成这个 key；
`NSBluetoothServices` 不是等价写法。

本仓库对应入口是 `ios/BajjiApp/AccessoryManager.swift`、
`ios/WiFiSharingExtension/TransportExtension.swift` 和
`ios/WiFiSharingExtension/WiFiSharingProvider.swift`。Apple 的完整事件顺序以
[Sharing Wi-Fi network credentials](https://developer.apple.com/documentation/wifiinfrastructure/sharing-wi-fi-network-credentials)
示例为准。

## 地域门禁落点（iOS 26.6）

Xcode 26.5 的 iPhoneOS SDK 只含 `WiFiInfrastructure.tbd` 和 Swift interface，没有可供反汇编的
实现。iPhone 16 Pro Max、iOS 26.6（23G71）的 DeviceSupport 符号缓存显示，
`WiFiInfrastructure` 只是 Swift 封装：App 和 ATE 分别通过
`CWFInterface.initForWiFiNetworkSharingAppWithAccessoryID:` 与
`CWFInterface.initForWiFiNetworkSharingAppExtensionWithAccessoryID:` 连接私有 `CoreWiFi`。

实际资格判定发生在 `CoreWiFi` 的 `CWFXPCRequestProxy` check-in 路径。Wi-Fi Network Sharing
App/ATE 建立服务连接时，该路径以客户端 audit token 创建 `OSEligibilityQuery`；不符合资格会记录
`WiFiNetworkSharing is not eligible`，随后返回 unsupported 错误。因此
`WINetworkSharingController.requestAuthorization()` 是后续授权入口，
`supportsWiFiNetworkSharing` 是能力结果，不是地域门禁本身。

方法表确认该实现属于 `CWFXPCRequestProxy`，类型编码是 `v32@0:8@16@24`。从 dyld shared cache
抽出的单独 framework 合并了 selector 字符串，无法再由指针直接还原名字；结合唯一匹配的签名和
check-in 语义，地域门禁方法可高置信度定位为
`-[CWFXPCRequestProxy __checkin:XPCConnection:]`。这里“类、实现与判定行为”已经由二进制验证，
“selector 名与实现的对应关系”仍属于高置信度推断。未验证也未记录任何 Hook 或绕过方式。

### TestFlight 不属于全地区开发例外

Apple 文档只说所有地区都可以“develop and test”，没有明确列出 TestFlight。iOS 26.6
`OSEligibility` 的实际分支会从 `LSApplicationRecord` 读取 `isProfileValidated`、
`isUPPValidated` 和 `isBeta`；只有 profile 已验证、不是 production profile、并且不是 beta 时才记录
`Bypassing eligibility`。TestFlight 应用属于 beta，因此仍执行正常地域资格检查。非欧盟真机测试应使用
Xcode 直接安装的 Development build，不能依赖 TestFlight 获得测试例外。

## GitHub 公开案例检索（2026-08-22）

没有找到针对 `WiFiInfrastructure`、`CWFXPCRequestProxy` 或 Wi-Fi Network Sharing eligibility
的公开案例。能找到的是共用 `os_eligibility` 机制的 macOS 案例：
[zouxian](https://github.com/CatMe0w/zouxian) 和
[enableAppleAI](https://github.com/kanshurichard/enableAppleAI) 面向 Apple Intelligence/Xcode LLM，
[iphone-mirroring-bypass](https://github.com/nxm/iphone-mirroring-bypass) 面向 iPhone Mirroring 的
Mac 端资格域；后者还明确记录了 iOS 端仍有独立门禁。这些仓库处理的是 macOS 的
`eligibilityd`/资格缓存，不是 iOS `CoreWiFi` 的客户端 audit-token 查询，不能视为当前门禁的
现成实现。`fishhook`、Swift/Objective-C swizzle 和 Frida 搜索结果只是通用 Hook 工具，也没有命中
该功能的具体案例。

## 改法

1. App 用 `ASAccessorySession` 添加配件，并在连接后调用
   `WINetworkSharingController.requestAuthorization()`。
2. ATE 中创建 `WINetworkSharingProvider`，消费 `networkEvents()`；需要用户确认时调用
   `presentAskToShareUI()`。
3. 只把用户已经共享的 personal-network SSID、安全类型和密码编码后写入加密、已绑定的
   GATT characteristic。Enterprise identity/certificate 不应降级成密码配网。

没有采用 `NEHotspotConfiguration`：它用于让 iPhone 加入指定网络，不会把 iPhone 当前网络的
凭据安全分享给配件。也没有增加手工 SSID/密码表单，因为系统分享流程已经覆盖这个边界。

## 真机复现：ATE service key 错误与分享前静默断点

### 观察到的现象

iPhone 16 Pro Max / iOS 26.6 上，宿主 App 的 `requestAuthorization()` 稳定返回 `automatic`，系统也
启动了 `WiFiSharingExtension.appex` 进程。设备端完成 `encrypted=1 bonded=1`，但没有收到 Wi-Fi
provisioning characteristic 的 GATT write；宿主 App 控制台也没有扩展进程的
`TransportExtension`、`WiFiSharing` 或 `ProvisioningWriter` 日志。

### 已确认与高置信度根因

- 已确认：`automatic` 表示用户已允许系统自动向该配件分享 Wi-Fi，无需逐网络再次确认。
- 已确认：BLE 配对和绑定成功，故障发生在 payload 解码和设备 STA 连接之前。
- 已确认：修复前的 extension `Info.plist` 使用了 `NSBluetoothServices`，而宿主 App 与 Xcode 官方
  ATE 模板都使用 `NSAccessorySetupBluetoothServices`；现已改成官方 key 并通过完整构建。
- 高置信度、待重新安装实测：错误 key 使 ATE 无法把自己的 `ASAccessorySession` 正确匹配到该
  BLE 配件，因此扩展可被拉起，却不会进入有效的 network-sharing/provider 发送链路。

### 排除项

- NimBLE disconnect reason `534` 是 `BLE_HS_ERR_HCI_BASE + 0x16`（本地主机终止），`531` 是
  `BLE_HS_ERR_HCI_BASE + 0x13`（远端用户终止）；随后都能重新连接并恢复加密绑定，不是配网根因。
- `BLE_ERR_INV_HCI_CMD_PARMS` 出现在停止 advertising 的会话切换附近，没有阻止下一次 bonded
  connection 建立，也没有证据表明它影响 GATT service。
- 日志后半段 `ASAccessorySession` 已出现两个 accessory；修正 plist 后若仍失败，应先移除重复配件，
  避免 ATE 按 Apple 模板取 `accessories.first` 时命中旧记录。

### CoreBluetooth 初始化可能短暂报告 poweredOff

真机还观察到新建 `CBCentralManager` 后状态依次为 `unknown(0) → poweredOff(4) → poweredOn(5)`，
即使蓝牙实际已开启。原来的 `AccessoryManager.swift:172-174` 把所有非 `unknown/resetting` 状态都
当成终态，在 `4` 时清掉 `authorizationPending`，所以 `5` 到来后不会继续连接，并错误提示
“Bluetooth is unavailable”。ATE 的 `ProvisioningWriter` 有相同分支。

修复是让宿主与 ATE writer 在 `.poweredOff` 时保留 pending request，等待 CoreBluetooth 后续状态
回调；只有 `.unauthorized` 和 `.unsupported` 立即失败。若蓝牙确实关闭，用户开启后同一请求会继续。

### AccessorySetupKit picker 与全局 CBCentralManager 互斥

真机日志显示 Share 流程已创建并连接 `CBCentralManager` 后，再点 Add StopWatch 会稳定失败：
`CBManagers active with global permissions`。这不是缺少另一个 entitlement；宿主仍保留着直接访问 BLE
所用的全局权限 manager，而 AccessorySetupKit 的系统 picker 不能与它同时运行。界面此前也没有在 Share
进行中禁用 Add，因而可以反复触发同一错误。

宿主现由 `showPicker(for:)` 负责 Add 流程自己的系统授权，不为 Add 预先创建 CoreBluetooth manager；
Share 则先检查 `CBManager.authorization`，首次使用通过创建 manager 触发系统权限请求，拒绝或受限时提示
去 Settings。Share 完成或失败后取消连接并释放 manager，同时两个按钮共用 `isBusy` 互斥。已有的 3 条
AccessorySetupKit 配件记录不会由此自动清理，若 provider 后续命中旧记录，仍需在系统里移除重复配件。

### `securityPolicy` 是允许模式集合，不是单一认证模式

真机首次收到完整 payload 后，设备稳定以 `reason=211` 断开。ESP-IDF 6.0 的
`esp_wifi_types_generic.h:172` 将它定义为 `WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD`；日志同时显示
iOS 编码为 WPA3，设备把 `threshold.authmode` 设成了 `WIFI_AUTH_WPA3_PSK`。这证明配网、GATT 写入、
payload 解码和 NVS 保存都已经成功，失败发生在 STA 扫描阶段，密码尚未参与握手。

根因是 `WiFiSharingProvider.swift` 把 Apple `securityPolicy` 集合中最强的模式当成单一模式。Apple 文档
明确该集合表示网络允许的所有连接类型；WPA2/WPA3 transition 网络因此被误编码成 WPA3-only 最低
门槛，ESP32 会过滤掉 WPA2 BSSID。现改为对带密码的网络选择最低允许模式作为 ESP-IDF scan
threshold；ESP-IDF 在 WPA3 可用时仍会自动选择 WPA3。OWE 继续单独编码并设置 `owe_enabled`，其 scan
threshold 按 ESP-IDF `wifi-security.rst:166` 要求设为 open，以兼容 OWE transition mode。

同时修掉了一个已观察到但不是 `211` 根因的竞争：旧的 5 秒重连 timer 会在新凭据连接仍进行时触发，
产生 `ESP_ERR_WIFI_CONN`。任何成功提交的新连接请求现在都会取消旧 timer；后续真实断线事件仍会重新
安排 timer。断线日志也新增 reason 名称与 RSSI，iOS 日志新增完整 security policy 集合，便于下一轮
区分安全模式、信号门槛和密码握手失败。修复已通过两端 host tests、Swift 11/11、iPhoneOS 与 ESP-IDF
完整构建，尚待重新安装 App、烧写固件后真机验证。

## 已验证与未验证

已用 iPhoneOS 26.5 SDK 编译通过 App、Packet Tunnel 和 ATE，并用 C/Swift 单元测试核对了两端
payload。真机已验证宿主授权返回 `automatic`、ATE 进程启动及 BLE 加密绑定；尚未验证修正 service
key、CoreBluetooth 生命周期后的 provider event、GATT write 和设备入网。Apple 当前把正式可用范围限制在符合条件的欧盟
Apple Account 与位于欧盟的设备；其他地区只能按 Apple 文档做开发测试。
