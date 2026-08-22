// SPDX-License-Identifier: MIT
#include "ble_link.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_l2cap.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

// tx_queue is kQueueCapacity slots of ~1.3 KB in internal .bss, so size it to what lwip can
// actually hand us: CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5760 is ~5 full segments in flight, and the
// TCP_SND_QUEUELEN ceiling is ~17 small ones. Overflow is not fatal - ble_link_send() returns
// ESP_ERR_NO_MEM, ip_bridge.c:97 maps it to ERR_MEM and TCP retransmits - while at the measured
// 2.5-6 KB/s link rate every extra slot is only queueing delay, never goodput.
// sdkconfig.defaults uses one CoC RX SDU slot; two blocks cover one MTU plus mbuf overhead.
enum { kQueueCapacity = 16, kReceiveBufferCount = 2 };
static const char* tag = "ble_link";
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x21, 0xb3, 0x20, 0x2f, 0x9e, 0x3a, 0x54, 0xa8,
    0xc5, 0x4a, 0x86, 0x9c, 0xb0, 0x8d, 0x8f, 0x6f);
static const ble_uuid128_t info_uuid = BLE_UUID128_INIT(
    0x22, 0xb3, 0x20, 0x2f, 0x9e, 0x3a, 0x54, 0xa8,
    0xc5, 0x4a, 0x86, 0x9c, 0xb0, 0x8d, 0x8f, 0x6f);
static const ble_uuid128_t wifi_provision_uuid = BLE_UUID128_INIT(
    0x23, 0xb3, 0x20, 0x2f, 0x9e, 0x3a, 0x54, 0xa8,
    0xc5, 0x4a, 0x86, 0x9c, 0xb0, 0x8d, 0x8f, 0x6f);

typedef struct {
    uint16_t length;
    uint16_t offset;
    uint8_t bytes[BRIDGE_MAX_FRAME_SIZE];
} queued_frame_t;

static ble_link_status_t status;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t device_id[16];
static uint8_t identity_address[6];
static bool identity_address_loaded;
static uint8_t own_address_type;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static struct ble_l2cap_chan* coc_channel;
static bridge_parser_t parser;
static queued_frame_t tx_queue[kQueueCapacity];
static uint8_t tx_head;
static uint8_t tx_count;
static bool tx_stalled;
static uint16_t coc_tx_mtu;
static uint8_t receive_bytes[BAJJI_BRIDGE_MTU];
static bridge_frame_t receive_frame;
static bridge_frame_t response_frame;
static os_membuf_t receive_memory[OS_MEMPOOL_SIZE(kReceiveBufferCount, BAJJI_BRIDGE_MTU)];
static struct os_mempool receive_pool;
static struct os_mbuf_pool receive_mbuf_pool;
static struct ble_npl_event clear_bond_event;
static struct ble_npl_event advertise_event;
static struct ble_npl_event tx_event;
static bool clear_bond_event_initialized;
static bool tx_event_initialized;
static bool tx_event_pending;
static bool clear_bond_pending;
static bool preferred_phy_pending;
static ble_link_frame_handler_t frame_handler;
static ble_link_ready_handler_t ready_handler;
static void* handler_context;
static ble_link_provision_handler_t provision_handler;
static void* provision_context;
static uint16_t ping_sequence;

static void set_bool(bool* field, bool value) {
    portENTER_CRITICAL(&status_lock);
    *field = value;
    portEXIT_CRITICAL(&status_lock);
}

static bool addresses_equal(const ble_addr_t* left, const ble_addr_t* right) {
    return left->type == right->type && memcmp(left->val, right->val, sizeof(left->val)) == 0;
}

static int bonded_peers(ble_addr_t* peer) {
    int count = 0;
    return ble_store_util_bonded_peers(peer, &count, 1) == 0 ? count : 0;
}

static bool peer_allowed(uint16_t handle) {
    ble_addr_t bonded;
    const int count = bonded_peers(&bonded);
    if (count == 0) return true;
    struct ble_gap_conn_desc desc;
    return ble_gap_conn_find(handle, &desc) == 0 && addresses_equal(&bonded, &desc.peer_id_addr);
}

