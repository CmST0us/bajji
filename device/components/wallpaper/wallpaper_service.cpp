// SPDX-License-Identifier: MIT
#include "wallpaper_service.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <strings.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "wallpaper_format.h"

namespace bajji {
namespace {

constexpr char kMetadataUrl[] =
    "https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=zh-CN";
constexpr char kPartition[] = "wallpaper";
constexpr char kImage[] = "/spiffs/wallpaper.jpg";
constexpr char kImageTemp[] = "/spiffs/wallpaper.jpg.tmp";
constexpr char kImageBackup[] = "/spiffs/wallpaper.jpg.bak";
constexpr char kMetadata[] = "/spiffs/wallpaper.meta";
constexpr char kMetadataTemp[] = "/spiffs/wallpaper.meta.tmp";
constexpr char kMetadataBackup[] = "/spiffs/wallpaper.meta.bak";
constexpr size_t kMetadataLimit = 8192;
constexpr size_t kImageLimit = 1024 * 1024;
constexpr std::uint32_t kRetrySeconds[] = {60, 300, 900, 3600};
constexpr std::uint32_t kSuccessCheckSeconds = 6 * 60 * 60;

const char* tag = "wallpaper";

enum class Command : std::uint8_t { wake, refresh, dns_test, https_test };

struct BingMetadata {
    char url[384]{};
    char start_date[9]{};
    char copyright[192]{};
};

struct HeaderState {
    bool jpeg{};
};

WallpaperStatus status;
SemaphoreHandle_t status_mutex;
QueueHandle_t command_queue;
bool started;

void copy_text(char* destination, size_t capacity, const char* source) {
    if (!capacity) return;
    std::snprintf(destination, capacity, "%s", source ? source : "");
}

template <typename Callback>
void update_status(Callback callback) {
    if (!status_mutex || xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    callback(status);
    xSemaphoreGive(status_mutex);
}

void set_result(esp_err_t error, const char* message) {
    update_status([&](WallpaperStatus& value) {
        value.last_error = error;
        copy_text(value.state, sizeof(value.state), message);
    });
}

bool valid_date(const char* value) {
    if (!value || std::strlen(value) != 8) return false;
    for (size_t index = 0; index < 8; ++index) {
        if (value[index] < '0' || value[index] > '9') return false;
    }
    return true;
}

esp_err_t header_event(esp_http_client_event_t* event) {
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        strcasecmp(event->header_key, "Content-Type") == 0) {
        auto* header = static_cast<HeaderState*>(event->user_data);
        header->jpeg = strncasecmp(event->header_value, "image/jpeg", 10) == 0;
    }
    return ESP_OK;
}

esp_http_client_handle_t open_https(const char* url, HeaderState* header, int64_t* length) {
    esp_http_client_config_t config{};
    config.url = url;
    config.user_agent = "Bajji-StopWatch/1";
    config.timeout_ms = 10000;
    config.max_redirection_count = 3;
    config.event_handler = header_event;
    config.buffer_size = 4096;
    config.user_data = header;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return nullptr;
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t error = esp_http_client_open(client, 0);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "HTTPS open failed for %s: %s", url, esp_err_to_name(error));
        esp_http_client_cleanup(client);
        return nullptr;
    }
    *length = esp_http_client_fetch_headers(client);
    const int code = esp_http_client_get_status_code(client);
    if (*length < 0 || code < 200 || code >= 300) {
        ESP_LOGE(tag, "HTTPS rejected for %s: status=%d length=%lld", url, code,
                 static_cast<long long>(*length));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return nullptr;
    }
    return client;
}

void close_https(esp_http_client_handle_t client) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

