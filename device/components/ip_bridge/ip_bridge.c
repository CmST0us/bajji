// SPDX-License-Identifier: MIT
#include "ip_bridge.h"

#include <string.h>
#include <sys/time.h>

#include "ble_link.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "ipv4_packet.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"

#define BAJJI_IPV4_U32 0x0a4d0002U

static const char* tag = "ip_bridge";
static struct netif bridge_netif;
static ip_bridge_status_t status;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t tx_sequence = 1;
static uint32_t request_dumped;
static uint32_t response_dumped;
// lwIP invokes netif output on its single tcpip task; keeping this off its 3 KiB stack avoids overflow.
static bridge_frame_t tx_frame;

static void log_ipv4_packet(const char* direction, uint16_t sequence, const uint8_t* packet,
                            size_t length, bool dump_payload, uint32_t* dumped, uint32_t limit) {
    if (*dumped >= limit) return;
    (*dumped)++;
    const size_t header_length = (size_t)(packet[0] & 0x0fU) * 4U;
    const uint8_t protocol = packet[9];
    const bool has_ports = (protocol == 6U || protocol == 17U) && length >= header_length + 4U;
    const uint16_t source_port = has_ports
                                     ? (uint16_t)((uint16_t)packet[header_length] << 8U) |
                                           packet[header_length + 1U]
                                     : 0;
    const uint16_t destination_port = has_ports
                                          ? (uint16_t)((uint16_t)packet[header_length + 2U] << 8U) |
                                                packet[header_length + 3U]
                                          : 0;
    const uint8_t tcp_flags = protocol == 6U && length >= header_length + 14U
                                  ? packet[header_length + 13U]
                                  : 0;
    ESP_LOGI(tag,
             "%s packet %lu/%lu seq=%u len=%u %u.%u.%u.%u:%u -> "
             "%u.%u.%u.%u:%u proto=%u tcp_flags=0x%02x",
             direction, (unsigned long)*dumped, (unsigned long)limit, (unsigned)sequence,
             (unsigned)length, (unsigned)packet[12], (unsigned)packet[13], (unsigned)packet[14],
             (unsigned)packet[15], (unsigned)source_port, (unsigned)packet[16],
             (unsigned)packet[17], (unsigned)packet[18], (unsigned)packet[19],
             (unsigned)destination_port, (unsigned)protocol, (unsigned)tcp_flags);
    if (dump_payload) {
        const size_t dump_length = length < 96U ? length : 96U;
        ESP_LOG_BUFFER_HEX_LEVEL(tag, packet, dump_length, ESP_LOG_INFO);
    }
    if (*dumped == limit) ESP_LOGI(tag, "%s packet dump limit reached", direction);
}

static err_t bridge_output(struct netif* netif, struct pbuf* packet,
                           const ip4_addr_t* destination) {
    (void)netif;
    (void)destination;
    if (!packet || packet->tot_len > BRIDGE_MAX_PAYLOAD) return ERR_VAL;

    tx_frame.type = BRIDGE_TYPE_IPV4;
    tx_frame.payload_len = packet->tot_len;
    portENTER_CRITICAL(&status_lock);
    tx_frame.sequence = tx_sequence++;
    portEXIT_CRITICAL(&status_lock);
    if (pbuf_copy_partial(packet, tx_frame.payload, packet->tot_len, 0) != packet->tot_len ||
        ipv4_packet_validate(tx_frame.payload, tx_frame.payload_len, BAJJI_IPV4_U32, true) !=
            IPV4_PACKET_OK) {
        portENTER_CRITICAL(&status_lock);
        status.invalid_packets++;
        portEXIT_CRITICAL(&status_lock);
        return ERR_VAL;
    }
    log_ipv4_packet("OUT", tx_frame.sequence, tx_frame.payload, tx_frame.payload_len, true,
                    &request_dumped, 24);

    const esp_err_t result = ble_link_send(&tx_frame);
    portENTER_CRITICAL(&status_lock);
    if (result == ESP_OK) {
        status.tx_packets++;
        status.tx_bytes += tx_frame.payload_len;
    } else {
        status.dropped_packets++;
        status.last_error = result;
    }
    portEXIT_CRITICAL(&status_lock);
    return result == ESP_OK ? ERR_OK : ERR_MEM;
}

static err_t bridge_netif_init(struct netif* netif) {
    netif->name[0] = 'b';
    netif->name[1] = 'j';
    netif->output = bridge_output;
    netif->mtu = BRIDGE_MAX_PAYLOAD;
    return ERR_OK;
}