static esp_err_t load_device_id(void) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("bajji_ble", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    size_t size = sizeof(device_id);
    result = nvs_get_blob(handle, "device_id", device_id, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND || size != sizeof(device_id)) {
        esp_fill_random(device_id, sizeof(device_id));
        result = nvs_set_blob(handle, "device_id", device_id, sizeof(device_id));
        if (result == ESP_OK) result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static esp_err_t load_identity_address(void) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("bajji_ble", NVS_READONLY, &handle);
    if (result != ESP_OK) return result;
    size_t size = sizeof(identity_address);
    result = nvs_get_blob(handle, "ble_addr", identity_address, &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND || size != sizeof(identity_address)) {
        identity_address_loaded = false;
        return ESP_OK;
    }
    identity_address_loaded = result == ESP_OK;
    return result;
}

static esp_err_t save_identity_address(const uint8_t* address) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("bajji_ble", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, "ble_addr", address, sizeof(identity_address));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

static int bridge_info_read(uint16_t conn, uint16_t attribute,
                            struct ble_gatt_access_ctxt* context, void* argument) {
    (void)attribute;
    (void)argument;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) != 0 || !desc.sec_state.encrypted || !peer_allowed(conn)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    uint8_t info[22] = {1, 0x1f, (uint8_t)(BAJJI_BRIDGE_PSM >> 8U), (uint8_t)BAJJI_BRIDGE_PSM,
                        (uint8_t)(BRIDGE_MAX_PAYLOAD >> 8U), (uint8_t)BRIDGE_MAX_PAYLOAD};
    memcpy(info + 6, device_id, sizeof(device_id));
    return os_mbuf_append(context->om, info, sizeof(info)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int wifi_provision_write(uint16_t conn, uint16_t attribute,
                                struct ble_gatt_access_ctxt* context, void* argument) {
    (void)argument;
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ESP_LOGW(tag, "rejected Wi-Fi provisioning access: conn=%u attr=%u op=%u",
                 conn, attribute, context->op);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    struct ble_gap_conn_desc desc;
    const int find_result = ble_gap_conn_find(conn, &desc);
    if (find_result != 0) {
        ESP_LOGW(tag, "rejected Wi-Fi provisioning write: conn=%u attr=%u lookup=%d",
                 conn, attribute, find_result);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    const bool allowed = peer_allowed(conn);
    if (!desc.sec_state.encrypted || !desc.sec_state.bonded || !allowed) {
        ESP_LOGW(tag, "rejected Wi-Fi provisioning write: conn=%u attr=%u encrypted=%d bonded=%d allowed=%d",
                 conn, attribute, desc.sec_state.encrypted, desc.sec_state.bonded, allowed);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    ESP_LOGI(tag, "received Wi-Fi provisioning GATT write: conn=%u attr=%u bytes=%u",
             conn, attribute, length);
    uint8_t payload[100];
    if (length > sizeof(payload)) {
        ESP_LOGW(tag, "rejected oversized Wi-Fi provisioning write: bytes=%u max=%zu",
                 length, sizeof(payload));
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(context->om, 0, length, payload) != 0) {
        ESP_LOGE(tag, "could not copy Wi-Fi provisioning write: bytes=%u", length);
        return BLE_ATT_ERR_UNLIKELY;
    }
    portENTER_CRITICAL(&status_lock);
    ble_link_provision_handler_t callback = provision_handler;
    void* callback_context = provision_context;
    portEXIT_CRITICAL(&status_lock);
    if (!callback) {
        ESP_LOGE(tag, "cannot process Wi-Fi provisioning write: no handler registered");
        return BLE_ATT_ERR_UNLIKELY;
    }
    const esp_err_t result = callback(payload, length, callback_context);
    if (result != ESP_OK) {
        ESP_LOGW(tag, "Wi-Fi provisioning handler rejected write: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    ESP_LOGI(tag, "accepted Wi-Fi provisioning GATT write: bytes=%u", length);
    return 0;
}

static const struct ble_gatt_chr_def characteristics[] = {
    {
        .uuid = &info_uuid.u,
        .access_cb = bridge_info_read,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
        .min_key_size = 16,
        .val_handle = NULL,
        .cpfd = NULL,
    },
    {
        .uuid = &wifi_provision_uuid.u,
        .access_cb = wifi_provision_write,
        .arg = NULL,
        .descriptors = NULL,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
        .min_key_size = 16,
        .val_handle = NULL,
        .cpfd = NULL,
    },
    {0},
};

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .includes = NULL,
        .characteristics = characteristics,
    },
    {0},
};

static void advertise(void);

static void advertise_on_host(struct ble_npl_event* event) {
    (void)event;
    advertise();
}

static bool bond_clear_pending(void) {
    portENTER_CRITICAL(&status_lock);
    const bool pending = clear_bond_pending;
    portEXIT_CRITICAL(&status_lock);
    return pending;
}

static void finish_bond_clear(void) {
    ble_addr_t address;
    int result = ble_hs_id_gen_rnd(0, &address);
    if (result == 0) {
        const esp_err_t saved = save_identity_address(address.val);
        if (saved == ESP_OK) {
            result = ble_hs_id_set_rnd(address.val);
            if (result == 0) {
                memcpy(identity_address, address.val, sizeof(identity_address));
                identity_address_loaded = true;
                own_address_type = BLE_OWN_ADDR_RANDOM;
            }
        } else {
            ESP_LOGE(tag, "could not persist new BLE identity: %s", esp_err_to_name(saved));
            result = BLE_HS_ESTORE_FAIL;
        }
    }
    portENTER_CRITICAL(&status_lock);
    clear_bond_pending = false;
    portEXIT_CRITICAL(&status_lock);
    if (result != 0) ESP_LOGE(tag, "could not rotate BLE identity: %d", result);
    if (!ble_npl_event_is_queued(&advertise_event)) {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &advertise_event);
    }
}

static void clear_bond_on_host(struct ble_npl_event* event) {
    (void)event;
    ble_addr_t peer;
    if (bonded_peers(&peer) > 0 && ble_store_util_delete_peer(&peer) != 0) {
        ESP_LOGE(tag, "could not delete bonded peer");
        portENTER_CRITICAL(&status_lock);
        clear_bond_pending = false;
        portEXIT_CRITICAL(&status_lock);
        return;
    }
    portENTER_CRITICAL(&status_lock);
    status.bonded = false;
    status.has_bond = false;
    status.passkey = 0;
    portEXIT_CRITICAL(&status_lock);
    if (connection_handle != BLE_HS_CONN_HANDLE_NONE) {
        const int result = ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (result != 0) {
            ESP_LOGE(tag, "could not terminate connection for bond clear: %d", result);
            portENTER_CRITICAL(&status_lock);
            clear_bond_pending = false;
            portEXIT_CRITICAL(&status_lock);
        }
        return;
    }
    ble_gap_adv_stop();
    finish_bond_clear();
}

static int supply_receive_buffer(struct ble_l2cap_chan* channel) {
    struct os_mbuf* buffer = os_mbuf_get_pkthdr(&receive_mbuf_pool, 0);
    return buffer ? ble_l2cap_recv_ready(channel, buffer) : BLE_HS_ENOMEM;
}

static void drain_tx(void) {
    while (coc_channel && !tx_stalled) {
        portENTER_CRITICAL(&status_lock);
        const bool empty = tx_count == 0;
        portEXIT_CRITICAL(&status_lock);
        if (empty) return;

        queued_frame_t* frame = &tx_queue[tx_head];
        const uint16_t remaining = (uint16_t)(frame->length - frame->offset);
        const uint16_t chunk = remaining < coc_tx_mtu ? remaining : coc_tx_mtu;
        if (chunk == 0) {
            ESP_LOGE(tag, "cannot send CoC frame without a peer MTU");
            portENTER_CRITICAL(&status_lock);
            status.tx_errors++;
            portEXIT_CRITICAL(&status_lock);
            ble_l2cap_disconnect(coc_channel);
            return;
        }
        struct os_mbuf* buffer = os_msys_get_pkthdr(chunk, 0);
        if (!buffer || os_mbuf_append(buffer, frame->bytes + frame->offset, chunk) != 0) {
            if (buffer) os_mbuf_free_chain(buffer);
            ESP_LOGE(tag, "could not allocate %u-byte CoC TX chunk", chunk);
            portENTER_CRITICAL(&status_lock);
            status.tx_errors++;
            portEXIT_CRITICAL(&status_lock);
            ble_l2cap_disconnect(coc_channel);
            return;
        }
        const int result = ble_l2cap_send(coc_channel, buffer);
        if (result == BLE_HS_EBUSY) {
            os_mbuf_free_chain(buffer);
            return;
        }
        if (result == 0 || result == BLE_HS_ESTALLED) {
            portENTER_CRITICAL(&status_lock);
            frame->offset = (uint16_t)(frame->offset + chunk);
            status.tx_bytes += chunk;
            if (frame->offset == frame->length) {
                tx_head = (uint8_t)((tx_head + 1U) % kQueueCapacity);
                tx_count--;
                status.tx_frames++;
            }
            portEXIT_CRITICAL(&status_lock);
            tx_stalled = result == BLE_HS_ESTALLED;
        } else {
            os_mbuf_free_chain(buffer);
            ESP_LOGE(tag, "CoC TX failed: %d", result);
            portENTER_CRITICAL(&status_lock);
            status.tx_errors++;
            portEXIT_CRITICAL(&status_lock);
            ble_l2cap_disconnect(coc_channel);
            return;
        }
    }
}

static void tx_on_host(struct ble_npl_event* event) {
    (void)event;
    portENTER_CRITICAL(&status_lock);
    tx_event_pending = false;
    portEXIT_CRITICAL(&status_lock);
    drain_tx();
}

static void schedule_tx(void) {
    portENTER_CRITICAL(&status_lock);
    const bool enqueue = tx_event_initialized && !tx_event_pending;
    if (enqueue) tx_event_pending = true;
    portEXIT_CRITICAL(&status_lock);
    if (enqueue) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &tx_event);
}

esp_err_t ble_link_send(const bridge_frame_t* frame) {
    if (!frame) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&status_lock);
    if (!status.coc_connected) {
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (tx_count == kQueueCapacity) {
        const uint32_t overflows = ++status.queue_overflows;
        portEXIT_CRITICAL(&status_lock);
        if ((overflows & (overflows - 1U)) == 0) {
            ESP_LOGW(tag, "CoC TX queue full; overflows=%" PRIu32, overflows);
        }
        return ESP_ERR_NO_MEM;
    }
    queued_frame_t* queued = &tx_queue[(tx_head + tx_count) % kQueueCapacity];
    const size_t length = bridge_encode(frame, queued->bytes, sizeof(queued->bytes));
    if (!length) {
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_INVALID_ARG;
    }
    queued->length = (uint16_t)length;
    queued->offset = 0;
    tx_count++;
    portEXIT_CRITICAL(&status_lock);
    schedule_tx();
    return ESP_OK;
}

static void set_bridge_ready(bool ready) {
    ble_link_ready_handler_t callback;
    void* context;
    portENTER_CRITICAL(&status_lock);
    const bool changed = status.bridge_ready != ready;
    status.bridge_ready = ready;
    callback = ready_handler;
    context = handler_context;
    portEXIT_CRITICAL(&status_lock);
    if (changed && callback) callback(ready, context);
}

static void respond(const bridge_frame_t* received) {
    memset(&response_frame, 0, sizeof(response_frame));
    response_frame.sequence = received->sequence;
    if (received->type == BRIDGE_TYPE_HELLO) {
        response_frame.type = BRIDGE_TYPE_HELLO_ACK;
        response_frame.payload_len = 7;
        uint16_t requested_mtu = (uint16_t)((uint16_t)received->payload[16] << 8U) | received->payload[17];
        const bool valid_id = memcmp(received->payload, device_id, sizeof(device_id)) == 0;
        const uint16_t accepted_mtu = requested_mtu < BRIDGE_MAX_PAYLOAD ? requested_mtu : BRIDGE_MAX_PAYLOAD;
        response_frame.payload[0] = (uint8_t)(accepted_mtu >> 8U);
        response_frame.payload[1] = (uint8_t)accepted_mtu;
        memcpy(response_frame.payload + 2, received->payload + 18, 4);
        response_frame.payload[6] = valid_id ? 0 : 1;
    } else if (received->type == BRIDGE_TYPE_PING) {
        response_frame = *received;
        response_frame.type = BRIDGE_TYPE_PONG;
    } else if (received->type == BRIDGE_TYPE_PONG) {
        uint64_t sent_ms = 0;
        for (size_t index = 0; index < 8; ++index) {
            sent_ms = (sent_ms << 8U) | received->payload[index];
        }
        const uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000U;
        portENTER_CRITICAL(&status_lock);
        status.pongs_received++;
        status.last_ping_rtt_ms = now_ms >= sent_ms && now_ms - sent_ms <= UINT32_MAX
                                      ? (uint32_t)(now_ms - sent_ms)
                                      : 0;
        portEXIT_CRITICAL(&status_lock);
        return;
    } else if (received->type == BRIDGE_TYPE_CLEAR_BOND) {
        ble_link_clear_bond();
        return;
    } else {
        ble_link_frame_handler_t callback;
        void* context;
        portENTER_CRITICAL(&status_lock);
        const bool ready = status.bridge_ready;
        callback = frame_handler;
        context = handler_context;
        portEXIT_CRITICAL(&status_lock);
        if (ready && callback &&
            (received->type == BRIDGE_TYPE_IPV4 || received->type == BRIDGE_TYPE_TIME_SYNC ||
             received->type == BRIDGE_TYPE_SETTINGS_GET ||
             received->type == BRIDGE_TYPE_SETTINGS_SET ||
             received->type == BRIDGE_TYPE_WALLPAPER_BEGIN ||
             received->type == BRIDGE_TYPE_WALLPAPER_CHUNK ||
             received->type == BRIDGE_TYPE_WALLPAPER_COMMIT ||
             received->type == BRIDGE_TYPE_WALLPAPER_CANCEL)) {
            callback(received, context);
        }
        return;
    }
    if (ble_link_send(&response_frame) == ESP_OK && received->type == BRIDGE_TYPE_HELLO &&
        response_frame.payload[6] == 0) {
        set_bridge_ready(true);
    }
}

// The 15 ms interval request is deliberately independent of the 2M PHY outcome: a 1M link at
// 15 ms still doubles the CoC round trips per interval versus the ~30 ms iOS picks by default.
// Peers without LE 2M (iPhone 7 and older) complete the PHY procedure with a non-zero status,
// and ble_gap_set_prefered_le_phy() can fail outright; gating the request on either used to
// leave those sessions at the peer's interval for their whole lifetime.
static void request_fast_connection_interval(void) {
    const struct ble_gap_upd_params params = {
        .itvl_min = 12,
        .itvl_max = 12,
        .latency = 0,
        .supervision_timeout = 400,
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    const int result = ble_gap_update_params(connection_handle, &params);
    if (result == 0) {
        ESP_LOGI(tag, "requested 15 ms connection interval");
    } else {
        ESP_LOGW(tag, "could not request 15 ms connection interval: %d", result);
    }
}

static int l2cap_event(struct ble_l2cap_event* event, void* argument) {
    (void)argument;
    switch (event->type) {
        case BLE_L2CAP_EVENT_COC_ACCEPT: {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->accept.conn_handle, &desc) != 0 ||
                !desc.sec_state.encrypted || !desc.sec_state.bonded || !peer_allowed(event->accept.conn_handle)) {
                ESP_LOGW(tag, "rejected unauthenticated CoC connection");
                return BLE_HS_EAUTHEN;
            }
            ESP_LOGI(tag, "accepted CoC connection");
            return supply_receive_buffer(event->accept.chan);
        }
        case BLE_L2CAP_EVENT_COC_CONNECTED: {
            if (event->connect.status != 0) {
                ESP_LOGE(tag, "CoC connection failed: %d", event->connect.status);
                return 0;
            }
            coc_channel = event->connect.chan;
            bridge_parser_reset(&parser);
            set_bridge_ready(false);
            struct ble_l2cap_chan_info info;
            if (ble_l2cap_get_chan_info(coc_channel, &info) == 0) {
                coc_tx_mtu = info.peer_coc_mtu;
                portENTER_CRITICAL(&status_lock);
                status.coc_connected = true;
                status.peer_coc_mtu = info.peer_coc_mtu;
                status.peer_mps = info.peer_l2cap_mtu;
                portEXIT_CRITICAL(&status_lock);
                ESP_LOGI(tag, "CoC ready: peer_mtu=%u peer_mps=%u", info.peer_coc_mtu,
                         info.peer_l2cap_mtu);
                preferred_phy_pending = true;
                const int result = ble_gap_set_prefered_le_phy(
                    connection_handle, BLE_HCI_LE_PHY_2M_PREF_MASK,
                    BLE_HCI_LE_PHY_2M_PREF_MASK, BLE_GAP_LE_PHY_CODED_ANY);
                if (result == 0) {
                    ESP_LOGI(tag, "requested 2M PHY; 15 ms request will follow PHY completion");
                } else {
                    preferred_phy_pending = false;
                    ESP_LOGW(tag, "could not request 2M PHY: %d", result);
                    // No PHY procedure is in flight, so the interval request can go out now.
                    request_fast_connection_interval();
                }
            } else {
                ESP_LOGE(tag, "could not read CoC channel information");
                ble_l2cap_disconnect(coc_channel);
            }
            return 0;
        }
        case BLE_L2CAP_EVENT_COC_DATA_RECEIVED: {
            const uint16_t length = OS_MBUF_PKTLEN(event->receive.sdu_rx);
            if (length > sizeof(receive_bytes) ||
                os_mbuf_copydata(event->receive.sdu_rx, 0, length, receive_bytes) != 0) {
                os_mbuf_free_chain(event->receive.sdu_rx);
                ESP_LOGE(tag, "invalid CoC RX SDU: length=%u", length);
                ble_l2cap_disconnect(event->receive.chan);
                return BLE_HS_EBADDATA;
            }
            os_mbuf_free_chain(event->receive.sdu_rx);
            portENTER_CRITICAL(&status_lock);
            status.rx_bytes += length;
            portEXIT_CRITICAL(&status_lock);
            size_t offset = 0;
            while (offset < length) {
                size_t consumed = 0;
                const bridge_parse_result_t result = bridge_parser_feed(
                    &parser, receive_bytes + offset, length - offset, &consumed, &receive_frame);
                offset += consumed;
                if (result == BRIDGE_PROTOCOL_ERROR) {
                    portENTER_CRITICAL(&status_lock);
                    status.protocol_errors++;
                    portEXIT_CRITICAL(&status_lock);
                    ESP_LOGE(tag, "bridge protocol error after %zu/%u RX bytes", offset, length);
                    ble_l2cap_disconnect(event->receive.chan);
                    return BLE_HS_EBADDATA;
                }
                if (result == BRIDGE_FRAME_READY) {
                    portENTER_CRITICAL(&status_lock);
                    status.rx_frames++;
                    portEXIT_CRITICAL(&status_lock);
                    respond(&receive_frame);
                }
            }
            return supply_receive_buffer(event->receive.chan);
        }
        case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
            tx_stalled = false;
            if (event->tx_unstalled.status == 0) drain_tx();
            return 0;
        case BLE_L2CAP_EVENT_COC_DISCONNECTED:
            set_bridge_ready(false);
            coc_channel = NULL;
            coc_tx_mtu = 0;
            tx_stalled = false;
            preferred_phy_pending = false;
            portENTER_CRITICAL(&status_lock);
            tx_head = tx_count = 0;
            status.coc_connected = false;
            status.peer_coc_mtu = 0;
            status.peer_mps = 0;
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI(tag, "CoC disconnected");
            return 0;
        default:
            return 0;
    }
}

static void update_connection_status(uint16_t handle) {
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(handle, &desc) != 0) return;
    portENTER_CRITICAL(&status_lock);
    status.connected = true;
    status.encrypted = desc.sec_state.encrypted;
    status.bonded = desc.sec_state.bonded;
    if (desc.sec_state.bonded) status.has_bond = true;
    status.connection_interval_units = desc.conn_itvl;
    portEXIT_CRITICAL(&status_lock);
}

