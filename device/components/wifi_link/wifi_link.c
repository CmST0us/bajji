// SPDX-License-Identifier: MIT
#include "wifi_link.h"

#include <inttypes.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ip_bridge.h"
#include "wifi_provision.h"

enum { kReconnectDelayMs = 5000 };

static const char* tag = "wifi_link";
static esp_netif_t* station_netif;
static QueueHandle_t provision_queue;
static esp_timer_handle_t reconnect_timer;
static wifi_link_status_t status;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static bool sntp_started;

static bool configured(void) {
    portENTER_CRITICAL(&status_lock);
    const bool value = status.configured;
    portEXIT_CRITICAL(&status_lock);
    return value;
}

static const char* security_name(wifi_provision_security_t security) {
    switch (security) {
        case WIFI_PROVISION_OPEN: return "open";
        case WIFI_PROVISION_WEP: return "wep";
        case WIFI_PROVISION_WPA: return "wpa";
        case WIFI_PROVISION_OWE: return "owe";
        case WIFI_PROVISION_WPA2: return "wpa2";
        case WIFI_PROVISION_WPA3: return "wpa3";
        default: return "unknown";
    }
}

static const char* disconnect_reason_name(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_STA_LEAVING: return "sta_leaving";
        case WIFI_REASON_BEACON_TIMEOUT: return "beacon_timeout";
        case WIFI_REASON_NO_AP_FOUND: return "no_ap_found";
        case WIFI_REASON_AUTH_FAIL: return "auth_fail";
        case WIFI_REASON_ASSOC_FAIL: return "assoc_fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake_timeout";
        case WIFI_REASON_CONNECTION_FAIL: return "connection_fail";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "no_compatible_security";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "below_authmode_threshold";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "below_rssi_threshold";
        default: return "other";
    }
}

static void request_connect(const char* source) {
    if (!configured()) {
        ESP_LOGI(tag, "Wi-Fi connect skipped: source=%s configured=0", source);
        return;
    }
    const esp_err_t result = esp_wifi_connect();
    if (result == ESP_OK) {
        ESP_LOGI(tag, "Wi-Fi connect requested: source=%s", source);
        if (esp_timer_is_active(reconnect_timer)) {
            const esp_err_t stop_result = esp_timer_stop(reconnect_timer);
            ESP_LOGI(tag, "cancelled stale Wi-Fi reconnect timer: result=%s (0x%x)",
                     esp_err_to_name(stop_result), (unsigned)stop_result);
        }
    } else {
        ESP_LOGW(tag, "Wi-Fi connect request failed: source=%s error=%s (0x%x)", source,
                 esp_err_to_name(result), (unsigned)result);
    }
}

static void reconnect(void* argument) {
    (void)argument;
    ESP_LOGI(tag, "Wi-Fi reconnect timer fired");
    request_connect("reconnect_timer");
}

