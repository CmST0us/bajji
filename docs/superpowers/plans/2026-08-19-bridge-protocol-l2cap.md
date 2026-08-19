# Bridge Protocol and L2CAP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define Bridge v1 once, validate identical C/Swift parsing behavior, and expose an encrypted NimBLE LE L2CAP CoC echo service on StopWatch.

**Architecture:** A bounded streaming parser turns arbitrary byte chunks into complete Bridge frames. NimBLE owns advertising, secure bonding, GATT BridgeInfo, CoC and link statistics; the transport initially echoes PING and test payloads without lwIP.

**Tech Stack:** ESP-IDF 6.0 NimBLE, C11, Swift 6, JSON test vectors.

**Spec:** `docs/superpowers/specs/2026-08-19-bajji-stopwatch-ble-ip-bridge-design.md`

## Global Constraints

- Header is exactly 8 bytes: `BA 77`, version, type, big-endian payload length, big-endian sequence.
- IPv4 payload limit is 1280; control payload limit is 64; CoC MTU is at least 1536.
- Queues hold at most 32 complete frames per direction.
- BLE pairing uses Secure Connections and one persisted peer.
- Device requests 2M PHY, 15 ms interval, latency 0 and 4 s supervision timeout; actual values are reported.

---

## File Map

- `protocol/bridge-v1.md`: wire-level authority.
- `protocol/vectors/bridge-v1.json`: shared valid and invalid byte streams.
- `device/components/bridge_protocol/`: C encoder/parser and host test.
- `device/components/ble_link/`: NimBLE security, GATT, CoC, queues and echo.
- `device/components/ui/diagnostics_ui.cpp`: BLE/bridge statistics display.

### Task 1: Protocol authority and vectors

**Files:**
- Create: `protocol/bridge-v1.md`
- Create: `protocol/vectors/bridge-v1.json`
- Create: `device/components/bridge_protocol/include/bridge_protocol.h`
- Create: `device/components/bridge_protocol/bridge_protocol.c`
- Create: `device/components/bridge_protocol/CMakeLists.txt`
- Create: `device/tests/host/test_bridge_protocol.c`

**Interfaces:**
- Produces: `bridge_parser_feed`, `bridge_encode`, `bridge_frame_t`, `bridge_parse_result_t`.

- [ ] **Step 1: Write failing parser checks**

```c
bridge_parser_t parser = {0};
bridge_frame_t frame;
assert(bridge_parser_feed(&parser, ping, 3, &frame) == BRIDGE_NEED_MORE);
assert(bridge_parser_feed(&parser, ping + 3, sizeof(ping) - 3, &frame) == BRIDGE_FRAME_READY);
assert(frame.type == BRIDGE_TYPE_PING);
assert(frame.payload_len == 8);
```

- [ ] **Step 2: Verify failure**

Run: `cc -std=c11 -Idevice/components/bridge_protocol/include device/tests/host/test_bridge_protocol.c device/components/bridge_protocol/bridge_protocol.c -o /tmp/bajji-protocol-test`

Expected: FAIL because the parser files do not exist.

- [ ] **Step 3: Implement bounded parser and encoder**

```c
typedef struct {
    uint8_t type;
    uint16_t sequence;
    uint16_t payload_len;
    uint8_t payload[1280];
} bridge_frame_t;

bridge_parse_result_t bridge_parser_feed(
    bridge_parser_t *parser, const uint8_t *bytes, size_t length, bridge_frame_t *out);
size_t bridge_encode(const bridge_frame_t *frame, uint8_t *out, size_t capacity);
```

Invalid version/type/length returns `BRIDGE_PROTOCOL_ERROR`; magic is resynchronized before a header is accepted. The parser never allocates.

- [ ] **Step 4: Verify full vector set**

Run: `cc -std=c11 -Wall -Wextra -Werror -Idevice/components/bridge_protocol/include device/tests/host/test_bridge_protocol.c device/components/bridge_protocol/bridge_protocol.c -o /tmp/bajji-protocol-test && /tmp/bajji-protocol-test`