static int gap_event(struct ble_gap_event* event, void* argument) {
    (void)argument;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                ESP_LOGW(tag, "BLE connection failed: %d", event->connect.status);
                advertise();
                return 0;
            }
            connection_handle = event->connect.conn_handle;
            preferred_phy_pending = false;
            ESP_LOGI(tag, "BLE connected: handle=%u", connection_handle);
            set_bool(&status.advertising, false);
            update_connection_status(connection_handle);
            if (!peer_allowed(connection_handle)) {
                ble_gap_terminate(connection_handle, BLE_ERR_AUTH_FAIL);
                return 0;
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(tag, "BLE disconnected: reason=%d", event->disconnect.reason);
            set_bridge_ready(false);
            connection_handle = BLE_HS_CONN_HANDLE_NONE;
            coc_channel = NULL;
            coc_tx_mtu = 0;
            tx_stalled = false;
            preferred_phy_pending = false;
            portENTER_CRITICAL(&status_lock);
            tx_head = tx_count = 0;
            status.connected = false;
            status.encrypted = false;
            status.bonded = false;
            status.coc_connected = false;
            status.peer_coc_mtu = 0;
            status.peer_mps = 0;
            status.passkey = 0;
            status.last_disconnect_reason = event->disconnect.reason;
            portEXIT_CRITICAL(&status_lock);
            if (bond_clear_pending()) {
                finish_bond_clear();
            } else {
                advertise();
            }
            return 0;
        case BLE_GAP_EVENT_CONN_UPDATE: {
            update_connection_status(event->conn_update.conn_handle);
            portENTER_CRITICAL(&status_lock);
            const uint16_t interval = status.connection_interval_units;
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI(tag, "BLE connection parameters updated: status=%d interval=%.2f ms",
                     event->conn_update.status, (double)interval * 1.25);
            return 0;
        }
        case BLE_GAP_EVENT_ENC_CHANGE: {
            update_connection_status(event->enc_change.conn_handle);
            portENTER_CRITICAL(&status_lock);
            status.passkey = 0;
            const bool encrypted = status.encrypted;
            const bool bonded = status.bonded;
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI(tag, "BLE security updated: status=%d encrypted=%d bonded=%d",
                     event->enc_change.status, encrypted, bonded);
            return 0;
        }
        case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE: {
            portENTER_CRITICAL(&status_lock);
            status.tx_phy = event->phy_updated.tx_phy;
            status.rx_phy = event->phy_updated.rx_phy;
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI(tag, "BLE PHY updated: status=%d tx=%u rx=%u", event->phy_updated.status,
                     event->phy_updated.tx_phy, event->phy_updated.rx_phy);
            if (!preferred_phy_pending || event->phy_updated.conn_handle != connection_handle) {
                return 0;
            }
            preferred_phy_pending = false;
            if (event->phy_updated.status != 0) {
                ESP_LOGW(tag, "PHY update failed; still requesting 15 ms interval");
            }
            request_fast_connection_interval();
            return 0;
        }
        case BLE_GAP_EVENT_DATA_LEN_CHG:
            ESP_LOGI(tag, "BLE data length updated: tx=%u/%u us rx=%u/%u us",
                     event->data_len_chg.max_tx_octets, event->data_len_chg.max_tx_time,
                     event->data_len_chg.max_rx_octets, event->data_len_chg.max_rx_time);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) != 0) {
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            ble_store_util_delete_peer(&desc.peer_id_addr);
            set_bool(&status.has_bond, false);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            if (event->passkey.params.action != BLE_SM_IOACT_DISP) return 0;
            struct ble_sm_io passkey;
            memset(&passkey, 0, sizeof(passkey));
            passkey.action = BLE_SM_IOACT_DISP;
            passkey.passkey = 100000U + esp_random() % 900000U;
            portENTER_CRITICAL(&status_lock);
            status.passkey = passkey.passkey;
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI(tag, "pairing passkey: %06" PRIu32, passkey.passkey);
            return ble_sm_inject_io(event->passkey.conn_handle, &passkey);
        }
        default:
            return 0;
    }
}

