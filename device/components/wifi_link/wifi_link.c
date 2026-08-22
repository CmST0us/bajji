// SPDX-License-Identifier: MIT
#include "wifi_link.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ip_bridge.h"
#include "lwip/inet.h"
#include "wifi_provision.h"
#include "wifi_portal_protocol.h"

enum {
    kReconnectDelayMs = 5000,
    kPortalDurationSeconds = 300,
    kPortalSuccessSeconds = 10,
    kPortalNetworkLimit = 16,
    kPortalDnsPort = 53,
};

typedef enum {
    WIFI_COMMAND_PROVISION,
    WIFI_COMMAND_PORTAL_START,
    WIFI_COMMAND_PORTAL_STOP,
    WIFI_COMMAND_PORTAL_CONNECT,
    WIFI_COMMAND_PORTAL_SUCCESS,
    WIFI_COMMAND_PORTAL_FAILED,
} wifi_command_type_t;

typedef struct {
    wifi_command_type_t type;
    wifi_provision_credentials_t credentials;
    int32_t error;
} wifi_command_t;

static const char* tag = "wifi_link";
static esp_netif_t* station_netif;
static esp_netif_t* access_point_netif;
static QueueHandle_t provision_queue;
static esp_timer_handle_t reconnect_timer;
static esp_timer_handle_t portal_timeout_timer;
static esp_timer_handle_t portal_close_timer;
static wifi_link_status_t status;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static bool sntp_started;
static bool wifi_driver_started;
static bool portal_services_active;
static bool portal_trial_active;
static wifi_config_t portal_previous_config;
static wifi_config_t portal_trial_config;
static wifi_ap_record_t portal_networks[kPortalNetworkLimit];
static uint16_t portal_network_count;
static uint16_t portal_selected_network = UINT16_MAX;
static int64_t portal_deadline_us;
static char portal_token[33];
static httpd_handle_t portal_http_server;
static volatile bool portal_dns_running;
static int portal_dns_socket = -1;
static SemaphoreHandle_t portal_dns_stopped;
static TaskHandle_t portal_dns_task_handle;

