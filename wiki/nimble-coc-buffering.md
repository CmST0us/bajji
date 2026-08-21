# NimBLE CoC 的 SDU slot、credit 和 mbuf block

## 现象

`ble_link.c` 原来为 CoC RX 建了 8 个 1536-byte block，静态占用 12288 bytes，
但当前固件一次只向 NimBLE 交一个接收 SDU。容易误以为 1536-byte MTU 或多个
L2CAP credit 要求 8 个完整 buffer。

## 根因

当前 [`device/sdkconfig.defaults`](../device/sdkconfig.defaults) 固定
`CONFIG_BT_NIMBLE_L2CAP_COC_SDU_BUFF_COUNT=1`。ESP-IDF 6.0 的
`components/bt/host/nimble/nimble/nimble/host/src/ble_l2cap_coc.c:294-309`
在完整 SDU 到达时先把当前 slot 置空，再同步调用应用回调。Bajji 在
`device/components/ble_link/ble_link.c` 的 `COC_DATA_RECEIVED` 分支释放整个 mbuf
chain，处理完数据，最后才调用一次 `ble_l2cap_recv_ready`。所以任意时刻只有一条
RX SDU chain 占用自定义 pool。

1536-byte block 并不等于 1536-byte payload。IDF 的
`porting/nimble/src/os_mbuf.c:288-294` 会扣掉 `struct os_mbuf`；packet head 还要再
扣 `struct os_mbuf_pkthdr`。用当前 Xtensa toolchain 验证两者分别为 16 和 8 bytes：

- 第一个 block 可装 `1536 - 16 - 8 = 1512` bytes；
- 最大 1536-byte SDU 还剩 24 bytes；
- 第二个 block 可装 `1536 - 16 = 1520` bytes。

所以单 SDU slot 的自定义 pool 只需要两个 block。8 改成 2 后 RX pool 从 12288
降到 3072 bytes，释放 9216 bytes 静态内存。

## 排除过的方向

- **CoC initial credits 不是并发 buffer 数。** credit 允许 peer 继续发送组成同一
  SDU 的 L2CAP PDU；NimBLE 用 `os_mbuf_appendfrom` 把它们追加到当前 chain。
- **`BLE_HS_ESTALLED` 不要求应用轮询重发同一个 TX SDU。** IDF
  `ble_l2cap_coc.c:676-694` 在接受 SDU 后持有它；credit 恢复后通过
  `BLE_L2CAP_EVENT_COC_TX_UNSTALLED` 通知应用。返回 `ESTALLED` 时 buffer 已被消费。
- **大 IPv4 frame 不需要把应用帧手工切到 MPS。** `ble_l2cap_send` 接受不超过
  peer CoC MTU 的 SDU，NimBLE 自己按 peer MPS 和 credit 分片。

## 修改与验证

保持 `kReceiveBufferCount = 2`。如果以后把 `COC_SDU_BUFF_COUNT` 改回多 slot，必须
按“并发 SDU 数 × 每条 chain 的最坏 block 数”重新计算，不要只改 Kconfig。

已验证 host tests、ESP-IDF 全量 build 和 target ABI/object size。尚未验证的是硬件上
持续接收恰好 1536-byte SDU；改 MTU、block size 或 slot 数后应补这项 soak test。
