/*
 * SPDX-FileCopyrightText: 2026 Embedded Event Black Box Team (UNPAD)
 *
 * SPDX-License-Identifier: MIT
 *
 * Component: blackbox_storage
 * Purpose:   Reusable ESP-IDF component for persisting motion event recordings
 *            to a FAT-formatted microSD card over SPI (SDSPI).
 *
 * Official Espressif References Consulted (ESP-IDF v5.4 for ESP32-S3):
 * - FATFS VFS API: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/storage/fatfs.html
 * - SDSPI Host Driver: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/sdspi_host.html
 * - Shared SPI Bus notes: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/sdspi_share.html
 * - SDSPI Example (v5.4): https://github.com/espressif/esp-idf/tree/v5.4/examples/storage/sd_card/sdspi
 */

#include "blackbox_storage.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BB_STORAGE_HOST_TEST
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bb_storage";
#define BB_LOGI(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define BB_LOGW(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define BB_LOGE(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define BB_LOGI   bb_host_log_info
#define BB_LOGW   bb_host_log_warn
#define BB_LOGE   bb_host_log_err

static void bb_host_log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    (void)fmt;
    va_end(args);
}

static void bb_host_log_warn(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "[W] bb_storage: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static void bb_host_log_err(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "[E] bb_storage: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static bb_storage_test_faults_t   s_test_faults;
static bb_storage_test_counters_t s_test_counters;

void bb_storage_test_set_faults(const bb_storage_test_faults_t *faults)
{
    if (faults != NULL) {
        s_test_faults = *faults;
    }
}

void bb_storage_test_reset_faults(void)
{
    memset(&s_test_faults, 0, sizeof(s_test_faults));
    memset(&s_test_counters, 0, sizeof(s_test_counters));
    s_test_faults.mock_total_bytes = 16ULL * 1024ULL * 1024ULL;
    s_test_faults.mock_free_bytes  = 12ULL * 1024ULL * 1024ULL;
}

void bb_storage_test_get_counters(bb_storage_test_counters_t *out_counters)
{
    if (out_counters != NULL) {
        *out_counters = s_test_counters;
    }
}
#endif /* BB_STORAGE_HOST_TEST */

/* =========================================================================
 * Internal State
 * ========================================================================= */

typedef struct {
    bool                initialized;
    bool                spi_bus_owned;
    char                mount_point[BB_STORAGE_MAX_MOUNT_PATH_LEN + 1U];
    bb_storage_config_t config;
    bb_storage_stats_t  stats;
#if defined(__GNUC__) || defined(__clang__)
    int                 status_atomic;
#else
    volatile int        status_atomic;
#endif
#ifndef BB_STORAGE_HOST_TEST
    sdmmc_card_t       *card;
    StaticSemaphore_t   mutex_buf;
    SemaphoreHandle_t   mutex;
#endif
} bb_storage_ctx_t;

static bb_storage_ctx_t s_ctx;

#ifndef BB_STORAGE_HOST_TEST
static portMUX_TYPE s_mutex_init_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void bb_ensure_mutex(void)
{
    /* Acquire the mux unconditionally instead of unsynchronized double-checked loading. */
    portENTER_CRITICAL(&s_mutex_init_spinlock);
    if (s_ctx.mutex == NULL) {
        s_ctx.mutex = xSemaphoreCreateMutexStatic(&s_ctx.mutex_buf);
    }
    portEXIT_CRITICAL(&s_mutex_init_spinlock);
}

static void bb_lock(void)
{
    bb_ensure_mutex();
    if (s_ctx.mutex != NULL) {
        (void)xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    }
}

static void bb_unlock(void)
{
    if (s_ctx.mutex != NULL) {
        (void)xSemaphoreGive(s_ctx.mutex);
    }
}
#elif defined(_WIN32)
#if defined(__GNUC__) || defined(__clang__)
static int s_win_mutex_state = 0; /* 0=uninit, 1=initing, 2=ready */
#else
static bool s_win_mutex_ready = false;
#endif
static CRITICAL_SECTION s_win_cs;

static void bb_ensure_mutex(void)
{
#if defined(__GNUC__) || defined(__clang__)
    int expected = 0;
    if (__atomic_load_n(&s_win_mutex_state, __ATOMIC_ACQUIRE) == 2) {
        return;
    }
    if (__atomic_compare_exchange_n(&s_win_mutex_state, &expected, 1,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        InitializeCriticalSection(&s_win_cs);
        __atomic_store_n(&s_win_mutex_state, 2, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&s_win_mutex_state, __ATOMIC_ACQUIRE) != 2) {
            Sleep(0);
        }
    }
#else
    if (!s_win_mutex_ready) {
        InitializeCriticalSection(&s_win_cs);
        s_win_mutex_ready = true;
    }
#endif
}

static void bb_lock(void)
{
    bb_ensure_mutex();
    EnterCriticalSection(&s_win_cs);
}

static void bb_unlock(void)
{
    LeaveCriticalSection(&s_win_cs);
}
#else
static pthread_mutex_t s_posix_mutex = PTHREAD_MUTEX_INITIALIZER;

static void bb_lock(void)
{
    (void)pthread_mutex_lock(&s_posix_mutex);
}

static void bb_unlock(void)
{
    (void)pthread_mutex_unlock(&s_posix_mutex);
}
#endif

static void bb_set_status(bb_storage_status_t status)
{
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(&s_ctx.status_atomic, (int)status, __ATOMIC_RELEASE);
#else
    s_ctx.status_atomic = (int)status;
#endif
    s_ctx.stats.status = status;
}

static bb_storage_status_t bb_get_status_atomic(void)
{
#if defined(__GNUC__) || defined(__clang__)
    return (bb_storage_status_t)__atomic_load_n(&s_ctx.status_atomic, __ATOMIC_ACQUIRE);
#else
    return (bb_storage_status_t)s_ctx.status_atomic;
#endif
}

/* =========================================================================
 * Portable Formatting & Parsing Helpers
 * ========================================================================= */

/**
 * Format a uint64_t into decimal ASCII without relying on platform-specific
 * printf length modifiers (%llu vs %I64u).
 */
static void format_u64_dec(uint64_t value, char *buf, size_t buf_size)
{
    char tmp[24];
    size_t i = 0;
    size_t j = 0;

    if (buf == NULL || buf_size == 0U) {
        return;
    }
    if (value == 0ULL) {
        if (buf_size >= 2U) {
            buf[0] = '0';
            buf[1] = '\0';
        } else {
            buf[0] = '\0';
        }
        return;
    }
    while (value > 0ULL && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }
    while (i > 0U && (j + 1U) < buf_size) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static bool parse_u64_dec(const char *str, uint64_t *out_val)
{
    uint64_t result = 0ULL;
    size_t digits = 0U;

    if (str == NULL || out_val == NULL || *str == '\0') {
        return false;
    }
    while (*str >= '0' && *str <= '9') {
        uint64_t d = (uint64_t)(*str - '0');
        if (result > ((~0ULL - d) / 10ULL)) {
            return false;
        }
        result = (result * 10ULL) + d;
        str++;
        digits++;
    }
    if (digits == 0U || *str != '\0') {
        return false;
    }
    *out_val = result;
    return true;
}

static bool parse_u32_dec(const char *str, uint32_t *out_val)
{
    uint64_t tmp = 0ULL;

    if (!parse_u64_dec(str, &tmp) || tmp > 0xFFFFFFFFULL) {
        return false;
    }
    *out_val = (uint32_t)tmp;
    return true;
}

static bool parse_i32_dec(const char *str, int32_t *out_val)
{
    bool neg = false;
    uint64_t mag = 0ULL;
    uint64_t limit = 0x7FFFFFFFULL;

    if (str == NULL || out_val == NULL || *str == '\0') {
        return false;
    }
    if (*str == '-') {
        neg = true;
        limit = 0x80000000ULL;
        str++;
    }
    if (!parse_u64_dec(str, &mag) || mag > limit) {
        return false;
    }
    if (neg) {
        if (mag == 0x80000000ULL) {
            *out_val = (-2147483647L - 1L);
        } else {
            *out_val = -(int32_t)mag;
        }
    } else {
        *out_val = (int32_t)mag;
    }
    return true;
}

static bool parse_u32_hex(const char *str, size_t expected_digits, uint32_t *out_val)
{
    uint32_t val = 0UL;
    size_t count = 0U;

    if (str == NULL || out_val == NULL || *str == '\0') {
        return false;
    }
    while (*str != '\0') {
        uint32_t nibble;
        char c = *str;
        if (c >= '0' && c <= '9') {
            nibble = (uint32_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nibble = (uint32_t)(10 + (c - 'A'));
        } else if (c >= 'a' && c <= 'f') {
            nibble = (uint32_t)(10 + (c - 'a'));
        } else {
            return false;
        }
        if (count >= 8U) {
            return false;
        }
        val = (val << 4) | nibble;
        count++;
        str++;
    }
    if (expected_digits != 0U && count != expected_digits) {
        return false;
    }
    *out_val = val;
    return true;
}

static bool join_mount_filename(const char *mount_point,
                                const char *filename,
                                char *out_path,
                                size_t out_size)
{
    size_t mount_len;
    size_t file_len;
    size_t needed;

    if (mount_point == NULL || filename == NULL || out_path == NULL) {
        return false;
    }
    mount_len = strlen(mount_point);
    file_len = strlen(filename);
    needed = mount_len + 1U + file_len + 1U;
    if (out_size < needed) {
        return false;
    }
    memcpy(out_path, mount_point, mount_len);
    out_path[mount_len] = '/';
    memcpy(out_path + mount_len + 1U, filename, file_len);
    out_path[needed - 1U] = '\0';
    return true;
}

static bool append_str(char *dst, size_t dst_size, size_t *io_pos, const char *src)
{
    size_t len;
    if (dst == NULL || io_pos == NULL || src == NULL) {
        return false;
    }
    len = strlen(src);
    if ((*io_pos + len + 1U) > dst_size) {
        return false;
    }
    memcpy(dst + *io_pos, src, len);
    *io_pos += len;
    dst[*io_pos] = '\0';
    return true;
}

/* =========================================================================
 * CRC-32 Helper (IEEE 802.3 polynomial 0xEDB88320)
 * ========================================================================= */

static uint32_t crc32_update_byte(uint32_t crc, uint8_t data)
{
    int bit;
    crc ^= (uint32_t)data;
    for (bit = 0; bit < 8; bit++) {
        if ((crc & 1UL) != 0UL) {
            crc = (crc >> 1) ^ 0xEDB88320UL;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static uint32_t crc32_update_u32_le(uint32_t crc, uint32_t val)
{
    crc = crc32_update_byte(crc, (uint8_t)(val & 0xFFU));
    crc = crc32_update_byte(crc, (uint8_t)((val >> 8) & 0xFFU));
    crc = crc32_update_byte(crc, (uint8_t)((val >> 16) & 0xFFU));
    crc = crc32_update_byte(crc, (uint8_t)((val >> 24) & 0xFFU));
    return crc;
}

static uint32_t crc32_update_u64_le(uint32_t crc, uint64_t val)
{
    crc = crc32_update_u32_le(crc, (uint32_t)(val & 0xFFFFFFFFULL));
    crc = crc32_update_u32_le(crc, (uint32_t)((val >> 32) & 0xFFFFFFFFULL));
    return crc;
}

uint32_t bb_storage_compute_samples_crc32(const bb_storage_sample_t *samples, size_t count)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t i;

    if (samples == NULL || count == 0U) {
        return 0UL;
    }
    for (i = 0U; i < count; i++) {
        crc = crc32_update_u64_le(crc, samples[i].timestamp_us);
        crc = crc32_update_u32_le(crc, (uint32_t)samples[i].offset_ms);
        crc = crc32_update_byte(crc, samples[i].sensor_id);
        crc = crc32_update_u32_le(crc, (uint32_t)samples[i].accel_x_mg);
        crc = crc32_update_u32_le(crc, (uint32_t)samples[i].accel_y_mg);
        crc = crc32_update_u32_le(crc, (uint32_t)samples[i].accel_z_mg);
        crc = crc32_update_u32_le(crc, (uint32_t)samples[i].gyro_x_mdps);
        crc = crc32_update_u32_le(crc, (uint32_t)samples[i].gyro_y_mdps);
        crc = crc32_update_u32_le(crc, (uint32_t)samples[i].gyro_z_mdps);
    }
    return ~crc;
}

/* =========================================================================
 * Validation & Path Helpers
 * ========================================================================= */

static size_t mount_point_max_len(void)
{
    size_t max_len = BB_STORAGE_MAX_MOUNT_PATH_LEN;

#ifndef BB_STORAGE_HOST_TEST
    /*
     * ESP-IDF vfs_fat.c copies base_path with strlcpy(..., sizeof(base_path) - 1)
     * where sizeof(base_path) == ESP_VFS_PATH_MAX (15), truncating paths > 13 chars
     * and breaking subsequent esp_vfs_fat_info() / esp_vfs_fat_sdcard_unmount() lookups.
     */
    if ((size_t)ESP_VFS_PATH_MAX > 2U && ((size_t)ESP_VFS_PATH_MAX - 2U) < max_len) {
        max_len = (size_t)ESP_VFS_PATH_MAX - 2U;
    }
#endif
    return max_len;
}

static bool is_valid_mount_point(const char *mount_point)
{
    size_t len;
    size_t i;
    const size_t max_len = mount_point_max_len();

    if (mount_point == NULL) {
        return false;
    }
    len = strlen(mount_point);
#ifndef BB_STORAGE_HOST_TEST
    if (mount_point[0] != '/') {
        return false;
    }
#else
    if (!(mount_point[0] == '/' || (len >= 3U && strncmp(mount_point, "./", 2U) == 0))) {
        return false;
    }
#endif
    if (len < 2U || len > max_len) {
        return false;
    }
    /* Reject trailing slash, backslashes, duplicate slashes, traversal, or control/whitespace */
    if (mount_point[len - 1U] == '/' || mount_point[len - 1U] == '\\') {
        return false;
    }
    if (strstr(mount_point, "..") != NULL || strstr(mount_point, "//") != NULL) {
        return false;
    }
    for (i = 0U; i < len; i++) {
        unsigned char c = (unsigned char)mount_point[i];
        if (c == '\\' || iscntrl(c) || isspace(c)) {
            return false;
        }
    }
    return true;
}

static bool is_valid_esp32s3_gpio(gpio_num_t pin, bool output_required)
{
#ifndef BB_STORAGE_HOST_TEST
    if (output_required) {
        return GPIO_IS_VALID_OUTPUT_GPIO(pin);
    }
    return GPIO_IS_VALID_GPIO(pin);
#else
    (void)output_required;
    if (pin < 0 || pin >= 49) {
        return false;
    }
    if (pin >= 22 && pin <= 25) {
        return false;
    }
    return true;
#endif
}

static esp_err_t validate_config(const bb_storage_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_valid_mount_point(cfg->mount_point)) {
        BB_LOGE("Invalid mount_point (must start with '/', <= %u chars, no trailing slash)",
                (unsigned)mount_point_max_len());
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->host_id < SPI2_HOST || cfg->host_id >= SPI_HOST_MAX) {
        BB_LOGE("Invalid SPI host_id (%d); expected SPI2_HOST (%d) or SPI3_HOST (%d)",
                (int)cfg->host_id, (int)SPI2_HOST, (int)SPI3_HOST);
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->format_if_mount_failed) {
        BB_LOGE("format_if_mount_failed=true is forbidden by safety policy");
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->max_open_files < 1 || cfg->max_open_files > 16) {
        BB_LOGE("max_open_files (%d) out of range [1..16]", cfg->max_open_files);
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->max_freq_khz <= 0 || cfg->max_freq_khz > 40000) {
        BB_LOGE("max_freq_khz (%d) out of range (1..40000)", cfg->max_freq_khz);
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->gpio_cs < 0 || !is_valid_esp32s3_gpio(cfg->gpio_cs, true)) {
        BB_LOGE("gpio_cs (%d) is not configured or invalid for ESP32-S3 output", (int)cfg->gpio_cs);
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->manage_spi_bus) {
        if (cfg->gpio_mosi < 0 || cfg->gpio_miso < 0 || cfg->gpio_sclk < 0) {
            BB_LOGE("SPI pins (MOSI=%d, MISO=%d, SCLK=%d) must be configured when manage_spi_bus=true",
                    (int)cfg->gpio_mosi, (int)cfg->gpio_miso, (int)cfg->gpio_sclk);
            return ESP_ERR_INVALID_ARG;
        }
        if (!is_valid_esp32s3_gpio(cfg->gpio_mosi, true) ||
            !is_valid_esp32s3_gpio(cfg->gpio_miso, false) ||
            !is_valid_esp32s3_gpio(cfg->gpio_sclk, true)) {
            BB_LOGE("Invalid ESP32-S3 GPIO pin in SPI config (MOSI=%d, MISO=%d, SCLK=%d)",
                    (int)cfg->gpio_mosi, (int)cfg->gpio_miso, (int)cfg->gpio_sclk);
            return ESP_ERR_INVALID_ARG;
        }
        if (cfg->gpio_mosi == cfg->gpio_miso ||
            cfg->gpio_mosi == cfg->gpio_sclk ||
            cfg->gpio_mosi == cfg->gpio_cs ||
            cfg->gpio_miso == cfg->gpio_sclk ||
            cfg->gpio_miso == cfg->gpio_cs ||
            cfg->gpio_sclk == cfg->gpio_cs) {
            BB_LOGE("Duplicate GPIO pin assignment detected in SPI configuration");
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        if ((cfg->gpio_mosi >= 0 && !is_valid_esp32s3_gpio(cfg->gpio_mosi, true)) ||
            (cfg->gpio_miso >= 0 && !is_valid_esp32s3_gpio(cfg->gpio_miso, false)) ||
            (cfg->gpio_sclk >= 0 && !is_valid_esp32s3_gpio(cfg->gpio_sclk, true))) {
            BB_LOGE("Optional SPI pin out of valid ESP32-S3 GPIO range");
            return ESP_ERR_INVALID_ARG;
        }
        if ((cfg->gpio_mosi >= 0 && cfg->gpio_mosi == cfg->gpio_cs) ||
            (cfg->gpio_miso >= 0 && cfg->gpio_miso == cfg->gpio_cs) ||
            (cfg->gpio_sclk >= 0 && cfg->gpio_sclk == cfg->gpio_cs)) {
            BB_LOGE("gpio_cs collides with configured SPI bus pin");
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

esp_err_t bb_storage_validate_event(const bb_event_record_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event->format_version != BB_STORAGE_FORMAT_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event->event_id == 0UL || event->event_id > BB_STORAGE_MAX_EVENT_ID) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event->sample_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event->sample_count > BB_STORAGE_MAX_SAMPLES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (event->samples == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event->pre_event_samples > event->sample_count ||
        event->post_event_samples > (event->sample_count - event->pre_event_samples)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t bb_storage_build_event_paths(const char *mount_point,
                                       uint32_t event_id,
                                       char *out_tmp_path,
                                       size_t tmp_size,
                                       char *out_final_path,
                                       size_t final_size)
{
    size_t mount_len;
    size_t needed_len;

    if (!is_valid_mount_point(mount_point)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event_id == 0UL || event_id > BB_STORAGE_MAX_EVENT_ID) {
        return ESP_ERR_INVALID_ARG;
    }

    mount_len = strlen(mount_point);
    /* "/E0000001.CSV" is 13 chars + 1 null terminator = 14 bytes */
    needed_len = mount_len + 14U;

    if (out_tmp_path != NULL) {
        int written;
        if (tmp_size < needed_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        written = snprintf(out_tmp_path, tmp_size, "%s/E%07lX.TMP", mount_point, (unsigned long)event_id);
        if (written < 0 || (size_t)written >= tmp_size) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (out_final_path != NULL) {
        int written;
        if (final_size < needed_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        written = snprintf(out_final_path, final_size, "%s/E%07lX.CSV", mount_point, (unsigned long)event_id);
        if (written < 0 || (size_t)written >= final_size) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    return ESP_OK;
}

bool bb_storage_is_completed_event_filename(const char *filename)
{
    size_t i;

    if (filename == NULL) {
        return false;
    }
    /* Exact 8.3 pattern: "E" + 7 uppercase hex digits + ".CSV" = 12 characters */
    if (strlen(filename) != 12U) {
        return false;
    }
    if (filename[0] != 'E') {
        return false;
    }
    for (i = 1U; i <= 7U; i++) {
        char c = filename[i];
        bool is_upper_hex = ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
        if (!is_upper_hex) {
            return false;
        }
    }
    /* Ensure event_id is not all zeros ("E0000000.CSV") */
    if (strncmp(&filename[1], "0000000", 7U) == 0) {
        return false;
    }
    if (strcmp(&filename[8], ".CSV") != 0) {
        return false;
    }
    return true;
}

/* =========================================================================
 * CSV Serialization
 * ========================================================================= */

static const char CSV_COLUMNS_HEADER[] =
    "sample_idx,timestamp_us,offset_ms,sensor,ax_mg,ay_mg,az_mg,gx_mdps,gy_mdps,gz_mdps";

static int format_event_header_text(const bb_event_record_t *event, char *buf, size_t buf_size)
{
    char ts_buf[24];

    format_u64_dec(event->trigger_timestamp_us, ts_buf, sizeof(ts_buf));
    return snprintf(buf,
                    buf_size,
                    "# BLACKBOX_EVENT_FORMAT,v%u\n"
                    "# event_id,%lu\n"
                    "# trigger_timestamp_us,%s\n"
                    "# sample_rate_hz,%u\n"
                    "# flags,0x%08lX\n"
                    "# pre_event_samples,%lu\n"
                    "# post_event_samples,%lu\n"
                    "# sample_count,%lu\n"
                    "%s\n",
                    (unsigned)event->format_version,
                    (unsigned long)event->event_id,
                    ts_buf,
                    (unsigned)event->sample_rate_hz,
                    (unsigned long)event->flags,
                    (unsigned long)event->pre_event_samples,
                    (unsigned long)event->post_event_samples,
                    (unsigned long)event->sample_count,
                    CSV_COLUMNS_HEADER);
}

static int format_sample_row_text(size_t idx,
                                  const bb_storage_sample_t *s,
                                  char *buf,
                                  size_t buf_size)
{
    char ts_buf[24];

    format_u64_dec(s->timestamp_us, ts_buf, sizeof(ts_buf));
    return snprintf(buf,
                    buf_size,
                    "%lu,%s,%ld,%u,%ld,%ld,%ld,%ld,%ld,%ld\n",
                    (unsigned long)idx,
                    ts_buf,
                    (long)s->offset_ms,
                    (unsigned)s->sensor_id,
                    (long)s->accel_x_mg,
                    (long)s->accel_y_mg,
                    (long)s->accel_z_mg,
                    (long)s->gyro_x_mdps,
                    (long)s->gyro_y_mdps,
                    (long)s->gyro_z_mdps);
}

static int format_event_footer_text(size_t sample_count,
                                    uint32_t crc,
                                    char *buf,
                                    size_t buf_size)
{
    return snprintf(buf,
                    buf_size,
                    "# END_EVENT,count=%lu,crc32=0x%08lX\n",
                    (unsigned long)sample_count,
                    (unsigned long)crc);
}

esp_err_t bb_storage_format_event_csv(const bb_event_record_t *event,
                                      char *out_buf,
                                      size_t buf_size,
                                      size_t *out_len)
{
    esp_err_t err;
    size_t pos = 0U;
    size_t i;
    uint32_t crc;
    int rc;
    char line[384];

    if (out_len != NULL) {
        *out_len = 0U;
    }

    err = bb_storage_validate_event(event);
    if (err != ESP_OK) {
        return err;
    }
    if (out_buf == NULL || buf_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    out_buf[0] = '\0';

    rc = format_event_header_text(event, line, sizeof(line));
    if (rc <= 0 || (size_t)rc >= sizeof(line)) {
        return ESP_FAIL;
    }
    if (!append_str(out_buf, buf_size, &pos, line)) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (i = 0U; i < event->sample_count; i++) {
        rc = format_sample_row_text(i, &event->samples[i], line, sizeof(line));
        if (rc <= 0 || (size_t)rc >= sizeof(line)) {
            return ESP_FAIL;
        }
        if (!append_str(out_buf, buf_size, &pos, line)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    crc = bb_storage_compute_samples_crc32(event->samples, event->sample_count);
    rc = format_event_footer_text(event->sample_count, crc, line, sizeof(line));
    if (rc <= 0 || (size_t)rc >= sizeof(line)) {
        return ESP_FAIL;
    }
    if (!append_str(out_buf, buf_size, &pos, line)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_len != NULL) {
        *out_len = pos;
    }
    return ESP_OK;
}

/* =========================================================================
 * Storage Lifecycle (Init / Deinit / Status / Stats)
 * ========================================================================= */

esp_err_t bb_storage_init(const bb_storage_config_t *cfg)
{
    esp_err_t err;
    bool bus_initialized_here = false;

    bb_lock();

    if (s_ctx.initialized) {
        BB_LOGW("bb_storage_init called while already initialized");
        bb_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    err = validate_config(cfg);
    if (err != ESP_OK) {
        bb_set_status(BB_STORAGE_STATUS_UNINITIALIZED);
        s_ctx.stats.last_error = err;
        bb_unlock();
        return err;
    }

#ifndef BB_STORAGE_HOST_TEST
    if (cfg->manage_spi_bus) {
        /*
         * Initialize SPI bus for SDSPI.
         * Reference: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/sdspi_host.html
         */
        spi_bus_config_t bus_cfg;
        memset(&bus_cfg, 0, sizeof(bus_cfg));
        bus_cfg.mosi_io_num = cfg->gpio_mosi;
        bus_cfg.miso_io_num = cfg->gpio_miso;
        bus_cfg.sclk_io_num = cfg->gpio_sclk;
        bus_cfg.quadwp_io_num = -1;
        bus_cfg.quadhd_io_num = -1;
        bus_cfg.data4_io_num = -1;
        bus_cfg.data5_io_num = -1;
        bus_cfg.data6_io_num = -1;
        bus_cfg.data7_io_num = -1;
        bus_cfg.max_transfer_sz = 4000;

        err = spi_bus_initialize(cfg->host_id, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK) {
            BB_LOGE("spi_bus_initialize(host=%d) failed: 0x%x", (int)cfg->host_id, (unsigned)err);
            bb_set_status(BB_STORAGE_STATUS_MOUNT_FAILED);
            s_ctx.stats.last_error = err;
            bb_unlock();
            return err;
        }
        bus_initialized_here = true;
    }

    /*
     * Mount FAT partition over SDSPI.
     * format_if_mount_failed is strictly false to prevent accidental data loss.
     * Reference: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/storage/fatfs.html#esp-vfs-fat-sdspi-mount
     */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = cfg->max_open_files,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = cfg->host_id;
    host.max_freq_khz = cfg->max_freq_khz;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = cfg->host_id;
    slot_config.gpio_cs = cfg->gpio_cs;

    sdmmc_card_t *card = NULL;
    err = esp_vfs_fat_sdspi_mount(cfg->mount_point, &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK) {
        BB_LOGE("esp_vfs_fat_sdspi_mount(%s) failed: 0x%x", cfg->mount_point, (unsigned)err);
        if (bus_initialized_here) {
            (void)spi_bus_free(cfg->host_id);
        }
        bb_set_status(BB_STORAGE_STATUS_MOUNT_FAILED);
        s_ctx.stats.last_error = err;
        bb_unlock();
        return err;
    }

    s_ctx.card = card;
    s_ctx.spi_bus_owned = bus_initialized_here;
#if !defined(CONFIG_FATFS_FS_LOCK) || (CONFIG_FATFS_FS_LOCK < 1)
    BB_LOGW("CONFIG_FATFS_FS_LOCK is disabled; set CONFIG_FATFS_FS_LOCK=16 in sdkconfig.defaults for concurrent FATFS access");
#endif
    sdmmc_card_print_info(stdout, card);
#else
    if (cfg->manage_spi_bus) {
        s_test_counters.spi_bus_init_calls++;
        if (s_test_faults.fail_spi_bus) {
            bb_set_status(BB_STORAGE_STATUS_MOUNT_FAILED);
            s_ctx.stats.last_error = ESP_FAIL;
            bb_unlock();
            return ESP_FAIL;
        }
        bus_initialized_here = true;
    }

    s_test_counters.mount_calls++;
    if (s_test_faults.fail_mount) {
        if (bus_initialized_here) {
            s_test_counters.spi_bus_free_calls++;
        }
        bb_set_status(BB_STORAGE_STATUS_MOUNT_FAILED);
        s_ctx.stats.last_error = ESP_FAIL;
        bb_unlock();
        return ESP_FAIL;
    }

    {
#ifdef _WIN32
        DWORD attrs = GetFileAttributesA(cfg->mount_point);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            if (CreateDirectoryA(cfg->mount_point, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
                attrs = GetFileAttributesA(cfg->mount_point);
            }
        }
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            BB_LOGE("Host simulated mount dir %s unavailable", cfg->mount_point);
            if (bus_initialized_here) {
                s_test_counters.spi_bus_free_calls++;
            }
            bb_set_status(BB_STORAGE_STATUS_MOUNT_FAILED);
            s_ctx.stats.last_error = ESP_FAIL;
            bb_unlock();
            return ESP_FAIL;
        }
#else
        struct stat st;
        int mk_rc = mkdir(cfg->mount_point, 0777);
        if (mk_rc != 0) {
            if (errno != EEXIST || stat(cfg->mount_point, &st) != 0 || !S_ISDIR(st.st_mode)) {
                BB_LOGE("Host simulated mount dir %s unavailable (errno=%d)", cfg->mount_point, errno);
                if (bus_initialized_here) {
                    s_test_counters.spi_bus_free_calls++;
                }
                bb_set_status(BB_STORAGE_STATUS_MOUNT_FAILED);
                s_ctx.stats.last_error = ESP_FAIL;
                bb_unlock();
                return ESP_FAIL;
            }
        }
#endif
    }
    s_ctx.spi_bus_owned = bus_initialized_here;
#endif /* BB_STORAGE_HOST_TEST */

    s_ctx.config = *cfg;
    strncpy(s_ctx.mount_point, cfg->mount_point, BB_STORAGE_MAX_MOUNT_PATH_LEN);
    s_ctx.mount_point[BB_STORAGE_MAX_MOUNT_PATH_LEN] = '\0';
    s_ctx.config.mount_point = s_ctx.mount_point;

    memset(&s_ctx.stats, 0, sizeof(s_ctx.stats));
    bb_set_status(BB_STORAGE_STATUS_READY);
    s_ctx.stats.last_error = ESP_OK;
    s_ctx.initialized = true;

    BB_LOGI("Storage mounted at %s (manage_spi_bus=%d, cs=%d)",
            s_ctx.mount_point, (int)cfg->manage_spi_bus, (int)cfg->gpio_cs);

    bb_unlock();
    return ESP_OK;
}

esp_err_t bb_storage_deinit(void)
{
    esp_err_t err = ESP_OK;

    bb_lock();
    if (!s_ctx.initialized) {
        bb_unlock();
        return ESP_ERR_INVALID_STATE;
    }

#ifndef BB_STORAGE_HOST_TEST
    err = esp_vfs_fat_sdcard_unmount(s_ctx.mount_point, s_ctx.card);
    if (err != ESP_OK) {
        BB_LOGE("esp_vfs_fat_sdcard_unmount failed: 0x%x", (unsigned)err);
    }
    /*
     * esp_vfs_fat_sdcard_unmount() frees s_ctx.card before esp_vfs_fat_unregister_path(),
     * so always clear local card/initialized state and free an owned SPI bus.
     */
    s_ctx.card = NULL;
    s_ctx.initialized = false;
    bb_set_status(BB_STORAGE_STATUS_UNINITIALIZED);

    if (s_ctx.spi_bus_owned) {
        esp_err_t bus_err = spi_bus_free(s_ctx.config.host_id);
        if (bus_err != ESP_OK) {
            BB_LOGE("spi_bus_free(host=%d) failed: 0x%x", (int)s_ctx.config.host_id, (unsigned)bus_err);
            if (err == ESP_OK) {
                err = bus_err;
            }
        }
        s_ctx.spi_bus_owned = false;
    }
#else
    s_test_counters.unmount_calls++;
    if (s_test_faults.fail_unmount) {
        err = ESP_FAIL;
    }
    /* Match target ESP-IDF teardown: always clear local initialized state and release owned SPI bus. */
    s_ctx.initialized = false;
    bb_set_status(BB_STORAGE_STATUS_UNINITIALIZED);

    if (s_ctx.spi_bus_owned) {
        s_test_counters.spi_bus_free_calls++;
        s_ctx.spi_bus_owned = false;
        if (s_test_faults.fail_spi_bus_free && err == ESP_OK) {
            err = ESP_FAIL;
        }
    }
#endif

    s_ctx.stats.total_bytes = 0ULL;
    s_ctx.stats.free_bytes = 0ULL;
    s_ctx.stats.last_error = err;

    bb_unlock();
    return err;
}

bb_storage_status_t bb_storage_get_status(void)
{
    return bb_get_status_atomic();
}

const char *bb_storage_status_to_str(bb_storage_status_t status)
{
    switch (status) {
        case BB_STORAGE_STATUS_UNINITIALIZED:
            return "UNINITIALIZED";
        case BB_STORAGE_STATUS_READY:
            return "READY";
        case BB_STORAGE_STATUS_MOUNT_FAILED:
            return "MOUNT_FAILED";
        case BB_STORAGE_STATUS_WRITE_ERROR:
            return "WRITE_ERROR";
        case BB_STORAGE_STATUS_FULL:
            return "FULL";
        default:
            return "UNKNOWN";
    }
}

esp_err_t bb_storage_get_stats(bb_storage_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bb_lock();
    s_ctx.stats.total_bytes = 0ULL;
    s_ctx.stats.free_bytes = 0ULL;
    if (s_ctx.initialized) {
#ifndef BB_STORAGE_HOST_TEST
        uint64_t total = 0ULL;
        uint64_t free_b = 0ULL;
        if (esp_vfs_fat_info(s_ctx.mount_point, &total, &free_b) == ESP_OK) {
            s_ctx.stats.total_bytes = total;
            s_ctx.stats.free_bytes = free_b;
        }
#else
        s_ctx.stats.total_bytes = s_test_faults.mock_total_bytes;
        s_ctx.stats.free_bytes  = s_test_faults.mock_free_bytes;
#endif
    }
    *out_stats = s_ctx.stats;
    bb_unlock();
    return ESP_OK;
}

/* =========================================================================
 * Event Persistence (Two-Phase Write .TMP -> .CSV)
 * ========================================================================= */

static bool file_exists(const char *path)
{
#if defined(BB_STORAGE_HOST_TEST) && defined(_WIN32)
    return (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    return (stat(path, &st) == 0);
#endif
}

static esp_err_t map_errno_to_err(int saved_errno)
{
    return (saved_errno == ENOSPC) ? ESP_ERR_NO_MEM : ESP_FAIL;
}

static esp_err_t write_event_to_paths_locked(const bb_event_record_t *event,
                                             const char *tmp_path,
                                             const char *final_path,
                                             bool update_stats,
                                             size_t *out_bytes_written)
{
    esp_err_t err = ESP_FAIL;
    FILE *fp = NULL;
    size_t i;
    size_t bytes_written = 0U;
    int rc;
    int saved_errno = 0;
    uint32_t crc;
    char line[384];

    if (out_bytes_written != NULL) {
        *out_bytes_written = 0U;
    }

    /* Refuse to overwrite an existing completed event recording */
    if (file_exists(final_path)) {
        BB_LOGW("Finalized event file already exists: %s", final_path);
        if (update_stats) {
            s_ctx.stats.last_error = ESP_ERR_INVALID_STATE;
        }
        return ESP_ERR_INVALID_STATE;
    }

    /* Clean up any stale temporary file from a previously interrupted write */
    (void)remove(tmp_path);

#ifdef BB_STORAGE_HOST_TEST
    if (s_test_faults.fail_fopen) {
        BB_LOGE("Simulated fopen failure on %s", tmp_path);
        err = ESP_FAIL;
        goto cleanup_error;
    }
#endif

    errno = 0;
    fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        saved_errno = errno;
        BB_LOGE("Failed to open temporary file %s (errno=%d)", tmp_path, saved_errno);
        err = map_errno_to_err(saved_errno);
        goto cleanup_error;
    }

#ifdef BB_STORAGE_HOST_TEST
    if (s_test_faults.fail_header_write) {
        errno = EIO;
        saved_errno = EIO;
        err = ESP_FAIL;
        goto cleanup_error;
    }
#endif

    rc = format_event_header_text(event, line, sizeof(line));
    if (rc <= 0 || (size_t)rc >= sizeof(line)) {
        err = ESP_FAIL;
        goto cleanup_error;
    }
    errno = 0;
    if (fputs(line, fp) == EOF || ferror(fp)) {
        saved_errno = errno;
        err = map_errno_to_err(saved_errno);
        goto cleanup_error;
    }
    bytes_written += (size_t)rc;

    for (i = 0U; i < event->sample_count; i++) {
#ifdef BB_STORAGE_HOST_TEST
        if (i >= s_test_faults.fail_after_samples) {
            if (s_test_faults.fail_write_enospc) {
                errno = ENOSPC;
                err = ESP_ERR_NO_MEM;
                goto cleanup_error;
            }
            if (s_test_faults.fail_write_io) {
                errno = EIO;
                err = ESP_FAIL;
                goto cleanup_error;
            }
        }
#endif

        rc = format_sample_row_text(i, &event->samples[i], line, sizeof(line));
        if (rc <= 0 || (size_t)rc >= sizeof(line)) {
            err = ESP_FAIL;
            goto cleanup_error;
        }
        errno = 0;
        if (fputs(line, fp) == EOF || ferror(fp)) {
            saved_errno = errno;
            err = map_errno_to_err(saved_errno);
            goto cleanup_error;
        }
        bytes_written += (size_t)rc;
    }

#ifdef BB_STORAGE_HOST_TEST
    if (s_test_faults.fail_footer_write) {
        errno = EIO;
        err = ESP_FAIL;
        goto cleanup_error;
    }
#endif

    crc = bb_storage_compute_samples_crc32(event->samples, event->sample_count);
    rc = format_event_footer_text(event->sample_count, crc, line, sizeof(line));
    if (rc <= 0 || (size_t)rc >= sizeof(line)) {
        err = ESP_FAIL;
        goto cleanup_error;
    }
    errno = 0;
    if (fputs(line, fp) == EOF || ferror(fp)) {
        saved_errno = errno;
        err = map_errno_to_err(saved_errno);
        goto cleanup_error;
    }
    bytes_written += (size_t)rc;

#ifdef BB_STORAGE_HOST_TEST
    if (s_test_faults.fail_fflush) {
        errno = EIO;
        err = ESP_FAIL;
        goto cleanup_error;
    }
#endif

    errno = 0;
    if (fflush(fp) != 0) {
        saved_errno = errno;
        BB_LOGE("fflush failed on %s (errno=%d)", tmp_path, saved_errno);
        err = map_errno_to_err(saved_errno);
        goto cleanup_error;
    }

#ifndef BB_STORAGE_HOST_TEST
    errno = 0;
    if (fsync(fileno(fp)) != 0) {
        saved_errno = errno;
        BB_LOGE("fsync failed on %s (errno=%d)", tmp_path, saved_errno);
        err = map_errno_to_err(saved_errno);
        goto cleanup_error;
    }
#else
    if (s_test_faults.fail_fsync) {
        errno = EIO;
        err = ESP_FAIL;
        goto cleanup_error;
    }
#endif

    errno = 0;
    if (fclose(fp) != 0) {
        saved_errno = errno;
        fp = NULL;
        BB_LOGE("fclose failed on %s (errno=%d)", tmp_path, saved_errno);
        err = map_errno_to_err(saved_errno);
        goto cleanup_error;
    }
    fp = NULL;

#ifdef BB_STORAGE_HOST_TEST
    if (s_test_faults.fail_fclose) {
        errno = EIO;
        err = ESP_FAIL;
        goto cleanup_error;
    }
    if (s_test_faults.fail_rename_enospc) {
        errno = ENOSPC;
        err = ESP_ERR_NO_MEM;
        goto cleanup_error;
    }
    if (s_test_faults.fail_rename) {
        err = ESP_FAIL;
        goto cleanup_error;
    }
#endif

    errno = 0;
    if (rename(tmp_path, final_path) != 0) {
        saved_errno = errno;
        BB_LOGE("Failed to finalize %s -> %s (errno=%d)", tmp_path, final_path, saved_errno);
        err = map_errno_to_err(saved_errno);
        goto cleanup_error;
    }

    if (update_stats) {
        s_ctx.stats.events_written++;
        s_ctx.stats.last_event_id = event->event_id;
        s_ctx.stats.last_write_bytes = bytes_written;
        s_ctx.stats.last_error = ESP_OK;
        bb_set_status(BB_STORAGE_STATUS_READY);
    }
    if (out_bytes_written != NULL) {
        *out_bytes_written = bytes_written;
    }
    return ESP_OK;

cleanup_error:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)remove(tmp_path);
    if (update_stats) {
        s_ctx.stats.write_failures++;
        s_ctx.stats.last_error = err;
        bb_set_status((err == ESP_ERR_NO_MEM) ? BB_STORAGE_STATUS_FULL : BB_STORAGE_STATUS_WRITE_ERROR);
    }
    return err;
}

esp_err_t bb_storage_write_event(const bb_event_record_t *event,
                                 char *out_final_path,
                                 size_t final_path_size)
{
    esp_err_t err;
    char tmp_path[BB_STORAGE_MAX_PATH_LEN];
    char final_path[BB_STORAGE_MAX_PATH_LEN];

    if (out_final_path != NULL) {
        if (final_path_size == 0U) {
            return ESP_ERR_INVALID_ARG;
        }
        out_final_path[0] = '\0';
    }

    err = bb_storage_validate_event(event);
    if (err != ESP_OK) {
        return err;
    }

    bb_lock();
    if (!s_ctx.initialized) {
        bb_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    err = bb_storage_build_event_paths(s_ctx.mount_point,
                                       event->event_id,
                                       tmp_path,
                                       sizeof(tmp_path),
                                       final_path,
                                       sizeof(final_path));
    if (err != ESP_OK) {
        s_ctx.stats.last_error = err;
        bb_unlock();
        return err;
    }

    if (out_final_path != NULL && final_path_size < (strlen(final_path) + 1U)) {
        bb_unlock();
        return ESP_ERR_INVALID_SIZE;
    }

    err = write_event_to_paths_locked(event, tmp_path, final_path, true, NULL);
    if (err == ESP_OK && out_final_path != NULL) {
        strcpy(out_final_path, final_path);
    }

    bb_unlock();
    return err;
}

/* =========================================================================
 * Event File Verification & Listing
 * ========================================================================= */

static bool split_exact_csv_fields(char *line, char *fields[], size_t expected_fields)
{
    size_t idx = 0U;
    char *p = line;

    if (line == NULL || expected_fields == 0U) {
        return false;
    }
    fields[idx++] = p;
    while (*p != '\0') {
        if (*p == ',') {
            if (idx >= expected_fields) {
                return false;
            }
            *p = '\0';
            fields[idx++] = p + 1;
        }
        p++;
    }
    return (idx == expected_fields);
}

static bool parse_sample_csv_row(char *line,
                                 unsigned long expected_idx,
                                 bb_storage_sample_t *out_sample)
{
    char *fields[10];
    uint32_t idx = 0UL;
    uint32_t sensor = 0UL;

    if (line == NULL || out_sample == NULL) {
        return false;
    }
    if (!split_exact_csv_fields(line, fields, 10U)) {
        return false;
    }
    if (!parse_u32_dec(fields[0], &idx) || (unsigned long)idx != expected_idx) {
        return false;
    }
    if (!parse_u64_dec(fields[1], &out_sample->timestamp_us)) {
        return false;
    }
    if (!parse_i32_dec(fields[2], &out_sample->offset_ms)) {
        return false;
    }
    if (!parse_u32_dec(fields[3], &sensor) || sensor > 255UL) {
        return false;
    }
    out_sample->sensor_id = (uint8_t)sensor;

    if (!parse_i32_dec(fields[4], &out_sample->accel_x_mg) ||
        !parse_i32_dec(fields[5], &out_sample->accel_y_mg) ||
        !parse_i32_dec(fields[6], &out_sample->accel_z_mg) ||
        !parse_i32_dec(fields[7], &out_sample->gyro_x_mdps) ||
        !parse_i32_dec(fields[8], &out_sample->gyro_y_mdps) ||
        !parse_i32_dec(fields[9], &out_sample->gyro_z_mdps)) {
        return false;
    }
    return true;
}

static bool strip_line_ending(char *line, size_t buf_size, FILE *fp)
{
    size_t len = strlen(line);
    (void)buf_size;

    if (len > 0U && line[len - 1U] == '\n') {
        line[--len] = '\0';
        if (len > 0U && line[len - 1U] == '\r') {
            line[--len] = '\0';
        }
        return true;
    }
    /* No '\n' found: valid only if at genuine EOF without read error (last line without NL) */
    if (!feof(fp)) {
        /* Line exceeded buffer size */
        return false;
    }
    if (len > 0U && line[len - 1U] == '\r') {
        line[--len] = '\0';
    }
    return true;
}

static bool parse_end_event_footer(const char *line, uint32_t *out_count, uint32_t *out_crc)
{
    const char *p;
    const char *crc_tag;
    char count_buf[16];
    size_t count_len;

    if (strncmp(line, "# END_EVENT,count=", 18U) != 0) {
        return false;
    }
    p = line + 18U;
    crc_tag = strstr(p, ",crc32=0x");
    if (crc_tag == NULL || crc_tag == p) {
        return false;
    }
    count_len = (size_t)(crc_tag - p);
    if (count_len >= sizeof(count_buf)) {
        return false;
    }
    memcpy(count_buf, p, count_len);
    count_buf[count_len] = '\0';
    if (!parse_u32_dec(count_buf, out_count)) {
        return false;
    }
    return parse_u32_hex(crc_tag + 9U, 8U, out_crc);
}

static esp_err_t verify_event_file_locked(const char *file_path,
                                          uint32_t expected_event_id,
                                          size_t expected_sample_count)
{
    FILE *fp;
    char line[384];
    bool has_format_header = false;
    bool has_event_id = false;
    bool has_trigger_ts = false;
    bool has_sample_rate = false;
    bool has_flags = false;
    bool has_pre_samples = false;
    bool has_post_samples = false;
    bool has_sample_count = false;
    bool has_columns_header = false;
    bool has_end_footer = false;
    uint32_t parsed_event_id = 0UL;
    uint32_t header_sample_count = 0UL;
    uint32_t pre_count = 0UL;
    uint32_t post_count = 0UL;
    uint32_t footer_count = 0UL;
    uint32_t footer_crc = 0UL;
    size_t row_count = 0U;
    uint32_t running_crc = 0xFFFFFFFFUL;
    bool io_err = false;

    fp = fopen(file_path, "rb");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (!strip_line_ending(line, sizeof(line), fp) || line[0] == '\0' || has_end_footer) {
            fclose(fp);
            return ESP_FAIL;
        }

        if (!has_columns_header) {
            uint64_t dummy_u64 = 0ULL;
            uint32_t dummy_u32 = 0UL;

            if (strncmp(line, "# BLACKBOX_EVENT_FORMAT,v", 25U) == 0) {
                uint32_t ver = 0UL;
                if (has_format_header || !parse_u32_dec(line + 25U, &ver) || ver != BB_STORAGE_FORMAT_VERSION) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_format_header = true;
            } else if (strncmp(line, "# event_id,", 11U) == 0) {
                if (has_event_id || !parse_u32_dec(line + 11U, &parsed_event_id) ||
                    parsed_event_id == 0UL || parsed_event_id > BB_STORAGE_MAX_EVENT_ID) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_event_id = true;
            } else if (strncmp(line, "# trigger_timestamp_us,", 23U) == 0) {
                if (has_trigger_ts || !parse_u64_dec(line + 23U, &dummy_u64)) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_trigger_ts = true;
            } else if (strncmp(line, "# sample_rate_hz,", 17U) == 0) {
                if (has_sample_rate || !parse_u32_dec(line + 17U, &dummy_u32) || dummy_u32 > 65535UL) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_sample_rate = true;
            } else if (strncmp(line, "# flags,0x", 10U) == 0) {
                if (has_flags || !parse_u32_hex(line + 10U, 8U, &dummy_u32)) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_flags = true;
            } else if (strncmp(line, "# pre_event_samples,", 20U) == 0) {
                if (has_pre_samples || !parse_u32_dec(line + 20U, &pre_count)) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_pre_samples = true;
            } else if (strncmp(line, "# post_event_samples,", 21U) == 0) {
                if (has_post_samples || !parse_u32_dec(line + 21U, &post_count)) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_post_samples = true;
            } else if (strncmp(line, "# sample_count,", 15U) == 0) {
                if (has_sample_count || !parse_u32_dec(line + 15U, &header_sample_count) ||
                    header_sample_count == 0UL || header_sample_count > BB_STORAGE_MAX_SAMPLES) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_sample_count = true;
            } else if (strcmp(line, CSV_COLUMNS_HEADER) == 0) {
                if (!has_format_header || !has_event_id || !has_sample_count) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                if (has_pre_samples && pre_count > header_sample_count) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                if (has_post_samples && post_count > header_sample_count) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                if (has_pre_samples && has_post_samples &&
                    (uint64_t)pre_count + (uint64_t)post_count > (uint64_t)header_sample_count) {
                    fclose(fp);
                    return ESP_FAIL;
                }
                has_columns_header = true;
            } else {
                fclose(fp);
                return ESP_FAIL;
            }
        } else if (strncmp(line, "# END_EVENT,", 12U) == 0) {
            if (!parse_end_event_footer(line, &footer_count, &footer_crc)) {
                fclose(fp);
                return ESP_FAIL;
            }
            has_end_footer = true;
        } else {
            bb_storage_sample_t s;
            if (row_count >= BB_STORAGE_MAX_SAMPLES ||
                !parse_sample_csv_row(line, (unsigned long)row_count, &s)) {
                fclose(fp);
                return ESP_FAIL;
            }
            running_crc = crc32_update_u64_le(running_crc, s.timestamp_us);
            running_crc = crc32_update_u32_le(running_crc, (uint32_t)s.offset_ms);
            running_crc = crc32_update_byte(running_crc, s.sensor_id);
            running_crc = crc32_update_u32_le(running_crc, (uint32_t)s.accel_x_mg);
            running_crc = crc32_update_u32_le(running_crc, (uint32_t)s.accel_y_mg);
            running_crc = crc32_update_u32_le(running_crc, (uint32_t)s.accel_z_mg);
            running_crc = crc32_update_u32_le(running_crc, (uint32_t)s.gyro_x_mdps);
            running_crc = crc32_update_u32_le(running_crc, (uint32_t)s.gyro_y_mdps);
            running_crc = crc32_update_u32_le(running_crc, (uint32_t)s.gyro_z_mdps);
            row_count++;
        }
    }

    io_err = (ferror(fp) != 0);
    if (fclose(fp) != 0 || io_err) {
        return ESP_FAIL;
    }

    if (!has_format_header || !has_event_id || !has_sample_count ||
        !has_columns_header || !has_end_footer || row_count == 0U) {
        return ESP_FAIL;
    }
    if (row_count != (size_t)header_sample_count || row_count != (size_t)footer_count) {
        return ESP_FAIL;
    }
    if (expected_event_id != 0UL && parsed_event_id != expected_event_id) {
        return ESP_FAIL;
    }
    if (expected_sample_count != 0U && row_count != expected_sample_count) {
        return ESP_FAIL;
    }

    running_crc = ~running_crc;
    if (running_crc != footer_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

esp_err_t bb_storage_verify_event_file(const char *file_path,
                                       uint32_t expected_event_id,
                                       size_t expected_sample_count)
{
    esp_err_t err;

    if (file_path == NULL || file_path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (expected_event_id > BB_STORAGE_MAX_EVENT_ID ||
        expected_sample_count > BB_STORAGE_MAX_SAMPLES) {
        return ESP_ERR_INVALID_ARG;
    }

    bb_lock();
    err = verify_event_file_locked(file_path, expected_event_id, expected_sample_count);
    bb_unlock();
    return err;
}

esp_err_t bb_storage_list_completed_events(char out_filenames[][BB_STORAGE_MAX_FILENAME_LEN],
                                           size_t max_files,
                                           size_t *out_count)
{
    size_t count = 0U;

    if (out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_filenames != NULL && max_files == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0U;

    bb_lock();
    if (!s_ctx.initialized) {
        bb_unlock();
        return ESP_ERR_INVALID_STATE;
    }

#if defined(BB_STORAGE_HOST_TEST) && defined(_WIN32)
    {
        char pattern[BB_STORAGE_MAX_PATH_LEN];
        struct _finddata_t fileinfo;
        intptr_t handle;

        if (!join_mount_filename(s_ctx.mount_point, "*.*", pattern, sizeof(pattern))) {
            bb_unlock();
            return ESP_FAIL;
        }
        handle = _findfirst(pattern, &fileinfo);
        if (handle == -1) {
            bb_unlock();
            return ESP_FAIL;
        }
        do {
            if ((fileinfo.attrib & FILE_ATTRIBUTE_DIRECTORY) == 0U &&
                bb_storage_is_completed_event_filename(fileinfo.name)) {
                if (out_filenames != NULL && count < max_files) {
                    strncpy(out_filenames[count], fileinfo.name, BB_STORAGE_MAX_FILENAME_LEN - 1U);
                    out_filenames[count][BB_STORAGE_MAX_FILENAME_LEN - 1U] = '\0';
                }
                count++;
                if (out_filenames != NULL && count >= max_files) {
                    break;
                }
            }
        } while (_findnext(handle, &fileinfo) == 0);
        _findclose(handle);
    }
#else
    {
        DIR *dir = opendir(s_ctx.mount_point);
        struct dirent *entry;
        if (dir == NULL) {
            bb_unlock();
            return ESP_FAIL;
        }
        while ((entry = readdir(dir)) != NULL) {
            if (bb_storage_is_completed_event_filename(entry->d_name)) {
                char entry_path[BB_STORAGE_MAX_PATH_LEN];
                struct stat st;
                if (!join_mount_filename(s_ctx.mount_point, entry->d_name,
                                         entry_path, sizeof(entry_path))) {
                    continue;
                }
                if (stat(entry_path, &st) != 0 || !S_ISREG(st.st_mode)) {
                    continue;
                }
                if (out_filenames != NULL && count < max_files) {
                    strncpy(out_filenames[count], entry->d_name, BB_STORAGE_MAX_FILENAME_LEN - 1U);
                    out_filenames[count][BB_STORAGE_MAX_FILENAME_LEN - 1U] = '\0';
                }
                count++;
                if (out_filenames != NULL && count >= max_files) {
                    break;
                }
            }
        }
        closedir(dir);
    }
#endif

    *out_count = count;
    bb_unlock();
    return ESP_OK;
}

esp_err_t bb_storage_self_test(void)
{
    static const bb_storage_sample_t test_samples[2] = {
        { 1000000ULL, -10, (uint8_t)BB_SENSOR_MPU6050_I2C,   15,  -20,  998, 110, -50, 10 },
        { 1010000ULL,   0, (uint8_t)BB_SENSOR_ADXL345_SPI, 2450, -310, 1020,   0,   0,  0 }
    };

    bb_event_record_t record;
    char tmp_path[BB_STORAGE_MAX_PATH_LEN];
    char final_path[BB_STORAGE_MAX_PATH_LEN];
    esp_err_t err;

    memset(&record, 0, sizeof(record));
    record.format_version       = BB_STORAGE_FORMAT_VERSION;
    record.sample_rate_hz       = 100U;
    record.event_id             = BB_STORAGE_MAX_EVENT_ID;
    record.trigger_timestamp_us = 1010000ULL;
    record.flags                = (uint32_t)BB_EVENT_FLAG_SIMULATED;
    record.pre_event_samples    = 1U;
    record.post_event_samples   = 1U;
    record.sample_count         = 2U;
    record.samples              = test_samples;

    bb_lock();
    if (!s_ctx.initialized) {
        bb_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (!join_mount_filename(s_ctx.mount_point, "BBTEST00.TMP", tmp_path, sizeof(tmp_path)) ||
        !join_mount_filename(s_ctx.mount_point, "BBTEST00.CSV", final_path, sizeof(final_path))) {
        bb_unlock();
        return ESP_ERR_INVALID_SIZE;
    }

    (void)remove(tmp_path);
    (void)remove(final_path);

    err = write_event_to_paths_locked(&record, tmp_path, final_path, false, NULL);
    if (err == ESP_OK) {
        err = verify_event_file_locked(final_path, record.event_id, record.sample_count);
    }

    (void)remove(tmp_path);
    (void)remove(final_path);

    if (err != ESP_OK) {
        s_ctx.stats.last_error = err;
    }

    bb_unlock();
    return err;
}