static const char portal_html_before_token[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
    "<title>Bajji Wi-Fi 配网</title><style>"
    ":root{color-scheme:dark;--bg:#05070c;--surface:#0b1522;--line:#203247;"
    "--cyan:#54d7ff;--text:#f4f8ff;--muted:#a8b4c7;--ok:#74e6a5;--bad:#ff6b76}"
    "*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 50% 0,#102538,var(--bg) 55%);"
    "color:var(--text);font:16px -apple-system,BlinkMacSystemFont,'SF Pro Text',sans-serif;min-height:100vh}"
    "main{max-width:480px;margin:auto;padding:max(24px,env(safe-area-inset-top)) 20px 40px}"
    "header{margin:14px 0 24px}h1{font-size:30px;letter-spacing:-.6px;margin:6px 0}"
    ".eyebrow{color:var(--cyan);font-size:12px;font-weight:700;letter-spacing:1.5px}"
    ".meta,.hint{color:var(--muted);font-size:14px;line-height:1.5}.card{background:rgba(11,21,34,.94);"
    "border:1px solid var(--line);border-radius:22px;padding:18px;margin:14px 0;box-shadow:0 18px 50px #0008}"
    "h2{font-size:17px;margin:0 0 14px}.networks{display:grid;gap:8px}.network{appearance:none;width:100%;"
    "border:1px solid var(--line);background:#101a27;color:var(--text);border-radius:14px;padding:14px;"
    "text-align:left;font:inherit;display:flex;justify-content:space-between;gap:10px}.network[aria-pressed=true]{"
    "border-color:var(--cyan);box-shadow:0 0 0 1px var(--cyan)}.network small{color:var(--muted)}"
    "label{display:block;color:var(--muted);font-size:14px;margin:16px 0 8px}input{width:100%;"
    "border:1px solid var(--line);border-radius:14px;padding:14px;background:#07111d;color:var(--text);font:inherit}"
    "input:focus{outline:2px solid var(--cyan);outline-offset:1px}button.primary{width:100%;border:0;border-radius:16px;"
    "padding:15px;margin-top:16px;background:var(--cyan);color:#041017;font:700 16px -apple-system,sans-serif}"
    "button:disabled{opacity:.45}.status{display:none}.status.show{display:block}.status strong{display:block;"
    "font-size:20px;margin-bottom:8px}.ok{color:var(--ok)}.bad{color:var(--bad)}.spinner{display:inline-block;"
    "width:16px;height:16px;border:2px solid #ffffff40;border-top-color:var(--cyan);border-radius:50%;"
    "animation:spin .8s linear infinite;vertical-align:-2px;margin-right:8px}@keyframes spin{to{transform:rotate(360deg)}}"
    "</style></head><body><main><header><div class=\"eyebrow\">BAJJI SETUP</div><h1>连接家庭 Wi-Fi</h1>"
    "<div class=\"meta\"><span id=\"device\">Bajji</span> · 配网页面剩余 <span id=\"remaining\">--</span></div>"
    "</header><section class=\"card\" id=\"formCard\"><h2>选择网络</h2><div class=\"networks\" id=\"networks\">"
    "<div class=\"hint\"><span class=\"spinner\"></span>正在扫描附近网络…</div></div>"
    "<label for=\"password\">Wi-Fi 密码</label><input id=\"password\" type=\"password\" autocomplete=\"current-password\""
    " placeholder=\"至少 8 个字符\"><button class=\"primary\" id=\"connect\" disabled>连接 Wi-Fi</button>"
    "<p class=\"hint\">凭据仅保存在 Bajji 设备中，连接成功后配网热点会自动关闭。</p></section>"
    "<section class=\"card status\" id=\"status\" role=\"status\" aria-live=\"polite\"></section></main><script>const token='";

static const char portal_html_after_token[] =
    "';let list=[],selected=-1,needsPassword=true,lastState='';const $=id=>document.getElementById(id);"
    "function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}"
    "function draw(){const box=$('networks');box.innerHTML='';if(!list.length){box.innerHTML='<div class=hint>未发现可用网络，请稍后重新启动配网。</div>';return}"
    "list.forEach(n=>{const b=document.createElement('button');b.className='network';b.type='button';"
    "b.setAttribute('aria-pressed',n.id===selected);b.innerHTML='<span>'+esc(n.ssid)+'</span><small>'+"
    "(n.secure?'&#128274; ':'开放 · ')+n.rssi+' dBm</small>';b.onclick=()=>{selected=n.id;needsPassword=n.password;"
    "$('password').disabled=!needsPassword;if(!needsPassword)$('password').value='';$('password').placeholder=needsPassword?'至少 8 个字符':'此网络无需密码';"
    "$('connect').disabled=false;draw()};box.appendChild(b)})}"
    "async function load(){try{const [n,s]=await Promise.all([fetch('/api/networks',{cache:'no-store'}).then(r=>r.json()),"
    "fetch('/api/status',{cache:'no-store'}).then(r=>r.json())]);list=n.networks;selected=s.selected;const chosen=list.find(x=>x.id===selected);"
    "if(chosen){needsPassword=chosen.password;$('password').disabled=!needsPassword}$('formCard').style.display='block';draw();show(s)}"
    "catch(e){$('networks').innerHTML='<div class=bad>无法读取设备状态，请重新连接 Bajji 热点。</div>'}}"
    "function show(s){$('device').textContent=s.ap;$('remaining').textContent=s.remaining+' 秒';const box=$('status');"
    "if(s.state==='connecting'){box.className='card status show';box.innerHTML='<strong><span class=spinner></span>正在连接</strong>正在关联并获取 IP 地址，请保持本页打开。';$('connect').disabled=true}"
    "else if(s.state==='success'){box.className='card status show';box.innerHTML='<strong class=ok>连接成功</strong>Bajji 已保存此网络，配网热点将在 10 秒内关闭。';$('formCard').style.display='none'}"
    "else if(s.state==='failed'){box.className='card status show';box.innerHTML='<strong class=bad>连接失败</strong>请重新输入密码后重试，或选择另一个网络。';"
    "if(lastState!=='failed'){$('password').value='';if(needsPassword)$('password').focus()}$('connect').disabled=selected<0}"
    "else{box.className='card status';$('connect').disabled=selected<0}lastState=s.state}"
    "$('connect').onclick=async()=>{if(selected<0)return;const body=new URLSearchParams({token,network:String(selected),password:$('password').value});"
    "const r=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
    "if(!r.ok){const m=await r.text();$('status').className='card status show';$('status').innerHTML='<strong class=bad>无法开始连接</strong>'+esc(m);return}"
    "show({state:'connecting',ap:$('device').textContent,remaining:'--'})};load();setInterval(async()=>{try{show(await fetch('/api/status',{cache:'no-store'}).then(r=>r.json()))}catch(e){}},1000);"
    "</script></body></html>";

static bool portal_trial_is_active(void) {
    portENTER_CRITICAL(&status_lock);
    const bool active = portal_trial_active;
    portEXIT_CRITICAL(&status_lock);
    return active;
}

static bool portal_is_active(void) {
    portENTER_CRITICAL(&status_lock);
    const bool active = portal_services_active;
    portEXIT_CRITICAL(&status_lock);
    return active;
}

static BaseType_t enqueue_command(const wifi_command_t* command) {
    return provision_queue ? xQueueSend(provision_queue, command, 0) : pdFALSE;
}

static const char* portal_state_name(wifi_portal_state_t state) {
    switch (state) {
        case WIFI_PORTAL_STARTING: return "starting";
        case WIFI_PORTAL_READY: return "ready";
        case WIFI_PORTAL_CONNECTING: return "connecting";
        case WIFI_PORTAL_SUCCESS: return "success";
        case WIFI_PORTAL_FAILED: return "failed";
        case WIFI_PORTAL_OFF: return "off";
        default: return "off";
    }
}

static wifi_provision_security_t provision_security(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN: return WIFI_PROVISION_OPEN;
        case WIFI_AUTH_WEP: return WIFI_PROVISION_WEP;
        case WIFI_AUTH_WPA_PSK: return WIFI_PROVISION_WPA;
        case WIFI_AUTH_OWE: return WIFI_PROVISION_OWE;
        case WIFI_AUTH_WPA3_PSK: return WIFI_PROVISION_WPA3;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK: return WIFI_PROVISION_WPA2;
        default: return (wifi_provision_security_t)-1;
    }
}