static void advertise(void) {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t*)&service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) return;

    struct ble_hs_adv_fields response;
    memset(&response, 0, sizeof(response));
    const char* name = ble_svc_gap_device_name();
    response.name = (uint8_t*)name;
    response.name_len = (uint8_t)strlen(name);
    response.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&response);

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    if (ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL) == 0) {
        set_bool(&status.advertising, true);
        ESP_LOGI(tag, "advertising bridge service");
    }
}

static void on_sync(void) {
    int result;
    if (identity_address_loaded) {
        result = ble_hs_id_set_rnd(identity_address);
        own_address_type = BLE_OWN_ADDR_RANDOM;
    } else {
        result = ble_hs_util_ensure_addr(0);
        if (result == 0) result = ble_hs_id_infer_auto(0, &own_address_type);
    }
    if (result != 0) {
        ESP_LOGE(tag, "no BLE identity address");
        return;
    }
    ble_addr_t peer;
    set_bool(&status.has_bond, bonded_peers(&peer) > 0);
    set_bool(&status.initialized, true);
    result = ble_gap_set_prefered_default_le_phy(BLE_HCI_LE_PHY_2M_PREF_MASK,
                                                  BLE_HCI_LE_PHY_2M_PREF_MASK);
    if (result != 0) {
        ESP_LOGW(tag, "could not set default 2M PHY preference: %d", result);
    }
    if (ble_l2cap_create_server(BAJJI_BRIDGE_PSM, BAJJI_BRIDGE_MTU, l2cap_event, NULL) != 0) {
        ESP_LOGE(tag, "could not create CoC server");
        return;
    }
    ESP_LOGI(tag, "CoC server ready: psm=0x%04x rx_mtu=%u", BAJJI_BRIDGE_PSM,
             BAJJI_BRIDGE_MTU);
    advertise();
}