Expected: exit 0 for complete, split, coalesced, invalid-length and resynchronization vectors.

- [ ] **Step 5: Commit**

```bash
git add protocol device/components/bridge_protocol device/tests/host/test_bridge_protocol.c
git commit -m "feat(protocol): define Bridge v1 framing"
```

### Task 2: NimBLE secure peripheral and BridgeInfo

**Files:**
- Create: `device/components/ble_link/CMakeLists.txt`
- Create: `device/components/ble_link/include/ble_link.h`
- Create: `device/components/ble_link/ble_link.c`
- Modify: `device/sdkconfig.defaults`
- Modify: `device/main/app_main.cpp`

**Interfaces:**
- Consumes: `bridge_frame_t` and a persisted 16-byte Device ID.
- Produces: `ble_link_start`, `ble_link_send`, `ble_link_snapshot`, `ble_link_clear_bond`.

- [ ] **Step 1: Enable only NimBLE features required by CoC**

```ini
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM=1
CONFIG_BT_NIMBLE_L2CAP_COC_MPS=512
CONFIG_BT_NIMBLE_L2CAP_COC_SDU_BUFF_COUNT=8
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_SM_LEGACY=n
```

- [ ] **Step 2: Publish encrypted BridgeInfo**

```c
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &bridge_service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]) {
       {.uuid = &bridge_info_uuid.u, .access_cb = bridge_info_read,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC}, {0}}}, {0}};
```

The callback emits exactly 22 bytes and refuses an unencrypted connection.

- [ ] **Step 3: Enforce one peer and request link parameters**

On connection, compare the peer identity with NVS bond metadata, initiate passkey security when unbound, request 12 units (15 ms), latency 0, timeout 400 units, preferred 2M PHY, and maximum data length.

```c
struct ble_gap_upd_params p = {.itvl_min = 12, .itvl_max = 12, .latency = 0, .supervision_timeout = 400};
ble_gap_update_params(conn_handle, &p);
ble_gap_set_prefered_le_phy(conn_handle, BLE_HCI_LE_PHY_2M_PREF_MASK,
                            BLE_HCI_LE_PHY_2M_PREF_MASK, BLE_HCI_LE_PHY_CODED_ANY);
```

- [ ] **Step 4: Build**

Run: `cd device && source /Users/eki/esp/esp-idf/export.sh && idf.py build`

Expected: exit 0 with NimBLE CoC enabled and Bluedroid disabled.

- [ ] **Step 5: Commit**

```bash
git add device
git commit -m "feat(device): add secure BLE bridge service"
```

### Task 3: L2CAP CoC echo and statistics

**Files:**
- Modify: `device/components/ble_link/ble_link.c`
- Modify: `device/components/ui/diagnostics_ui.cpp`

**Interfaces:**
- Consumes: NimBLE `ble_l2cap_event` callbacks.
- Produces: Bridge HELLO/PING echo, bounded queue statistics and disconnect reason.

- [ ] **Step 1: Create one CoC server**

```c
int rc = ble_l2cap_create_server(bridge_psm, 1536, l2cap_event, NULL);
assert(rc == 0);
```

- [ ] **Step 2: Handle complete SDUs without unbounded copies**

```c
case BLE_L2CAP_EVENT_COC_DATA_RECEIVED:
    if (os_mbuf_len(event->receive.sdu_rx) > sizeof(rx_bytes)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    os_mbuf_copydata(event->receive.sdu_rx, 0, os_mbuf_len(event->receive.sdu_rx), rx_bytes);
    return handle_bridge_bytes(rx_bytes, os_mbuf_len(event->receive.sdu_rx));
```

HELLO returns HELLO_ACK, PING returns PONG, and an IPV4 frame is echoed only in explicit Phase 0 test mode.

- [ ] **Step 3: Verify firmware build and host parser**

Run: `device/tests/host/run.sh && cd device && source /Users/eki/esp/esp-idf/export.sh && idf.py build`

Expected: both commands exit 0.

- [ ] **Step 4: Commit**

```bash
git add device
git commit -m "feat(device): add encrypted L2CAP echo transport"
```