static esp_err_t portal_root_get(httpd_req_t* request) {
    char token[sizeof(portal_token)];
    portENTER_CRITICAL(&status_lock);
    memcpy(token, portal_token, sizeof(token));
    portEXIT_CRITICAL(&status_lock);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_sendstr_chunk(request, portal_html_before_token) != ESP_OK ||
        httpd_resp_sendstr_chunk(request, token) != ESP_OK ||
        httpd_resp_sendstr_chunk(request, portal_html_after_token) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t json_send_escaped(httpd_req_t* request, const uint8_t* bytes, size_t length) {
    size_t start = 0;
    for (size_t index = 0; index < length; ++index) {
        const uint8_t value = bytes[index];
        if (value != '"' && value != '\\' && value >= 0x20) continue;
        if (index > start && httpd_resp_send_chunk(request, (const char*)bytes + start,
                                                   index - start) != ESP_OK) return ESP_FAIL;
        char escaped[7];
        const char* text = escaped;
        int escaped_length;
        if (value == '"' || value == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)value;
            escaped_length = 2;
        } else {
            escaped_length = snprintf(escaped, sizeof(escaped), "\\u%04x", value);
        }
        if (httpd_resp_send_chunk(request, text, escaped_length) != ESP_OK) return ESP_FAIL;
        start = index + 1;
    }
    return start < length
               ? httpd_resp_send_chunk(request, (const char*)bytes + start, length - start)
               : ESP_OK;
}

