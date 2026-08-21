// SPDX-License-Identifier: MIT
#include "wallpaper_service.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

namespace bajji {
namespace {

constexpr char kPartition[] = "wallpaper";
constexpr char kImageTemp[] = "/spiffs/wallpaper.tmp";
constexpr char kImageBackup[] = "/spiffs/wallpaper.bak";
constexpr char kNvsNamespace[] = "bajji_ui";
constexpr size_t kImageLimit = 3U * 1024U * 1024U;
constexpr size_t kDecodedLimit = 5U * 1024U * 1024U;
constexpr unsigned kRedirectLimit = 5;
constexpr wallpaper_media_format_t kFormats[] = {
    WALLPAPER_MEDIA_JPEG, WALLPAPER_MEDIA_PNG, WALLPAPER_MEDIA_GIF, WALLPAPER_MEDIA_WEBP,
};

const char* tag = "wallpaper";

enum class Command : std::uint8_t { wake, refresh, save_settings, save_mode };

struct QueuedCommand {
    Command type{Command::wake};
    WallpaperSettings settings{};
};

struct HeaderState {
    FILE* file{};
    size_t total{};
    unsigned redirects{};
    bool content_type_seen{};
    bool image_content_type{};
    bool redirect_allowed{};
    esp_err_t error{ESP_OK};
    char location[512]{};
};

WallpaperStatus status;
SemaphoreHandle_t status_mutex;
QueueHandle_t command_queue;
bool started;

const char* image_path(wallpaper_media_format_t format) {
    switch (format) {
        case WALLPAPER_MEDIA_JPEG: return "/spiffs/wallpaper.jpg";
        case WALLPAPER_MEDIA_PNG: return "/spiffs/wallpaper.png";
        case WALLPAPER_MEDIA_GIF: return "/spiffs/wallpaper.gif";
        case WALLPAPER_MEDIA_WEBP: return "/spiffs/wallpaper.webp";
        default: return nullptr;
    }
}

const char* lvgl_path(wallpaper_media_format_t format) {
    switch (format) {
        case WALLPAPER_MEDIA_JPEG: return "S:/wallpaper.jpg";
        case WALLPAPER_MEDIA_PNG: return "S:/wallpaper.png";
        case WALLPAPER_MEDIA_GIF: return "S:/wallpaper.gif";
        case WALLPAPER_MEDIA_WEBP: return "S:/wallpaper.webp";
        default: return "";
    }
}

void remove_other_images(const char* keep) {
    for (wallpaper_media_format_t format : kFormats) {
        const char* path = image_path(format);
        if (path && (!keep || std::strcmp(path, keep) != 0)) std::remove(path);
    }
}

void copy_text(char* destination, size_t capacity, const char* source) {
    if (capacity) std::snprintf(destination, capacity, "%s", source ? source : "");
}

template <typename Callback>
void update_status(Callback callback) {
    if (!status_mutex || xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    callback(status);
    xSemaphoreGive(status_mutex);
}

void default_settings(WallpaperSettings* settings) {
    *settings = {};
    copy_text(settings->category, sizeof(settings->category), "bq");
    copy_text(settings->type, sizeof(settings->type), "eciyuan");
    settings->display_mode = DisplayMode::cover;
}

esp_err_t load_settings() {
    WallpaperSettings loaded;
    default_settings(&loaded);
    nvs_handle_t handle;
    esp_err_t result = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        update_status([&](WallpaperStatus& value) { value.settings = loaded; });
        return ESP_OK;
    }
    if (result != ESP_OK) return result;
    std::uint8_t configured = 0;
    std::uint8_t mode = 0;
    size_t category_size = sizeof(loaded.category);
    size_t type_size = sizeof(loaded.type);
    const esp_err_t configured_result = nvs_get_u8(handle, "configured", &configured);
    const esp_err_t category_result = nvs_get_str(handle, "category", loaded.category, &category_size);
    const esp_err_t type_result = nvs_get_str(handle, "type", loaded.type, &type_size);
    const esp_err_t mode_result = nvs_get_u8(handle, "display_mode", &mode);
    nvs_close(handle);
    if ((configured_result != ESP_OK && configured_result != ESP_ERR_NVS_NOT_FOUND) ||
        (category_result != ESP_OK && category_result != ESP_ERR_NVS_NOT_FOUND) ||
        (type_result != ESP_OK && type_result != ESP_ERR_NVS_NOT_FOUND) ||
        (mode_result != ESP_OK && mode_result != ESP_ERR_NVS_NOT_FOUND)) {
        return ESP_FAIL;
    }
    loaded.configured = configured != 0;
    loaded.display_mode = mode == 1 ? DisplayMode::fit_blur : DisplayMode::cover;
    if (!wallpaper_settings_valid(loaded.category, loaded.type)) default_settings(&loaded);
    update_status([&](WallpaperStatus& value) { value.settings = loaded; });
    return ESP_OK;
}

esp_err_t persist_settings(const WallpaperSettings& settings) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    if ((result = nvs_set_u8(handle, "configured", settings.configured ? 1 : 0)) == ESP_OK &&
        (result = nvs_set_str(handle, "category", settings.category)) == ESP_OK &&
        (result = nvs_set_str(handle, "type", settings.type)) == ESP_OK &&
        (result = nvs_set_u8(handle, "display_mode",
                             settings.display_mode == DisplayMode::fit_blur ? 1 : 0)) == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

bool secure_redirect(const char* location) {
    if (!location || !location[0]) return false;
    if (strncasecmp(location, "https://", 8) == 0) return true;
    return location[0] == '/' && location[1] != '/';
}

esp_err_t http_event(esp_http_client_event_t* event) {
    auto* header = static_cast<HeaderState*>(event->user_data);
    switch (event->event_id) {
        case HTTP_EVENT_ON_STATUS_CODE:
            header->content_type_seen = false;
            header->image_content_type = false;
            header->redirect_allowed = false;
            header->location[0] = '\0';
            break;
        case HTTP_EVENT_ON_HEADER:
            if (event->header_key && event->header_value &&
                strcasecmp(event->header_key, "Content-Type") == 0) {
                header->content_type_seen = true;
                header->image_content_type = strncasecmp(event->header_value, "image/", 6) == 0;
            } else if (event->header_key && event->header_value &&
                       strcasecmp(event->header_key, "Location") == 0) {
                copy_text(header->location, sizeof(header->location), event->header_value);
                header->redirect_allowed = secure_redirect(header->location);
            }
            break;
        case HTTP_EVENT_REDIRECT:
            if (!header->redirect_allowed || ++header->redirects > kRedirectLimit) {
                header->error = ESP_ERR_INVALID_RESPONSE;
            } else {
                const esp_err_t result = esp_http_client_set_redirection(event->client);
                if (result != ESP_OK) header->error = result;
            }
            break;
        case HTTP_EVENT_ON_DATA: {
            const int code = esp_http_client_get_status_code(event->client);
            if (code < 200 || code >= 300 || header->error != ESP_OK || event->data_len <= 0) break;
            if (header->total + static_cast<size_t>(event->data_len) > kImageLimit) {
                header->error = ESP_ERR_INVALID_SIZE;
                break;
            }
            if (!header->file ||
                std::fwrite(event->data, 1, static_cast<size_t>(event->data_len), header->file) !=
                    static_cast<size_t>(event->data_len)) {
                header->error = ESP_FAIL;
                break;
            }
            header->total += static_cast<size_t>(event->data_len);
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t validate_image_file(const char* path, wallpaper_media_info_t* info) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return ESP_FAIL;
    }
    const long length = std::ftell(file);
    if (length <= 0 || static_cast<size_t>(length) > kImageLimit ||
        std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    auto* data = static_cast<std::uint8_t*>(
        heap_caps_malloc(static_cast<size_t>(length), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) data = static_cast<std::uint8_t*>(std::malloc(static_cast<size_t>(length)));
    if (!data) {
        std::fclose(file);
        return ESP_ERR_NO_MEM;
    }
    const bool read = std::fread(data, 1, static_cast<size_t>(length), file) ==
                      static_cast<size_t>(length);
    std::fclose(file);
    const wallpaper_format_result_t format =
        read ? wallpaper_media_validate(data, static_cast<size_t>(length), kImageLimit, info)
             : WALLPAPER_FORMAT_TRUNCATED;
    std::free(data);
    if (format != WALLPAPER_FORMAT_OK) return ESP_ERR_INVALID_RESPONSE;
    const std::uint64_t pixels = static_cast<std::uint64_t>(info->width) * info->height;
    const std::uint64_t decoded = info->format == WALLPAPER_MEDIA_GIF
                                      ? pixels * 4U
                                      : info->format == WALLPAPER_MEDIA_WEBP
                                            ? pixels * 8U
                                            : info->format == WALLPAPER_MEDIA_PNG ? pixels * 4U : 0U;
    return decoded <= kDecodedLimit ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t download_image(const char* url, wallpaper_media_info_t* info) {
    ESP_LOGI(tag, "HTTP GET %s", url);
    std::remove(kImageTemp);
    FILE* file = std::fopen(kImageTemp, "wb");
    if (!file) return ESP_FAIL;
    HeaderState header{.file = file};
    esp_http_client_config_t config{};
    config.url = url;
    config.user_agent = "Bajji-StopWatch/2";
    config.timeout_ms = 15000;
    config.max_redirection_count = kRedirectLimit;
    config.disable_auto_redirect = true;
    config.event_handler = http_event;
    config.buffer_size = 4096;
    config.user_data = &header;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        std::fclose(file);
        std::remove(kImageTemp);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "image/webp,image/gif,image/png,image/jpeg;q=0.9");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t result = esp_http_client_perform(client);
    const int code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (std::fclose(file) != 0 && result == ESP_OK) result = ESP_FAIL;
    if (result == ESP_OK) result = header.error;
    if (result == ESP_OK && (code < 200 || code >= 300)) result = ESP_ERR_INVALID_RESPONSE;
    if (result == ESP_OK && header.content_type_seen && !header.image_content_type) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (result == ESP_OK && header.total == 0) result = ESP_ERR_INVALID_SIZE;
    if (result == ESP_OK) result = validate_image_file(kImageTemp, info);
    if (result != ESP_OK) {
        std::remove(kImageTemp);
        ESP_LOGE(tag, "image download failed: %s status=%d bytes=%u", esp_err_to_name(result),
                 code, static_cast<unsigned>(header.total));
        return result;
    }
    ESP_LOGI(tag, "downloaded %u bytes, format=%d %ux%u animated=%d",
             static_cast<unsigned>(header.total), static_cast<int>(info->format), info->width,
             info->height, info->animated);
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

void recover_cache() {
    std::remove(kImageTemp);
    wallpaper_media_info_t info{};
    for (wallpaper_media_format_t format : kFormats) {
        const char* path = image_path(format);
        if (validate_image_file(path, &info) == ESP_OK && info.format == format) {
            std::remove(kImageBackup);
            remove_other_images(path);
            update_status([&](WallpaperStatus& value) {
                value.has_cache = true;
                value.media = info;
                copy_text(value.lvgl_path, sizeof(value.lvgl_path), lvgl_path(info.format));
                value.revision++;
            });
            return;
        }
    }
    if (validate_image_file(kImageBackup, &info) == ESP_OK) {
        const char* restored = image_path(info.format);
        remove_other_images(nullptr);
        if (restored && std::rename(kImageBackup, restored) == 0) {
            update_status([&](WallpaperStatus& value) {
                value.has_cache = true;
                value.media = info;
                copy_text(value.lvgl_path, sizeof(value.lvgl_path), lvgl_path(info.format));
                value.revision++;
            });
        }
    }
}

esp_err_t mount_cache() {
    const esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = kPartition,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t result = esp_vfs_spiffs_register(&config);
    if (result == ESP_OK) recover_cache();
    update_status([&](WallpaperStatus& value) {
        value.mounted = result == ESP_OK;
        value.last_error = result;
        copy_text(value.state, sizeof(value.state), result == ESP_OK ? "等待手机连接" : "缓存不可用");
    });
    return result;
}

esp_err_t perform_update(const WallpaperSettings& settings, wallpaper_media_info_t* info) {
    char url[256];
    if (wallpaper_build_random_url(settings.category, settings.type, url, sizeof(url)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    update_status([](WallpaperStatus& value) {
        copy_text(value.state, sizeof(value.state), value.has_cache ? "正在换一张…" : "正在获取图片");
    });
    esp_err_t result = download_image(url, info);
    const char* target = result == ESP_OK ? image_path(info->format) : nullptr;
    if (result == ESP_OK && !target) result = ESP_ERR_NOT_SUPPORTED;
    if (result == ESP_OK) {
        WallpaperStatus current = wallpaper_snapshot();
        const char* previous = current.has_cache ? image_path(current.media.format) : nullptr;
        if (previous && std::strcmp(previous, target) != 0) {
            std::remove(target);
            result = std::rename(kImageTemp, target) == 0 ? ESP_OK : ESP_FAIL;
        } else {
            result = replace_file(kImageTemp, target, kImageBackup);
        }
        if (result == ESP_OK) remove_other_images(target);
    }
    return result;
}

void finish_request(esp_err_t result, const wallpaper_media_info_t& info) {
    update_status([&](WallpaperStatus& value) {
        value.busy = false;
        value.request_revision++;
        value.last_error = result;
        value.internet_verified = result == ESP_OK;
        if (result == ESP_OK) {
            value.has_cache = true;
            value.media = info;
            copy_text(value.lvgl_path, sizeof(value.lvgl_path), lvgl_path(info.format));
            value.revision++;
            copy_text(value.state, sizeof(value.state), "图片已更新");
        } else {
            copy_text(value.state, sizeof(value.state),
                      value.has_cache ? "网络失败 · 显示缓存" : "无法获取图片");
        }
    });
}

void worker(void*) {
    WallpaperStatus initial = wallpaper_snapshot();
    bool pending_refresh = initial.settings.configured && !initial.has_cache;
    while (true) {
        QueuedCommand command;
        const bool received =
            xQueueReceive(command_queue, &command, pdMS_TO_TICKS(1000)) == pdTRUE;
        if (received && command.type == Command::save_settings) {
            const esp_err_t result = persist_settings(command.settings);
            update_status([&](WallpaperStatus& value) {
                value.last_error = result;
                if (result == ESP_OK) {
                    value.settings = command.settings;
                    copy_text(value.state, sizeof(value.state), "设置已保存");
                } else {
                    copy_text(value.state, sizeof(value.state), "设置保存失败");
                }
            });
            pending_refresh = result == ESP_OK;
        } else if (received && command.type == Command::save_mode) {
            WallpaperStatus current = wallpaper_snapshot();
            current.settings.display_mode = command.settings.display_mode;
            const esp_err_t result = persist_settings(current.settings);
            update_status([&](WallpaperStatus& value) {
                value.last_error = result;
                if (result == ESP_OK) value.settings.display_mode = command.settings.display_mode;
            });
        } else if (received && command.type == Command::refresh) {
            pending_refresh = true;
        }

        if (!pending_refresh) continue;
        const WallpaperStatus current = wallpaper_snapshot();
        if (!current.settings.configured) {
            pending_refresh = false;
            finish_request(ESP_ERR_INVALID_STATE, {});
            continue;
        }
        if (!current.mounted || !current.online) {
            update_status([](WallpaperStatus& value) {
                copy_text(value.state, sizeof(value.state),
                          value.has_cache ? "等待手机连接 · 显示缓存" : "等待手机连接");
            });
            continue;
        }
        update_status([](WallpaperStatus& value) { value.busy = true; });
        wallpaper_media_info_t info{};
        const esp_err_t result = perform_update(current.settings, &info);
        finish_request(result, info);
        pending_refresh = false;
    }
}

esp_err_t enqueue(const QueuedCommand& command) {
    if (!command_queue) return ESP_ERR_INVALID_STATE;
    return xQueueSend(command_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

}  // namespace

esp_err_t wallpaper_start() {
    if (started) return ESP_OK;
    status_mutex = xSemaphoreCreateMutex();
    command_queue = xQueueCreate(6, sizeof(QueuedCommand));
    if (!status_mutex || !command_queue) return ESP_ERR_NO_MEM;
    default_settings(&status.settings);
    copy_text(status.state, sizeof(status.state), "正在启动…");
    const esp_err_t settings_result = load_settings();
    const esp_err_t mount_result = mount_cache();
    if (xTaskCreate(worker, "wallpaper", 16384, nullptr, 4, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    started = true;
    if (settings_result != ESP_OK) return settings_result;
    return mount_result;
}

void wallpaper_set_online(bool online_and_time_valid) {
    bool changed = false;
    update_status([&](WallpaperStatus& value) {
        changed = value.online != online_and_time_valid;
        value.online = online_and_time_valid;
        if (!online_and_time_valid) value.internet_verified = false;
        if (!online_and_time_valid && !value.busy) {
            copy_text(value.state, sizeof(value.state),
                      value.has_cache ? "等待手机连接 · 显示缓存" : "等待手机连接");
        }
    });
    if (changed && online_and_time_valid) enqueue({.type = Command::wake});
}

void wallpaper_request_refresh() { enqueue({.type = Command::refresh}); }

esp_err_t wallpaper_save_settings(const char* category, const char* type) {
    if (!category || !type || !wallpaper_settings_valid(category, type)) return ESP_ERR_INVALID_ARG;
    const WallpaperStatus current = wallpaper_snapshot();
    QueuedCommand command{.type = Command::save_settings, .settings = current.settings};
    command.settings.configured = true;
    copy_text(command.settings.category, sizeof(command.settings.category), category);
    copy_text(command.settings.type, sizeof(command.settings.type), type);
    return enqueue(command);
}

esp_err_t wallpaper_set_display_mode(DisplayMode mode) {
    if (mode != DisplayMode::cover && mode != DisplayMode::fit_blur) return ESP_ERR_INVALID_ARG;
    QueuedCommand command{.type = Command::save_mode};
    command.settings.display_mode = mode;
    return enqueue(command);
}

WallpaperStatus wallpaper_snapshot() {
    WallpaperStatus snapshot;
    if (!status_mutex || xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return snapshot;
    snapshot = status;
    xSemaphoreGive(status_mutex);
    return snapshot;
}

}  // namespace bajji