static void schedule_reconnect(void) {
    if (!configured()) {
        ESP_LOGI(tag, "Wi-Fi reconnect not scheduled: no saved configuration");
        return;
    }
    if (esp_timer_is_active(reconnect_timer)) {
        ESP_LOGD(tag, "Wi-Fi reconnect already scheduled");
        return;
    }
    const esp_err_t result = esp_timer_start_once(reconnect_timer, kReconnectDelayMs * 1000ULL);
    if (result == ESP_OK) {
        ESP_LOGI(tag, "Wi-Fi reconnect scheduled: delay_ms=%d", kReconnectDelayMs);
    } else {
        ESP_LOGW(tag, "could not schedule reconnect: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
    }
}

static wifi_auth_mode_t auth_mode(wifi_provision_security_t security) {
    switch (security) {
        case WIFI_PROVISION_OPEN: return WIFI_AUTH_OPEN;
        case WIFI_PROVISION_WEP: return WIFI_AUTH_WEP;
        case WIFI_PROVISION_WPA: return WIFI_AUTH_WPA_PSK;
        // OWE transition mode requires an open scan threshold; owe_enabled still makes
        // the driver prefer encrypted OWE. See esp-idf/docs/en/api-guides/wifi-security.rst:166.
        case WIFI_PROVISION_OWE: return WIFI_AUTH_OPEN;
        case WIFI_PROVISION_WPA2: return WIFI_AUTH_WPA2_PSK;
        case WIFI_PROVISION_WPA3: return WIFI_AUTH_WPA3_PSK;
        default: return WIFI_AUTH_MAX;
    }
}

static void apply_credentials(const wifi_provision_credentials_t* credentials) {
    ESP_LOGI(tag, "applying Wi-Fi credentials: ssid_bytes=%u security=%s password_bytes=%u",
             credentials->ssid_length, security_name(credentials->security),
             credentials->password_length);
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credentials->ssid, credentials->ssid_length);
    memcpy(config.sta.password, credentials->password, credentials->password_length);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = auth_mode(credentials->security);
    config.sta.pmf_cfg.capable = true;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config.sta.owe_enabled = credentials->security == WIFI_PROVISION_OWE;

    const esp_err_t disconnect_result = esp_wifi_disconnect();
    if (disconnect_result == ESP_OK) {
        ESP_LOGI(tag, "requested disconnect before applying Wi-Fi credentials");
    } else {
        ESP_LOGD(tag, "pre-provision Wi-Fi disconnect returned: %s (0x%x)",
                 esp_err_to_name(disconnect_result), (unsigned)disconnect_result);
    }
    const esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (result != ESP_OK) {
        portENTER_CRITICAL(&status_lock);
        status.last_error = result;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGE(tag, "could not save Wi-Fi configuration: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return;
    }
    portENTER_CRITICAL(&status_lock);
    status.configured = true;
    status.last_error = ESP_OK;
    portEXIT_CRITICAL(&status_lock);
    ESP_LOGI(tag, "saved Wi-Fi configuration: ssid_bytes=%u security=%s auth_threshold=%d owe=%d",
             credentials->ssid_length, security_name(credentials->security),
             config.sta.threshold.authmode, config.sta.owe_enabled);
    request_connect("provisioning");
}

static void provision_worker(void* argument) {
    (void)argument;
    wifi_provision_credentials_t credentials;
    while (true) {
        if (xQueueReceive(provision_queue, &credentials, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(tag, "dequeued Wi-Fi provisioning request");
            apply_credentials(&credentials);
        }
    }
}

static void wifi_event(void* argument, esp_event_base_t base, int32_t id, void* data) {
    (void)argument;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(tag, "Wi-Fi station started: configured=%d", configured());
        request_connect("station_start");
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t* event = data;
        ESP_LOGI(tag, "Wi-Fi associated: channel=%u auth_mode=%d aid=%u",
                 event ? event->channel : 0, event ? event->authmode : WIFI_AUTH_MAX,
                 event ? event->aid : 0);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t* event = data;
        ip_bridge_set_wifi_netif(station_netif, false);
        portENTER_CRITICAL(&status_lock);
        status.connected = false;
        status.rssi = 0;
        status.reconnects++;
        status.last_error = event ? event->reason : WIFI_REASON_UNSPECIFIED;
        const uint32_t reconnects = status.reconnects;
        portEXIT_CRITICAL(&status_lock);
        const uint8_t reason = event ? event->reason : WIFI_REASON_UNSPECIFIED;
        ESP_LOGW(tag, "Wi-Fi disconnected: reason=%u (%s) rssi=%d reconnects=%" PRIu32,
                 reason, disconnect_reason_name(reason), event ? event->rssi : 0, reconnects);
        schedule_reconnect();
        return;
    }
    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) return;

    const ip_event_got_ip_t* event = data;
    wifi_ap_record_t access_point = {0};
    const esp_err_t ap_result = esp_wifi_sta_get_ap_info(&access_point);
    if (ap_result != ESP_OK) {
        ESP_LOGW(tag, "could not read connected AP details: %s (0x%x)",
                 esp_err_to_name(ap_result), (unsigned)ap_result);
    }
    if (esp_timer_is_active(reconnect_timer)) {
        const esp_err_t stop_result = esp_timer_stop(reconnect_timer);
        ESP_LOGI(tag, "Wi-Fi reconnect timer stop: result=%s (0x%x)",
                 esp_err_to_name(stop_result), (unsigned)stop_result);
    }
    portENTER_CRITICAL(&status_lock);
    status.connected = true;
    status.rssi = access_point.rssi;
    status.last_error = ESP_OK;
    portEXIT_CRITICAL(&status_lock);
    ip_bridge_set_wifi_netif(station_netif, true);
    if (!sntp_started) {
        const esp_err_t result = esp_netif_sntp_start();
        sntp_started = result == ESP_OK;
        if (result == ESP_OK) ESP_LOGI(tag, "SNTP started: server=pool.ntp.org");
        else ESP_LOGW(tag, "could not start SNTP: %s (0x%x)",
                      esp_err_to_name(result), (unsigned)result);
    }
    if (event) {
        ESP_LOGI(tag, "Wi-Fi got IPv4: ip=" IPSTR " mask=" IPSTR " gateway=" IPSTR
                      " changed=%d rssi=%d dBm",
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask),
                 IP2STR(&event->ip_info.gw), event->ip_changed, access_point.rssi);
    } else {
        ESP_LOGI(tag, "Wi-Fi got IPv4 event without address data: rssi=%d dBm",
                 access_point.rssi);
    }
}