static esp_err_t portal_networks_get(httpd_req_t* request) {
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (httpd_resp_sendstr_chunk(request, "{\"networks\":[") != ESP_OK) return ESP_FAIL;
    for (uint16_t index = 0; index < portal_network_count; ++index) {
        const wifi_ap_record_t* network = &portal_networks[index];
        const size_t ssid_length = strnlen((const char*)network->ssid, sizeof(network->ssid));
        char prefix[80];
        const int prefix_length = snprintf(prefix, sizeof(prefix),
                                           "%s{\"id\":%u,\"ssid\":\"",
                                           index ? "," : "", index);
        if (httpd_resp_send_chunk(request, prefix, prefix_length) != ESP_OK ||
            json_send_escaped(request, network->ssid, ssid_length) != ESP_OK) return ESP_FAIL;
        char suffix[80];
        const bool secure = network->authmode != WIFI_AUTH_OPEN;
        const bool password = secure && network->authmode != WIFI_AUTH_OWE;
        const int suffix_length = snprintf(suffix, sizeof(suffix),
                                           "\",\"rssi\":%d,\"secure\":%s,\"password\":%s}",
                                           network->rssi, secure ? "true" : "false",
                                           password ? "true" : "false");
        if (httpd_resp_send_chunk(request, suffix, suffix_length) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(request, "]}") != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t portal_status_get(httpd_req_t* request) {
    const wifi_link_status_t snapshot = wifi_link_snapshot();
    int selected;
    portENTER_CRITICAL(&status_lock);
    selected = portal_selected_network == UINT16_MAX
                   ? -1 : (int)portal_selected_network;
    portEXIT_CRITICAL(&status_lock);
    char response[192];
    const int length = snprintf(response, sizeof(response),
                                "{\"state\":\"%s\",\"ap\":\"%s\",\"remaining\":%u,"
                                "\"selected\":%d,\"error\":%" PRId32 "}",
                                portal_state_name(snapshot.portal_state), snapshot.portal_ssid,
                                snapshot.portal_seconds_remaining,
                                selected,
                                snapshot.portal_last_error);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

static esp_err_t portal_send_error(httpd_req_t* request, const char* status_text,
                                   const char* message) {
    httpd_resp_set_status(request, status_text);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, message);
}

static esp_err_t portal_connect_post(httpd_req_t* request) {
    if (request->content_len <= 0 || request->content_len > 320) {
        return portal_send_error(request, "413 Payload Too Large", "请求内容过大");
    }
    char body[321];
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        const int count = httpd_req_recv(request, body + received,
                                         (size_t)request->content_len - received);
        if (count <= 0) return portal_send_error(request, "400 Bad Request", "请求内容不完整");
        received += (size_t)count;
    }
    body[received] = '\0';

    char token[sizeof(portal_token)];
    wifi_portal_state_t state;
    portENTER_CRITICAL(&status_lock);
    memcpy(token, portal_token, sizeof(token));
    state = status.portal_state;
    portEXIT_CRITICAL(&status_lock);
    if (state != WIFI_PORTAL_READY && state != WIFI_PORTAL_FAILED) {
        return portal_send_error(request, "409 Conflict", "设备正在处理另一个连接请求");
    }

    wifi_portal_form_t form;
    const bool valid_form = wifi_portal_decode_form(body, received, token, &form);
    memset(body, 0, sizeof(body));
    if (!valid_form ||
        form.network_index >= portal_network_count) {
        memset(&form, 0, sizeof(form));
        return portal_send_error(request, "400 Bad Request", "配网请求无效，请刷新页面重试");
    }
    const wifi_ap_record_t* network = &portal_networks[form.network_index];
    const wifi_provision_security_t security = provision_security(network->authmode);
    if ((int)security < 0 ||
        !wifi_provision_credentials_valid(security, form.password_length)) {
        memset(&form, 0, sizeof(form));
        return portal_send_error(request, "400 Bad Request", "密码长度或网络安全类型不受支持");
    }

    wifi_command_t command = {.type = WIFI_COMMAND_PORTAL_CONNECT};
    command.credentials.security = security;
    command.credentials.ssid_length = (uint8_t)strnlen(
        (const char*)network->ssid, WIFI_PROVISION_MAX_SSID);
    command.credentials.password_length = form.password_length;
    memcpy(command.credentials.ssid, network->ssid, command.credentials.ssid_length);
    memcpy(command.credentials.password, form.password, form.password_length);
    command.error = form.network_index;
    memset(form.password, 0, sizeof(form.password));
    if (enqueue_command(&command) != pdTRUE) {
        memset(&command.credentials, 0, sizeof(command.credentials));
        return portal_send_error(request, "503 Service Unavailable", "设备忙，请稍后重试");
    }
    memset(&command.credentials, 0, sizeof(command.credentials));
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "正在连接");
}

static esp_err_t portal_not_found(httpd_req_t* request, httpd_err_code_t error) {
    (void)error;
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_sendstr(request, "Open Bajji setup");
}

static esp_err_t start_portal_http(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 6;
    config.lru_purge_enable = true;
    if (httpd_start(&portal_http_server, &config) != ESP_OK) return ESP_FAIL;
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = portal_root_get};
    const httpd_uri_t networks = {
        .uri = "/api/networks", .method = HTTP_GET, .handler = portal_networks_get};
    const httpd_uri_t state = {
        .uri = "/api/status", .method = HTTP_GET, .handler = portal_status_get};
    const httpd_uri_t connect = {
        .uri = "/api/connect", .method = HTTP_POST, .handler = portal_connect_post};
    esp_err_t result = httpd_register_uri_handler(portal_http_server, &root);
    if (result == ESP_OK) result = httpd_register_uri_handler(portal_http_server, &networks);
    if (result == ESP_OK) result = httpd_register_uri_handler(portal_http_server, &state);
    if (result == ESP_OK) result = httpd_register_uri_handler(portal_http_server, &connect);
    if (result == ESP_OK) {
        result = httpd_register_err_handler(portal_http_server, HTTPD_404_NOT_FOUND,
                                            portal_not_found);
    }
    if (result != ESP_OK) {
        httpd_stop(portal_http_server);
        portal_http_server = NULL;
    }
    return result;
}

static void portal_dns_task(void* argument) {
    (void)argument;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    portal_dns_socket = socket_fd;
    if (socket_fd >= 0) {
        const struct timeval timeout = {.tv_sec = 0, .tv_usec = 500000};
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        const struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_port = htons(kPortalDnsPort),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(socket_fd, (const struct sockaddr*)&address, sizeof(address)) != 0) {
            ESP_LOGE(tag, "could not bind portal DNS socket: errno=%d", errno);
            portal_dns_running = false;
        }
    } else {
        ESP_LOGE(tag, "could not create portal DNS socket: errno=%d", errno);
        portal_dns_running = false;
    }

    esp_netif_ip_info_t ip_info = {0};
    esp_netif_get_ip_info(access_point_netif, &ip_info);
    uint8_t ipv4[4];
    memcpy(ipv4, &ip_info.ip.addr, sizeof(ipv4));
    while (portal_dns_running && socket_fd >= 0) {
        uint8_t request[256];
        struct sockaddr_storage source;
        socklen_t source_length = sizeof(source);
        const int request_length = recvfrom(socket_fd, request, sizeof(request), 0,
                                            (struct sockaddr*)&source, &source_length);
        if (request_length <= 0) continue;
        uint8_t reply[272];
        const size_t reply_length = wifi_portal_build_dns_reply(
            request, (size_t)request_length, ipv4, reply, sizeof(reply));
        if (reply_length) {
            sendto(socket_fd, reply, reply_length, 0,
                   (const struct sockaddr*)&source, source_length);
        }
    }
    if (socket_fd >= 0) close(socket_fd);
    portal_dns_socket = -1;
    xSemaphoreGive(portal_dns_stopped);
    vTaskDelete(NULL);
}