esp_err_t fetch_metadata(BingMetadata* metadata) {
    HeaderState header;
    int64_t content_length = 0;
    esp_http_client_handle_t client = open_https(kMetadataUrl, &header, &content_length);
    if (!client) return ESP_ERR_HTTP_CONNECT;
    if (content_length > static_cast<int64_t>(kMetadataLimit)) {
        close_https(client);
        return ESP_ERR_INVALID_SIZE;
    }

    auto* body = static_cast<char*>(std::malloc(kMetadataLimit + 1));
    if (!body) {
        close_https(client);
        return ESP_ERR_NO_MEM;
    }
    size_t used = 0;
    esp_err_t result = ESP_OK;
    while (used < kMetadataLimit) {
        const int count = esp_http_client_read(client, body + used, kMetadataLimit - used);
        if (count < 0) {
            result = ESP_ERR_HTTP_READ_TIMEOUT;
            break;
        }
        if (count == 0) break;
        used += static_cast<size_t>(count);
    }
    if (result == ESP_OK && used == kMetadataLimit) {
        char extra;
        if (esp_http_client_read(client, &extra, 1) != 0) result = ESP_ERR_INVALID_SIZE;
    }
    close_https(client);
    if (result != ESP_OK) {
        std::free(body);
        return result;
    }
    body[used] = '\0';

    cJSON* root = cJSON_ParseWithLength(body, used);
    std::free(body);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    cJSON* images = cJSON_GetObjectItemCaseSensitive(root, "images");
    cJSON* image = cJSON_IsArray(images) && cJSON_GetArraySize(images) == 1
                       ? cJSON_GetArrayItem(images, 0)
                       : nullptr;
    cJSON* urlbase = image ? cJSON_GetObjectItemCaseSensitive(image, "urlbase") : nullptr;
    cJSON* start_date = image ? cJSON_GetObjectItemCaseSensitive(image, "startdate") : nullptr;
    cJSON* copyright = image ? cJSON_GetObjectItemCaseSensitive(image, "copyright") : nullptr;
    if (!cJSON_IsString(urlbase) || !cJSON_IsString(start_date) || !cJSON_IsString(copyright) ||
        !valid_date(start_date->valuestring) ||
        wallpaper_build_bing_url(urlbase->valuestring, metadata->url, sizeof(metadata->url)) != 0) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    copy_text(metadata->start_date, sizeof(metadata->start_date), start_date->valuestring);
    copy_text(metadata->copyright, sizeof(metadata->copyright), copyright->valuestring);
    for (char* cursor = metadata->copyright; *cursor; ++cursor) {
        if (*cursor == '\r' || *cursor == '\n') *cursor = ' ';
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t validate_image_file(const char* path, wallpaper_jpeg_info_t* info) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return ESP_FAIL;
    }
    const long length = std::ftell(file);
    if (length < 0 || static_cast<size_t>(length) > kImageLimit ||
        std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    auto* data = static_cast<std::uint8_t*>(std::malloc(static_cast<size_t>(length)));
    if (!data) {
        std::fclose(file);
        return ESP_ERR_NO_MEM;
    }
    const bool read = std::fread(data, 1, static_cast<size_t>(length), file) ==
                      static_cast<size_t>(length);
    std::fclose(file);
    const wallpaper_format_result_t format =
        read ? wallpaper_jpeg_validate(data, static_cast<size_t>(length), kImageLimit, info)
             : WALLPAPER_FORMAT_TRUNCATED;
    std::free(data);
    return format == WALLPAPER_FORMAT_OK ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t download_image(const char* url) {
    HeaderState header;
    int64_t content_length = 0;
    esp_http_client_handle_t client = open_https(url, &header, &content_length);
    if (!client) return ESP_ERR_HTTP_CONNECT;
    if (content_length > static_cast<int64_t>(kImageLimit) || !header.jpeg) {
        close_https(client);
        return content_length > static_cast<int64_t>(kImageLimit) ? ESP_ERR_INVALID_SIZE
                                                                  : ESP_ERR_INVALID_RESPONSE;
    }

    std::remove(kImageTemp);
    FILE* file = std::fopen(kImageTemp, "wb");
    if (!file) {
        close_https(client);
        return ESP_FAIL;
    }
    char buffer[4096];
    size_t total = 0;
    esp_err_t result = ESP_OK;
    while (true) {
        const int count = esp_http_client_read(client, buffer, sizeof(buffer));
        if (count < 0) {
            result = ESP_ERR_HTTP_READ_TIMEOUT;
            break;
        }
        if (count == 0) break;
        total += static_cast<size_t>(count);
        if (total > kImageLimit || std::fwrite(buffer, 1, count, file) != static_cast<size_t>(count)) {
            result = total > kImageLimit ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
            break;
        }
    }
    if (std::fclose(file) != 0 && result == ESP_OK) result = ESP_FAIL;
    close_https(client);
    wallpaper_jpeg_info_t info{};
    if (result == ESP_OK) result = validate_image_file(kImageTemp, &info);
    if (result != ESP_OK) {
        std::remove(kImageTemp);
        return result;
    }
    ESP_LOGI(tag, "downloaded %u bytes, JPEG %ux%u", static_cast<unsigned>(total), info.width,
             info.height);
    return ESP_OK;
}

esp_err_t replace_file(const char* temporary, const char* final, const char* backup) {
    std::remove(backup);
    const bool had_final = std::rename(final, backup) == 0;
    if (std::rename(temporary, final) == 0) {
        if (had_final) std::remove(backup);
        return ESP_OK;
    }
    const int saved_errno = errno;
    if (had_final && std::rename(backup, final) != 0) {
        ESP_LOGE(tag, "could not restore %s after replacement failure", final);
    }
    errno = saved_errno;
    return ESP_FAIL;
}

esp_err_t write_metadata(const BingMetadata& metadata) {
    std::remove(kMetadataTemp);
    FILE* file = std::fopen(kMetadataTemp, "wb");
    if (!file) return ESP_FAIL;
    const int written = std::fprintf(file, "%s\n%s\n", metadata.start_date, metadata.copyright);
    const bool okay = written > 0 && std::fclose(file) == 0;
    if (!okay) {
        if (written <= 0) std::fclose(file);
        std::remove(kMetadataTemp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void load_metadata() {
    FILE* file = std::fopen(kMetadata, "rb");
    if (!file) return;
    char date[9]{};
    char copyright[192]{};
    const bool okay = std::fgets(date, sizeof(date), file) && std::fgets(copyright, sizeof(copyright), file);
    std::fclose(file);
    if (!okay) return;
    date[strcspn(date, "\r\n")] = '\0';
    copyright[strcspn(copyright, "\r\n")] = '\0';
    if (!valid_date(date)) return;
    update_status([&](WallpaperStatus& value) {
        copy_text(value.start_date, sizeof(value.start_date), date);
        copy_text(value.copyright, sizeof(value.copyright), copyright);
    });
}

void recover_cache() {
    std::remove(kImageTemp);
    wallpaper_jpeg_info_t info{};
    if (validate_image_file(kImage, &info) == ESP_OK) {
        std::remove(kImageBackup);
        update_status([](WallpaperStatus& value) {
            value.has_cache = true;
            value.revision++;
        });
        load_metadata();
        return;
    }
    if (validate_image_file(kImageBackup, &info) == ESP_OK) {
        std::remove(kImage);
        if (std::rename(kImageBackup, kImage) == 0) {
            update_status([](WallpaperStatus& value) {
                value.has_cache = true;
                value.revision++;
            });
            load_metadata();
        }
    }
}

bool partition_is_blank() {
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                 kPartition);
    if (!partition) return false;
    std::uint8_t buffer[256];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
        if (esp_partition_read(partition, offset, buffer, sizeof(buffer)) != ESP_OK) return false;
        for (std::uint8_t byte : buffer) {
            if (byte != 0xff) return false;
        }
    }
    return true;
}

esp_err_t mount_cache() {
    const esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = kPartition,
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    esp_err_t result = esp_vfs_spiffs_register(&config);
    if (result == ESP_FAIL && partition_is_blank()) {
        ESP_LOGW(tag, "blank wallpaper partition; formatting once");
        result = esp_spiffs_format(kPartition);
        if (result == ESP_OK) result = esp_vfs_spiffs_register(&config);
    }
    if (result == ESP_OK) recover_cache();
    update_status([&](WallpaperStatus& value) {
        value.mounted = result == ESP_OK;
        copy_text(value.state, sizeof(value.state), result == ESP_OK ? "Waiting for phone bridge"
                                                                    : "Cache unavailable");
        value.last_error = result;
    });
    return result;
}

esp_err_t perform_update() {
    set_result(ESP_OK, "Checking Bing wallpaper...");
    BingMetadata metadata;
    esp_err_t result = fetch_metadata(&metadata);
    if (result != ESP_OK) return result;
    const WallpaperStatus before = wallpaper_snapshot();
    if (before.has_cache && std::strcmp(before.start_date, metadata.start_date) == 0) {
        set_result(ESP_OK, "Wallpaper is up to date");
        return ESP_OK;
    }
    set_result(ESP_OK, "Downloading wallpaper...");
    result = download_image(metadata.url);
    if (result != ESP_OK) return result;
    result = write_metadata(metadata);
    if (result != ESP_OK) {
        std::remove(kImageTemp);
        return result;
    }
    result = replace_file(kImageTemp, kImage, kImageBackup);
    if (result != ESP_OK) {
        std::remove(kMetadataTemp);
        return result;
    }
    result = replace_file(kMetadataTemp, kMetadata, kMetadataBackup);
    update_status([&](WallpaperStatus& value) {
        value.has_cache = true;
        value.revision++;
        copy_text(value.start_date, sizeof(value.start_date), metadata.start_date);
        copy_text(value.copyright, sizeof(value.copyright), metadata.copyright);
    });
    return result;
}

void run_dns_test() {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* addresses = nullptr;
    const int result = getaddrinfo("www.bing.com", nullptr, &hints, &addresses);
    if (addresses) freeaddrinfo(addresses);
    update_status([&](WallpaperStatus& value) {
        if (result == 0) {
            copy_text(value.test_result, sizeof(value.test_result), "DNS: www.bing.com resolved");
        } else {
            std::snprintf(value.test_result, sizeof(value.test_result), "DNS: failed (%d)", result);
        }
        value.last_error = result;
        value.internet_verified = result == 0;
    });
    ESP_LOGI(tag, "DNS test: %s (%d)", result == 0 ? "passed" : "failed", result);
}

void run_https_test() {
    BingMetadata metadata;
    const esp_err_t result = fetch_metadata(&metadata);
    update_status([&](WallpaperStatus& value) {
        std::snprintf(value.test_result, sizeof(value.test_result), "HTTPS: %s",
                      result == ESP_OK ? "Bing metadata verified" : esp_err_to_name(result));
        value.last_error = result;
        value.internet_verified = result == ESP_OK;
    });
    ESP_LOGI(tag, "HTTPS test: %s", esp_err_to_name(result));
}

void worker(void*) {
    TickType_t next_attempt = 0;
    size_t failures = 0;
    while (true) {
        Command command = Command::wake;
        const bool received = xQueueReceive(command_queue, &command, pdMS_TO_TICKS(1000)) == pdTRUE;
        const WallpaperStatus current = wallpaper_snapshot();
        if (!current.online || !current.mounted) continue;

        if (received && command == Command::dns_test) {
            update_status([](WallpaperStatus& value) { value.busy = true; });
            run_dns_test();
            update_status([](WallpaperStatus& value) { value.busy = false; });
            continue;
        }
        if (received && command == Command::https_test) {
            update_status([](WallpaperStatus& value) { value.busy = true; });
            run_https_test();
            update_status([](WallpaperStatus& value) { value.busy = false; });
            continue;
        }

        const TickType_t now = xTaskGetTickCount();
        const bool due = static_cast<std::int32_t>(now - next_attempt) >= 0;
        if ((!received || command == Command::wake) && !due) continue;
        update_status([](WallpaperStatus& value) { value.busy = true; });
        const esp_err_t result = perform_update();
        update_status([](WallpaperStatus& value) { value.busy = false; });
        if (result == ESP_OK) {
            failures = 0;
            next_attempt = now + pdMS_TO_TICKS(kSuccessCheckSeconds * 1000U);
            update_status([](WallpaperStatus& value) { value.internet_verified = true; });
            ESP_LOGI(tag, "wallpaper check complete");
        } else {
            const size_t retry_index = failures < std::size(kRetrySeconds) ? failures
                                                                           : std::size(kRetrySeconds) - 1;
            const std::uint32_t retry = kRetrySeconds[retry_index];
            if (failures < std::size(kRetrySeconds)) ++failures;
            next_attempt = now + pdMS_TO_TICKS(retry * 1000U);
            char message[48];
            std::snprintf(message, sizeof(message), "Retry in %lu min: %s",
                          static_cast<unsigned long>((retry + 59) / 60), esp_err_to_name(result));
            set_result(result, message);
            update_status([](WallpaperStatus& value) { value.internet_verified = false; });
            ESP_LOGE(tag, "wallpaper update failed: %s; retry in %lu s", esp_err_to_name(result),
                     static_cast<unsigned long>(retry));
        }
    }
}

void enqueue(Command command) {
    if (!command_queue) return;
    xQueueSend(command_queue, &command, 0);
}

}  // namespace

esp_err_t wallpaper_start() {
    if (started) return ESP_OK;
    status_mutex = xSemaphoreCreateMutex();
    command_queue = xQueueCreate(4, sizeof(Command));
    if (!status_mutex || !command_queue) return ESP_ERR_NO_MEM;
    copy_text(status.state, sizeof(status.state), "Starting...");
    const esp_err_t mount_result = mount_cache();
    if (xTaskCreate(worker, "wallpaper", 16384, nullptr, 4, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    started = true;
    return mount_result;
}

void wallpaper_set_online(bool online_and_time_valid) {
    bool changed = false;
    update_status([&](WallpaperStatus& value) {
        changed = value.online != online_and_time_valid;
        value.online = online_and_time_valid;
        if (!online_and_time_valid) value.internet_verified = false;
        if (!online_and_time_valid && !value.busy) {
            copy_text(value.state, sizeof(value.state), value.has_cache ? "Offline - cached wallpaper"
                                                                       : "Waiting for phone bridge");
        }
    });
    if (changed && online_and_time_valid) enqueue(Command::wake);
}

void wallpaper_request_refresh() { enqueue(Command::refresh); }

void wallpaper_request_dns_test() { enqueue(Command::dns_test); }

void wallpaper_request_https_test() { enqueue(Command::https_test); }

WallpaperStatus wallpaper_snapshot() {
    WallpaperStatus snapshot;
    if (!status_mutex || xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return snapshot;
    snapshot = status;
    xSemaphoreGive(status_mutex);
    return snapshot;
}

}  // namespace bajji