esp_err_t wifi_link_start(void) {
    portENTER_CRITICAL(&status_lock);
    const bool already_started = status.initialized;
    portEXIT_CRITICAL(&status_lock);
    if (already_started) {
        ESP_LOGI(tag, "Wi-Fi link already initialized");
        return ESP_OK;
    }
    ESP_LOGI(tag, "initializing Wi-Fi station link");

    station_netif = esp_netif_create_default_wifi_sta();
    provision_queue = xQueueCreate(1, sizeof(wifi_provision_credentials_t));
    if (!station_netif || !provision_queue) {
        ESP_LOGE(tag, "could not allocate Wi-Fi station resources: netif=%d queue=%d",
                 station_netif != NULL, provision_queue != NULL);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(tag, "Wi-Fi station netif and provisioning queue ready");

    const esp_timer_create_args_t reconnect_args = {
        .callback = reconnect,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_reconnect",
        .skip_unhandled_events = true,
    };
    esp_err_t result = esp_timer_create(&reconnect_args, &reconnect_timer);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "could not create reconnect timer: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }
    ESP_LOGI(tag, "Wi-Fi reconnect timer ready");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "could not initialize Wi-Fi driver: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }
    ESP_LOGI(tag, "Wi-Fi driver initialized");
    if ((result = esp_wifi_set_storage(WIFI_STORAGE_FLASH)) != ESP_OK) {
        ESP_LOGE(tag, "could not select flash Wi-Fi storage: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }
    if ((result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
        ESP_LOGE(tag, "could not select Wi-Fi station mode: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }
    if ((result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             wifi_event, NULL)) != ESP_OK ||
        (result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             wifi_event, NULL)) != ESP_OK) {
        ESP_LOGE(tag, "could not register Wi-Fi event handlers: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }
    ESP_LOGI(tag, "Wi-Fi storage, station mode, and event handlers ready");

    wifi_config_t saved = {0};
    result = esp_wifi_get_config(WIFI_IF_STA, &saved);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "could not read saved Wi-Fi configuration: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }
    portENTER_CRITICAL(&status_lock);
    status.configured = saved.sta.ssid[0] != 0;
    const bool has_saved_config = status.configured;
    portEXIT_CRITICAL(&status_lock);
    ESP_LOGI(tag, "saved Wi-Fi configuration: present=%d ssid_bytes=%zu auth_mode=%d",
             has_saved_config, strnlen((const char*)saved.sta.ssid, sizeof(saved.sta.ssid)),
             saved.sta.threshold.authmode);

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.start = false;
    result = esp_netif_sntp_init(&sntp);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "could not configure SNTP: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }
    ESP_LOGI(tag, "SNTP configured for deferred start");
    if (xTaskCreate(provision_worker, "wifi_provision", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(tag, "could not create Wi-Fi provisioning worker");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(tag, "Wi-Fi provisioning worker ready");
    result = esp_wifi_start();
    if (result == ESP_OK) {
        portENTER_CRITICAL(&status_lock);
        status.initialized = true;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGI(tag, "Wi-Fi station link initialized");
    } else {
        ESP_LOGE(tag, "could not start Wi-Fi station: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
    }
    return result;
}

esp_err_t wifi_link_provision(const uint8_t* payload, size_t length) {
    ESP_LOGI(tag, "received Wi-Fi provisioning payload: bytes=%zu", length);
    wifi_provision_credentials_t credentials;
    if (!wifi_provision_decode(payload, length, &credentials)) {
        ESP_LOGW(tag, "rejected Wi-Fi provisioning payload: bytes=%zu version=%u security=%u",
                 length, payload && length > 0 ? payload[0] : 0,
                 payload && length > 1 ? payload[1] : 0);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(tag, "decoded Wi-Fi provisioning payload: ssid_bytes=%u security=%s password_bytes=%u",
             credentials.ssid_length, security_name(credentials.security),
             credentials.password_length);
#if CONFIG_BAJJI_WIFI_LOG_CREDENTIALS
    ESP_LOGW(tag, "SENSITIVE debug credentials: ssid=\"%.*s\" password=\"%.*s\"",
             credentials.ssid_length, (const char*)credentials.ssid,
             credentials.password_length, (const char*)credentials.password);
#endif
    if (!provision_queue) {
        ESP_LOGE(tag, "cannot queue Wi-Fi provisioning payload: link not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    const BaseType_t queued = xQueueOverwrite(provision_queue, &credentials);
    ESP_LOGI(tag, "Wi-Fi provisioning queue result: queued=%d", queued == pdTRUE);
    return queued == pdTRUE ? ESP_OK : ESP_FAIL;
}

wifi_link_status_t wifi_link_snapshot(void) {
    portENTER_CRITICAL(&status_lock);
    const wifi_link_status_t snapshot = status;
    portEXIT_CRITICAL(&status_lock);
    return snapshot;
}