static void host_task(void* argument) {
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_link_start(void) {
    esp_err_t result = load_device_id();
    if (result != ESP_OK) return result;
    result = load_identity_address();
    if (result != ESP_OK) return result;
    result = nimble_port_init();
    if (result != ESP_OK) return result;

    if (os_mempool_init(&receive_pool, kReceiveBufferCount, BAJJI_BRIDGE_MTU,
                        receive_memory, "bajji_coc_rx") != 0 ||
        os_mbuf_pool_init(&receive_mbuf_pool, &receive_pool, BAJJI_BRIDGE_MTU,
                          kReceiveBufferCount) != 0) return ESP_ERR_NO_MEM;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set("Bajji StopWatch") != 0 ||
        ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) return ESP_FAIL;

    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_npl_event_init(&clear_bond_event, clear_bond_on_host, NULL);
    ble_npl_event_init(&advertise_event, advertise_on_host, NULL);
    ble_npl_event_init(&tx_event, tx_on_host, NULL);
    clear_bond_event_initialized = true;
    tx_event_initialized = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

ble_link_status_t ble_link_snapshot(void) {
    portENTER_CRITICAL(&status_lock);
    ble_link_status_t snapshot = status;
    portEXIT_CRITICAL(&status_lock);
    return snapshot;
}

esp_err_t ble_link_ping(void) {
    bridge_frame_t frame = {.type = BRIDGE_TYPE_PING, .payload_len = 8};
    const uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000U;
    portENTER_CRITICAL(&status_lock);
    const bool ready = status.bridge_ready;
    frame.sequence = ping_sequence++;
    portEXIT_CRITICAL(&status_lock);
    if (!ready) return ESP_ERR_INVALID_STATE;
    for (size_t index = 0; index < 8; ++index) {
        frame.payload[index] = (uint8_t)(now_ms >> ((7U - index) * 8U));
    }
    const esp_err_t result = ble_link_send(&frame);
    if (result == ESP_OK) {
        portENTER_CRITICAL(&status_lock);
        status.pings_sent++;
        portEXIT_CRITICAL(&status_lock);
    }
    return result;
}

esp_err_t ble_link_clear_bond(void) {
    if (!clear_bond_event_initialized) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    const bool already_pending = clear_bond_pending;
    clear_bond_pending = true;
    portEXIT_CRITICAL(&status_lock);
    if (!already_pending && !ble_npl_event_is_queued(&clear_bond_event)) {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &clear_bond_event);
    }
    return ESP_OK;
}

esp_err_t ble_link_set_handlers(ble_link_frame_handler_t next_frame_handler,
                                ble_link_ready_handler_t next_ready_handler,
                                void* context) {
    portENTER_CRITICAL(&status_lock);
    frame_handler = next_frame_handler;
    ready_handler = next_ready_handler;
    handler_context = context;
    portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}

esp_err_t ble_link_set_provision_handler(ble_link_provision_handler_t handler, void* context) {
    portENTER_CRITICAL(&status_lock);
    provision_handler = handler;
    provision_context = context;
    portEXIT_CRITICAL(&status_lock);
    ESP_LOGI(tag, "Wi-Fi provisioning handler %s", handler ? "registered" : "cleared");
    return ESP_OK;
}