static esp_err_t start_portal_dns(void) {
    if (!portal_dns_stopped) portal_dns_stopped = xSemaphoreCreateBinary();
    if (!portal_dns_stopped) return ESP_ERR_NO_MEM;
    xSemaphoreTake(portal_dns_stopped, 0);
    portal_dns_running = true;
    if (xTaskCreate(portal_dns_task, "wifi_portal_dns", 3072, NULL, 4,
                    &portal_dns_task_handle) != pdPASS) {
        portal_dns_running = false;
        portal_dns_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void stop_portal_services(void) {
    if (portal_http_server) {
        httpd_stop(portal_http_server);
        portal_http_server = NULL;
    }
    if (portal_dns_task_handle) {
        portal_dns_running = false;
        if (xSemaphoreTake(portal_dns_stopped, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(tag, "portal DNS task did not stop in time");
            if (portal_dns_socket >= 0) {
                close(portal_dns_socket);
                portal_dns_socket = -1;
            }
            vTaskDelete(portal_dns_task_handle);
        }
        portal_dns_task_handle = NULL;
    }
}

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

static wifi_config_t station_config(const wifi_provision_credentials_t* credentials) {
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credentials->ssid, credentials->ssid_length);
    memcpy(config.sta.password, credentials->password, credentials->password_length);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = auth_mode(credentials->security);
    config.sta.pmf_cfg.capable = true;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config.sta.owe_enabled = credentials->security == WIFI_PROVISION_OWE;
    return config;
}

static void apply_credentials(const wifi_provision_credentials_t* credentials) {
    ESP_LOGI(tag, "applying Wi-Fi credentials: ssid_bytes=%u security=%s password_bytes=%u",
             credentials->ssid_length, security_name(credentials->security),
             credentials->password_length);
    wifi_config_t config = station_config(credentials);

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

static void stop_timer(esp_timer_handle_t timer) {
    if (timer && esp_timer_is_active(timer)) esp_timer_stop(timer);
}

static void publish_portal_state(wifi_portal_state_t state, int32_t error,
                                 int seconds) {
    portENTER_CRITICAL(&status_lock);
    status.portal_state = state;
    status.portal_last_error = error;
    portal_deadline_us = seconds > 0
                             ? esp_timer_get_time() + (int64_t)seconds * 1000000LL
                             : 0;
    portEXIT_CRITICAL(&status_lock);
}

static void generate_portal_identity(void) {
    uint8_t mac[6];
    uint8_t random_bytes[16];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    esp_fill_random(random_bytes, sizeof(random_bytes));

    char ssid[33];
    char token[33];
    snprintf(ssid, sizeof(ssid), "Bajji-%02X%02X%02X", mac[3], mac[4], mac[5]);
    for (size_t index = 0; index < 16; ++index) {
        snprintf(token + index * 2, 3, "%02x", random_bytes[index]);
    }

    portENTER_CRITICAL(&status_lock);
    memcpy(status.portal_ssid, ssid, sizeof(status.portal_ssid));
    status.portal_selected_ssid[0] = '\0';
    memcpy(portal_token, token, sizeof(portal_token));
    portEXIT_CRITICAL(&status_lock);
    memset(random_bytes, 0, sizeof(random_bytes));
    memset(token, 0, sizeof(token));
}

static esp_err_t configure_portal_dhcp(void) {
    static char captive_uri[] = "http://bajji.setup/";
    const esp_err_t stop_result = esp_netif_dhcps_stop(access_point_netif);
    if (stop_result != ESP_OK &&
        stop_result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return stop_result;
    esp_err_t result = esp_netif_dhcps_option(
        access_point_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI,
        captive_uri, strlen(captive_uri));
    if (result == ESP_OK) result = esp_netif_dhcps_start(access_point_netif);
    return result == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED ? ESP_OK : result;
}

static void scan_portal_networks(void) {
    portal_network_count = 0;
    const wifi_scan_config_t scan = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t result = esp_wifi_scan_start(&scan, true);
    if (result != ESP_OK) {
        ESP_LOGW(tag, "portal Wi-Fi scan failed: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return;
    }
    uint16_t count = kPortalNetworkLimit;
    result = esp_wifi_scan_get_ap_records(&count, portal_networks);
    if (result != ESP_OK) {
        ESP_LOGW(tag, "could not read portal Wi-Fi scan: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return;
    }
    for (uint16_t input = 0; input < count; ++input) {
        const wifi_ap_record_t* candidate = &portal_networks[input];
        if (!candidate->ssid[0] || (int)provision_security(candidate->authmode) < 0) continue;
        bool duplicate = false;
        for (uint16_t output = 0; output < portal_network_count; ++output) {
            if (strncmp((const char*)portal_networks[output].ssid,
                        (const char*)candidate->ssid,
                        sizeof(candidate->ssid)) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) portal_networks[portal_network_count++] = *candidate;
    }
    ESP_LOGI(tag, "portal Wi-Fi scan ready: networks=%u", portal_network_count);
}

static void portal_timer_callback(void* argument) {
    (void)argument;
    const wifi_command_t command = {
        .type = WIFI_COMMAND_PORTAL_STOP,
    };
    if (enqueue_command(&command) != pdTRUE) {
        ESP_LOGW(tag, "could not queue portal timer command: type=%d", command.type);
    }
}

static void clear_portal_secrets(void) {
    portENTER_CRITICAL(&status_lock);
    memset(portal_token, 0, sizeof(portal_token));
    portEXIT_CRITICAL(&status_lock);
    memset(&portal_trial_config, 0, sizeof(portal_trial_config));
}

static void stop_portal(void) {
    wifi_portal_state_t state;
    portENTER_CRITICAL(&status_lock);
    state = status.portal_state;
    portal_trial_active = false;
    portEXIT_CRITICAL(&status_lock);
    if (!portal_services_active && state == WIFI_PORTAL_OFF) return;

    stop_timer(portal_timeout_timer);
    stop_timer(portal_close_timer);
    stop_portal_services();
    if (state != WIFI_PORTAL_SUCCESS) {
        esp_wifi_disconnect();
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_config(WIFI_IF_STA, &portal_previous_config);
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    }
    const esp_err_t mode_result = esp_wifi_set_mode(WIFI_MODE_STA);
    esp_err_t start_result = ESP_OK;
    if (!wifi_driver_started) {
        start_result = esp_wifi_start();
        wifi_driver_started = start_result == ESP_OK;
    }
    portENTER_CRITICAL(&status_lock);
    portal_services_active = false;
    status.portal_state = WIFI_PORTAL_OFF;
    portal_deadline_us = 0;
    portEXIT_CRITICAL(&status_lock);
    clear_portal_secrets();
    portENTER_CRITICAL(&status_lock);
    portal_selected_network = UINT16_MAX;
    portEXIT_CRITICAL(&status_lock);
    if (mode_result != ESP_OK || start_result != ESP_OK) {
        portENTER_CRITICAL(&status_lock);
        status.portal_last_error = mode_result != ESP_OK ? mode_result : start_result;
        portEXIT_CRITICAL(&status_lock);
    }
    request_connect("portal_stop");
    ESP_LOGI(tag, "Wi-Fi portal stopped: result=%s", esp_err_to_name(mode_result));
}

static esp_err_t start_portal(void) {
    if (portal_is_active()) return ESP_OK;
    publish_portal_state(WIFI_PORTAL_STARTING, ESP_OK, 0);
    generate_portal_identity();
    portENTER_CRITICAL(&status_lock);
    portal_selected_network = UINT16_MAX;
    portEXIT_CRITICAL(&status_lock);
    stop_timer(reconnect_timer);

    esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (result == ESP_OK) result = esp_wifi_get_config(WIFI_IF_STA, &portal_previous_config);
    portENTER_CRITICAL(&status_lock);
    portal_services_active = true;
    portEXIT_CRITICAL(&status_lock);
    esp_wifi_disconnect();
    const esp_err_t stop_result = esp_wifi_stop();
    if (stop_result == ESP_OK) wifi_driver_started = false;
    if (result == ESP_OK) result = stop_result;

    char ssid[sizeof(status.portal_ssid)];
    portENTER_CRITICAL(&status_lock);
    memcpy(ssid, status.portal_ssid, sizeof(ssid));
    portEXIT_CRITICAL(&status_lock);
    wifi_config_t access_point = {0};
    access_point.ap.ssid_len = (uint8_t)strlen(ssid);
    memcpy(access_point.ap.ssid, ssid, access_point.ap.ssid_len);
    access_point.ap.channel = 1;
    access_point.ap.max_connection = 1;
    access_point.ap.authmode = WIFI_AUTH_OPEN;
    access_point.ap.csa_count = 3;

    if (result == ESP_OK) result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result == ESP_OK) result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &access_point);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &portal_previous_config);
    if (result == ESP_OK) result = esp_wifi_start();
    if (result == ESP_OK) wifi_driver_started = true;
    if (result == ESP_OK) result = configure_portal_dhcp();
    if (result == ESP_OK) scan_portal_networks();
    if (result == ESP_OK) result = start_portal_http();
    if (result == ESP_OK) result = start_portal_dns();
    if (result == ESP_OK) result = esp_timer_start_once(
        portal_timeout_timer, (uint64_t)kPortalDurationSeconds * 1000000ULL);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "could not start Wi-Fi portal: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        portENTER_CRITICAL(&status_lock);
        status.portal_last_error = result;
        portEXIT_CRITICAL(&status_lock);
        stop_portal();
        return result;
    }
    publish_portal_state(WIFI_PORTAL_READY, ESP_OK, kPortalDurationSeconds);
    ESP_LOGI(tag, "Wi-Fi portal ready: ssid=%s networks=%u timeout_seconds=%d",
             ssid, portal_network_count, kPortalDurationSeconds);
    return ESP_OK;
}

static void fail_portal_connection(int32_t error) {
    wifi_portal_state_t state;
    portENTER_CRITICAL(&status_lock);
    state = status.portal_state;
    portal_trial_active = false;
    portEXIT_CRITICAL(&status_lock);
    if (state != WIFI_PORTAL_CONNECTING) return;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_config(WIFI_IF_STA, &portal_previous_config);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    memset(portal_trial_config.sta.password, 0,
           sizeof(portal_trial_config.sta.password));
    portENTER_CRITICAL(&status_lock);
    status.portal_state = WIFI_PORTAL_FAILED;
    status.portal_last_error = error;
    portEXIT_CRITICAL(&status_lock);
    ESP_LOGW(tag, "portal Wi-Fi connection failed: error=%" PRId32, error);
}

static void connect_portal(const wifi_command_t* command) {
    wifi_portal_state_t state;
    portENTER_CRITICAL(&status_lock);
    state = status.portal_state;
    portEXIT_CRITICAL(&status_lock);
    if ((state != WIFI_PORTAL_READY && state != WIFI_PORTAL_FAILED) ||
        command->error < 0 || command->error >= portal_network_count) return;

    portal_trial_config = station_config(&command->credentials);
    portENTER_CRITICAL(&status_lock);
    portal_selected_network = (uint16_t)command->error;
    memcpy(status.portal_selected_ssid, command->credentials.ssid,
           command->credentials.ssid_length);
    status.portal_selected_ssid[command->credentials.ssid_length] = '\0';
    status.portal_state = WIFI_PORTAL_CONNECTING;
    status.portal_last_error = ESP_OK;
    portal_trial_active = true;
    portEXIT_CRITICAL(&status_lock);

    esp_wifi_disconnect();
    esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &portal_trial_config);
    if (result == ESP_OK) result = esp_wifi_connect();
    if (result != ESP_OK) fail_portal_connection(result);
    else ESP_LOGI(tag, "portal Wi-Fi trial started: ssid_bytes=%u security=%s",
                  command->credentials.ssid_length,
                  security_name(command->credentials.security));
}

static void complete_portal_connection(void) {
    wifi_portal_state_t state;
    portENTER_CRITICAL(&status_lock);
    state = status.portal_state;
    portal_trial_active = false;
    portEXIT_CRITICAL(&status_lock);
    if (state != WIFI_PORTAL_CONNECTING) return;

    esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &portal_trial_config);
    if (result != ESP_OK) {
        fail_portal_connection(result);
        return;
    }
    portENTER_CRITICAL(&status_lock);
    status.configured = true;
    portEXIT_CRITICAL(&status_lock);
    stop_timer(portal_timeout_timer);
    result = esp_timer_start_once(portal_close_timer,
                                  (uint64_t)kPortalSuccessSeconds * 1000000ULL);
    if (result != ESP_OK) {
        stop_portal();
        return;
    }
    publish_portal_state(WIFI_PORTAL_SUCCESS, ESP_OK, kPortalSuccessSeconds);
    ESP_LOGI(tag, "portal Wi-Fi configuration persisted; closing in %d seconds",
             kPortalSuccessSeconds);
}

static void provision_worker(void* argument) {
    (void)argument;
    wifi_command_t command;
    while (true) {
        if (xQueueReceive(provision_queue, &command, portMAX_DELAY) != pdTRUE) continue;
        switch (command.type) {
            case WIFI_COMMAND_PROVISION:
                stop_portal();
                apply_credentials(&command.credentials);
                break;
            case WIFI_COMMAND_PORTAL_START: start_portal(); break;
            case WIFI_COMMAND_PORTAL_STOP: stop_portal(); break;
            case WIFI_COMMAND_PORTAL_CONNECT: connect_portal(&command); break;
            case WIFI_COMMAND_PORTAL_SUCCESS: complete_portal_connection(); break;
            case WIFI_COMMAND_PORTAL_FAILED: fail_portal_connection(command.error); break;
        }
        memset(&command, 0, sizeof(command));
    }
}

static void wifi_event(void* argument, esp_event_base_t base, int32_t id, void* data) {
    (void)argument;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(tag, "Wi-Fi station started: configured=%d", configured());
        if (!portal_is_active()) request_connect("station_start");
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
        const uint8_t reason = event ? event->reason : WIFI_REASON_UNSPECIFIED;
        ip_bridge_set_wifi_netif(station_netif, false);
        portENTER_CRITICAL(&status_lock);
        status.connected = false;
        status.rssi = 0;
        status.reconnects++;
        status.last_error = event ? event->reason : WIFI_REASON_UNSPECIFIED;
        const uint32_t reconnects = status.reconnects;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGW(tag, "Wi-Fi disconnected: reason=%u (%s) rssi=%d reconnects=%" PRIu32,
                 reason, disconnect_reason_name(reason), event ? event->rssi : 0, reconnects);
        if (portal_trial_is_active()) {
            if (reason == WIFI_REASON_STA_LEAVING || reason == WIFI_REASON_ASSOC_LEAVE ||
                reason == WIFI_REASON_AUTH_LEAVE) {
                ESP_LOGI(tag, "ignored portal trial disconnect requested by Bajji");
                return;
            }
            portENTER_CRITICAL(&status_lock);
            portal_trial_active = false;
            portEXIT_CRITICAL(&status_lock);
            const wifi_command_t command = {
                .type = WIFI_COMMAND_PORTAL_FAILED,
                .error = reason,
            };
            if (enqueue_command(&command) != pdTRUE) {
                ESP_LOGE(tag, "could not queue portal failure: reason=%u", reason);
            }
            return;
        }
        if (portal_is_active()) return;
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
    if (portal_trial_is_active()) {
        portENTER_CRITICAL(&status_lock);
        portal_trial_active = false;
        portEXIT_CRITICAL(&status_lock);
        const wifi_command_t command = {.type = WIFI_COMMAND_PORTAL_SUCCESS};
        if (enqueue_command(&command) != pdTRUE) {
            ESP_LOGE(tag, "could not queue portal success");
        }
    }
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
    access_point_netif = esp_netif_create_default_wifi_ap();
    provision_queue = xQueueCreate(6, sizeof(wifi_command_t));
    if (!station_netif || !access_point_netif || !provision_queue) {
        ESP_LOGE(tag, "could not allocate Wi-Fi resources: sta=%d ap=%d queue=%d",
                 station_netif != NULL, access_point_netif != NULL,
                 provision_queue != NULL);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(tag, "Wi-Fi station/AP netifs and provisioning queue ready");

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

    const esp_timer_create_args_t portal_timeout_args = {
        .callback = portal_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_portal_timeout",
        .skip_unhandled_events = true,
    };
    const esp_timer_create_args_t portal_close_args = {
        .callback = portal_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_portal_close",
        .skip_unhandled_events = true,
    };
    if ((result = esp_timer_create(&portal_timeout_args, &portal_timeout_timer)) != ESP_OK ||
        (result = esp_timer_create(&portal_close_args, &portal_close_timer)) != ESP_OK) {
        ESP_LOGE(tag, "could not create Wi-Fi portal timers: %s (0x%x)",
                 esp_err_to_name(result), (unsigned)result);
        return result;
    }

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
        wifi_driver_started = true;
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
    wifi_command_t command = {
        .type = WIFI_COMMAND_PROVISION,
        .credentials = credentials,
    };
    const BaseType_t queued = enqueue_command(&command);
    memset(&credentials, 0, sizeof(credentials));
    memset(&command.credentials, 0, sizeof(command.credentials));
    ESP_LOGI(tag, "Wi-Fi provisioning queue result: queued=%d", queued == pdTRUE);
    return queued == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_link_start_portal(void) {
    const wifi_command_t command = {.type = WIFI_COMMAND_PORTAL_START};
    if (!provision_queue) return ESP_ERR_INVALID_STATE;
    return enqueue_command(&command) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t wifi_link_stop_portal(void) {
    const wifi_command_t command = {.type = WIFI_COMMAND_PORTAL_STOP};
    if (!provision_queue) return ESP_ERR_INVALID_STATE;
    return enqueue_command(&command) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

wifi_link_status_t wifi_link_snapshot(void) {
    portENTER_CRITICAL(&status_lock);
    wifi_link_status_t snapshot = status;
    const int64_t deadline = portal_deadline_us;
    portEXIT_CRITICAL(&status_lock);
    if (deadline > 0) {
        const int64_t remaining = deadline - esp_timer_get_time();
        snapshot.portal_seconds_remaining = remaining > 0
            ? (uint16_t)((remaining + 999999LL) / 1000000LL) : 0;
    }
    return snapshot;
}