esp_err_t ip_bridge_start(void) {
    portENTER_CRITICAL(&status_lock);
    const bool already_started = status.started;
    portEXIT_CRITICAL(&status_lock);
    if (already_started) return ESP_OK;

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK) return result;

    ip4_addr_t address;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
    IP4_ADDR(&address, 10, 77, 0, 2);
    IP4_ADDR(&netmask, 255, 255, 255, 252);
    IP4_ADDR(&gateway, 10, 77, 0, 1);
    if (netifapi_netif_add(&bridge_netif, &address, &netmask, &gateway, NULL,
                           bridge_netif_init, tcpip_input) != ERR_OK ||
        netifapi_netif_set_default(&bridge_netif) != ERR_OK ||
        netifapi_netif_set_up(&bridge_netif) != ERR_OK ||
        netifapi_netif_set_link_down(&bridge_netif) != ERR_OK) {
        return ESP_FAIL;
    }

    ip_addr_t dns;
    IP_ADDR4(&dns, 1, 1, 1, 1);
    dns_setserver(0, &dns);
    portENTER_CRITICAL(&status_lock);
    status.started = true;
    portEXIT_CRITICAL(&status_lock);
    ESP_LOGI(tag, "default IPv4 netif ready: 10.77.0.2/30 gateway=10.77.0.1 mtu=%u",
             BRIDGE_MAX_PAYLOAD);
    return ESP_OK;
}

void ip_bridge_set_link(bool up) {
    portENTER_CRITICAL(&status_lock);
    const bool started = status.started;
    const bool changed = status.link_up != up;
    status.link_up = started && up;
    portEXIT_CRITICAL(&status_lock);
    if (!started || !changed) return;

    const err_t result = up ? netifapi_netif_set_link_up(&bridge_netif)
                            : netifapi_netif_set_link_down(&bridge_netif);
    if (result != ERR_OK) {
        portENTER_CRITICAL(&status_lock);
        status.last_error = result;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGE(tag, "could not set link %s: %d", up ? "up" : "down", result);
    } else {
        ESP_LOGI(tag, "IPv4 link %s", up ? "up" : "down");
    }
}

static void receive_ipv4(const bridge_frame_t* frame) {
    if (ipv4_packet_validate(frame->payload, frame->payload_len, BAJJI_IPV4_U32, false) !=
        IPV4_PACKET_OK) {
        portENTER_CRITICAL(&status_lock);
        status.invalid_packets++;
        portEXIT_CRITICAL(&status_lock);
        return;
    }
    log_ipv4_packet("IN", frame->sequence, frame->payload, frame->payload_len, false,
                    &response_dumped, 12);
    struct pbuf* packet = pbuf_alloc(PBUF_RAW, frame->payload_len, PBUF_POOL);
    if (!packet || pbuf_take(packet, frame->payload, frame->payload_len) != ERR_OK) {
        if (packet) pbuf_free(packet);
        portENTER_CRITICAL(&status_lock);
        status.dropped_packets++;
        status.last_error = ERR_MEM;
        portEXIT_CRITICAL(&status_lock);
        return;
    }
    const err_t result = bridge_netif.input(packet, &bridge_netif);
    portENTER_CRITICAL(&status_lock);
    if (result == ERR_OK) {
        status.rx_packets++;
        status.rx_bytes += frame->payload_len;
    } else {
        status.dropped_packets++;
        status.last_error = result;
    }
    portEXIT_CRITICAL(&status_lock);
    if (result != ERR_OK) pbuf_free(packet);
}

static void receive_time(const bridge_frame_t* frame) {
    uint64_t seconds = 0;
    for (size_t index = 0; index < 8; ++index) seconds = (seconds << 8U) | frame->payload[index];
    if (seconds < 1704067200ULL || seconds >= 4102444800ULL) {
        portENTER_CRITICAL(&status_lock);
        status.invalid_packets++;
        portEXIT_CRITICAL(&status_lock);
        return;
    }
    const struct timeval now = {.tv_sec = (time_t)seconds, .tv_usec = 0};
    const int result = settimeofday(&now, NULL);
    portENTER_CRITICAL(&status_lock);
    status.time_valid = result == 0;
    if (result != 0) status.last_error = result;
    portEXIT_CRITICAL(&status_lock);
    if (result == 0) ESP_LOGI(tag, "clock synchronized");
}

void ip_bridge_receive(const bridge_frame_t* frame) {
    if (!frame) return;
    if (frame->type == BRIDGE_TYPE_IPV4) {
        receive_ipv4(frame);
    } else if (frame->type == BRIDGE_TYPE_TIME_SYNC) {
        receive_time(frame);
    }
}

ip_bridge_status_t ip_bridge_snapshot(void) {
    portENTER_CRITICAL(&status_lock);
    const ip_bridge_status_t snapshot = status;
    portEXIT_CRITICAL(&status_lock);
    return snapshot;
}
