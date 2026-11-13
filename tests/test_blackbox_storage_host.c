/*
 * SPDX-FileCopyrightText: 2026 Embedded Event Black Box Team (UNPAD)
 *
 * SPDX-License-Identifier: MIT
 *
 * File:    tests/test_blackbox_storage_host.c
 * Purpose: Host-side unit, fault-injection, CSV corruption, and concurrency
 *          test suite for `blackbox_storage`.
 *
 * Note:    All sensor data in this file is explicitly SYNTHETIC / SIMULATED.
 *          Running this suite on the Windows host validates component logic,
 *          NOT physical ESP32-S3 SPI wiring or microSD electrical behavior.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "blackbox_storage.h"

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  [FAIL] %s:%d: %s (%s)\n", __FILE__, __LINE__, msg, #cond); \
            return 0; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        g_tests_run++; \
        printf("[RUN ] %s\n", #fn); \
        if (fn()) { \
            g_tests_passed++; \
            printf("[PASS] %s\n", #fn); \
        } else { \
            g_tests_failed++; \
            printf("[FAIL] %s\n", #fn); \
        } \
    } while (0)

static const char *TEST_MOUNT_DIR = "./build_test_sdcard";

#ifdef _WIN32
static void remove_dir_recursive(const char *dir_path)
{
    char pattern[260];
    char child[260];
    WIN32_FIND_DATAA fd;
    HANDLE hFind;

    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir_path) >= (int)sizeof(pattern)) {
        return;
    }
    hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        if (snprintf(child, sizeof(child), "%s\\%s", dir_path, fd.cFileName) >= (int)sizeof(child)) {
            continue;
        }
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            remove_dir_recursive(child);
            (void)RemoveDirectoryA(child);
        } else {
            (void)DeleteFileA(child);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}
#endif

static void reset_test_env(void)
{
    bb_storage_test_reset_faults();
    (void)bb_storage_deinit();
    bb_storage_test_reset_faults();
#ifdef _WIN32
    remove_dir_recursive(TEST_MOUNT_DIR);
    (void)RemoveDirectoryA(TEST_MOUNT_DIR);
#endif
}

static bb_storage_config_t make_default_config(void)
{
    bb_storage_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mount_point            = BB_STORAGE_DEFAULT_MOUNT_POINT;
    cfg.host_id                = SDSPI_DEFAULT_HOST;
    cfg.gpio_mosi              = GPIO_NUM_NC;
    cfg.gpio_miso              = GPIO_NUM_NC;
    cfg.gpio_sclk              = GPIO_NUM_NC;
    cfg.gpio_cs                = GPIO_NUM_NC;
    cfg.manage_spi_bus         = true;
    cfg.format_if_mount_failed = false;
    cfg.max_open_files         = 4;
    cfg.max_freq_khz           = SDMMC_FREQ_DEFAULT;
    return cfg;
}

static bb_storage_config_t make_valid_test_config(void)
{
    bb_storage_config_t cfg = make_default_config();
    cfg.mount_point            = TEST_MOUNT_DIR;
    cfg.host_id                = SPI2_HOST;
    cfg.gpio_mosi              = 11;
    cfg.gpio_miso              = 13;
    cfg.gpio_sclk              = 12;
    cfg.gpio_cs                = 10;
    cfg.manage_spi_bus         = true;
    cfg.format_if_mount_failed = false;
    return cfg;
}

static int write_raw_file(const char *path, const char *content, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return 0;
    }
    if (fwrite(content, 1U, len, fp) != len) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

/* =========================================================================
 * Test 1: Configuration, Mount Point, SPI Host, and ESP32-S3 GPIO Validation
 * ========================================================================= */
static int test_config_validation(void)
{
    bb_storage_config_t cfg;

    reset_test_env();

    /* 1. NULL config & unconfigured GPIO_NUM_NC defaults */
    TEST_ASSERT(bb_storage_init(NULL) == ESP_ERR_INVALID_ARG,
                "NULL config must return ESP_ERR_INVALID_ARG");

    cfg = make_default_config();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "Default GPIO_NUM_NC pins must be rejected");

    /* 2. Safety policy: format_if_mount_failed=true forbidden */
    cfg = make_valid_test_config();
    cfg.format_if_mount_failed = true;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "format_if_mount_failed=true must be rejected");

    /* 3. Mount point validation */
    cfg = make_valid_test_config();
    cfg.mount_point = NULL;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "NULL mount_point rejected");
    cfg.mount_point = "";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "empty mount_point rejected");
    cfg.mount_point = "/";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "single '/' rejected");
    cfg.mount_point = "./";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "'./' rejected");
    cfg.mount_point = "sdcard";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "relative mount_point rejected");
    cfg.mount_point = "/sdcard/";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "trailing slash rejected");
    cfg.mount_point = "../sdcard";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "path traversal rejected");
    cfg.mount_point = "/sd\\card";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "backslash rejected");
    cfg.mount_point = "/sd//card";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "double slash rejected");
    cfg.mount_point = "/sd card";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "whitespace rejected");
    cfg.mount_point = "/0123456789012345678901234567890"; /* 32 chars */
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, ">31 char mount_point rejected");

    /* 4. SPI host_id, max_open_files, max_freq_khz bounds */
    cfg = make_valid_test_config();
    cfg.host_id = SPI1_HOST;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "SPI1_HOST (flash) rejected");
    cfg.host_id = SPI_HOST_MAX;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "SPI_HOST_MAX rejected");
    cfg.host_id = (spi_host_device_t)-1;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "negative host_id rejected");

    cfg = make_valid_test_config();
    cfg.max_open_files = 0;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "max_open_files=0 rejected");
    cfg.max_open_files = 17;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "max_open_files=17 rejected");

    cfg = make_valid_test_config();
    cfg.max_freq_khz = 0;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "max_freq_khz=0 rejected");
    cfg.max_freq_khz = 40001;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "max_freq_khz=40001 rejected");

    /* 5. Invalid ESP32-S3 GPIO pins (22..25 reserved/absent, >=49 out of range) */
    cfg = make_valid_test_config();
    cfg.gpio_cs = 22;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "ESP32-S3 non-existent GPIO22 rejected");
    cfg.gpio_cs = 49;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "GPIO49 rejected");
    cfg = make_valid_test_config();
    cfg.gpio_mosi = 25;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "MOSI=25 rejected");

    /* 6. Duplicate SPI pins when manage_spi_bus == true */
    cfg = make_valid_test_config();
    cfg.gpio_mosi = 12;
    cfg.gpio_sclk = 12;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG, "Duplicate SPI pins rejected");

    /* 7. SPI3_HOST is valid */
    cfg = make_valid_test_config();
    cfg.host_id = SPI3_HOST;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "SPI3_HOST init should succeed");
    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "deinit after SPI3_HOST init");

    /* 8. Shared SPI bus mode (manage_spi_bus == false) checks optional pin collision & accepts NC */
    cfg = make_default_config();
    cfg.mount_point    = TEST_MOUNT_DIR;
    cfg.manage_spi_bus = false;
    cfg.gpio_cs        = 10;
    cfg.gpio_mosi      = 10;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "manage_spi_bus=false must reject gpio_cs colliding with gpio_mosi");

    cfg = make_default_config();
    cfg.mount_point    = TEST_MOUNT_DIR;
    cfg.manage_spi_bus = false;
    cfg.gpio_cs        = 10;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK,
                "manage_spi_bus=false with valid gpio_cs and NC bus pins succeeds");
    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "deinit after shared-bus init");

    return 1;
}

/* =========================================================================
 * Test 2: Lifecycle State Machine, Mount/Unmount Cleanup, and Counters
 * ========================================================================= */
static int test_lifecycle_and_mount_failures(void)
{
    bb_storage_config_t cfg = make_valid_test_config();
    bb_storage_test_faults_t faults;
    bb_storage_test_counters_t counters;
    bb_storage_stats_t stats;
    size_t count = 0U;

    reset_test_env();

    /* Calls before init must return ESP_ERR_INVALID_STATE / safe defaults */
    TEST_ASSERT(bb_storage_deinit() == ESP_ERR_INVALID_STATE, "deinit before init");
    TEST_ASSERT(bb_storage_self_test() == ESP_ERR_INVALID_STATE, "self_test before init");
    TEST_ASSERT(bb_storage_list_completed_events(NULL, 0U, &count) == ESP_ERR_INVALID_STATE,
                "list before init");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_UNINITIALIZED,
                "initial status must be UNINITIALIZED");
    TEST_ASSERT(strcmp(bb_storage_status_to_str((bb_storage_status_t)99), "UNKNOWN") == 0,
                "unknown status string");
    TEST_ASSERT(bb_storage_get_stats(NULL) == ESP_ERR_INVALID_ARG, "get_stats(NULL)");
    TEST_ASSERT(bb_storage_get_stats(&stats) == ESP_OK, "get_stats uninitialized succeeds");
    TEST_ASSERT(stats.total_bytes == 0ULL && stats.free_bytes == 0ULL,
                "uninitialized capacity must be zero");

    /* 1. Simulate SPI bus init failure -> mount never called, spi_bus_free never called */
    memset(&faults, 0, sizeof(faults));
    faults.fail_spi_bus = true;
    bb_storage_test_set_faults(&faults);
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_FAIL, "SPI bus failure returns ESP_FAIL");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_MOUNT_FAILED,
                "status MOUNT_FAILED after spi_bus failure");
    bb_storage_test_get_counters(&counters);
    TEST_ASSERT(counters.spi_bus_init_calls == 1U && counters.mount_calls == 0U &&
                counters.spi_bus_free_calls == 0U,
                "SPI bus init failure must not call mount or spi_bus_free");

    /* 2. Simulate mount failure with manage_spi_bus=true -> frees SPI bus it created */
    bb_storage_test_reset_faults();
    memset(&faults, 0, sizeof(faults));
    faults.fail_mount = true;
    bb_storage_test_set_faults(&faults);
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_FAIL, "mount failure returns ESP_FAIL");
    bb_storage_test_get_counters(&counters);
    TEST_ASSERT(counters.spi_bus_init_calls == 1U && counters.mount_calls == 1U &&
                counters.spi_bus_free_calls == 1U,
                "owned SPI bus must be freed if mount fails");

    /* 3. Simulate mount failure with manage_spi_bus=false -> must NOT touch external SPI bus */
    bb_storage_test_reset_faults();
    memset(&faults, 0, sizeof(faults));
    faults.fail_mount = true;
    bb_storage_test_set_faults(&faults);
    cfg.manage_spi_bus = false;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_FAIL, "shared mount failure returns ESP_FAIL");
    bb_storage_test_get_counters(&counters);
    TEST_ASSERT(counters.spi_bus_init_calls == 0U && counters.mount_calls == 1U &&
                counters.spi_bus_free_calls == 0U,
                "shared SPI bus must never be initialized or freed");
    cfg.manage_spi_bus = true;

    /* 4. Normal mount & capacity stats */
    bb_storage_test_reset_faults();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "normal init succeeds");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_READY, "status is READY");
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_STATE, "double init rejected");
    TEST_ASSERT(bb_storage_get_stats(&stats) == ESP_OK && stats.total_bytes > 0ULL,
                "mounted stats carry simulated card capacity");

    /* 5. Unmount failure preserves ownership & initialized state so retry succeeds cleanly */
    memset(&faults, 0, sizeof(faults));
    faults.fail_unmount = true;
    faults.mock_total_bytes = 16ULL * 1024ULL * 1024ULL;
    faults.mock_free_bytes  = 12ULL * 1024ULL * 1024ULL;
    bb_storage_test_set_faults(&faults);
    TEST_ASSERT(bb_storage_deinit() == ESP_FAIL, "unmount failure propagates ESP_FAIL");
    bb_storage_test_get_counters(&counters);
    TEST_ASSERT(counters.unmount_calls == 1U && counters.spi_bus_free_calls == 0U,
                "spi_bus_free must NOT be called when unmount fails");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_READY,
                "storage remains mounted/initialized after unmount failure");

    /* Clear fault and retry deinit -> frees owned SPI bus & resets capacity stats */
    faults.fail_unmount = false;
    bb_storage_test_set_faults(&faults);
    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "retry deinit succeeds");
    bb_storage_test_get_counters(&counters);
    TEST_ASSERT(counters.spi_bus_free_calls == 1U, "retry deinit frees owned SPI bus");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_UNINITIALIZED,
                "status is UNINITIALIZED after deinit");
    TEST_ASSERT(bb_storage_get_stats(&stats) == ESP_OK &&
                stats.total_bytes == 0ULL && stats.free_bytes == 0ULL,
                "deinit clears stale capacity bytes");

    /* 6. spi_bus_free failure after successful unmount transitions to uninitialized */
    bb_storage_test_reset_faults();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "re-init succeeds");
    memset(&faults, 0, sizeof(faults));
    faults.fail_spi_bus_free = true;
    bb_storage_test_set_faults(&faults);
    TEST_ASSERT(bb_storage_deinit() == ESP_FAIL, "spi_bus_free failure returns ESP_FAIL");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_UNINITIALIZED,
                "status becomes UNINITIALIZED after unmount succeeded");
    TEST_ASSERT(bb_storage_deinit() == ESP_ERR_INVALID_STATE,
                "second deinit returns INVALID_STATE");

    return 1;
}

/* =========================================================================
 * Test 3: Event Input Validation & Overflow Protection
 * ========================================================================= */
static int test_event_input_validation(void)
{
    static const bb_storage_sample_t sample = {
        1000000ULL, -10, (uint8_t)BB_SENSOR_MPU6050_I2C, 12, -45, 980, 120, -40, 15
    };
    bb_event_record_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.format_version = BB_STORAGE_FORMAT_VERSION;
    rec.sample_rate_hz = 100U;
    rec.event_id       = 1UL;
    rec.sample_count   = 1U;
    rec.samples        = &sample;

    TEST_ASSERT(bb_storage_validate_event(NULL) == ESP_ERR_INVALID_ARG, "NULL event rejected");
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_OK, "valid event passes");

    rec.format_version = 99U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG, "bad version rejected");
    rec.format_version = BB_STORAGE_FORMAT_VERSION;

    rec.event_id = 0UL;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG, "event_id=0 rejected");
    rec.event_id = BB_STORAGE_MAX_EVENT_ID + 1UL;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG, "event_id>MAX rejected");
    rec.event_id = BB_STORAGE_MAX_EVENT_ID;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_OK, "event_id==MAX passes");
    rec.event_id = 1UL;

    rec.sample_count = 0U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG, "sample_count=0 rejected");
    rec.sample_count = BB_STORAGE_MAX_SAMPLES + 1U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_SIZE, "sample_count>MAX rejected");
    rec.sample_count = 1U;

    rec.samples = NULL;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG, "NULL samples rejected");
    rec.samples = &sample;

    rec.pre_event_samples  = 1U;
    rec.post_event_samples = 1U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "pre+post > sample_count rejected");

    rec.pre_event_samples  = 1U;
    rec.post_event_samples = 0U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_OK, "pre+post == sample_count passes");

    /* Integer overflow guard: SIZE_MAX + 1 would wrap without individual checks */
    rec.pre_event_samples  = (size_t)-1;
    rec.post_event_samples = 1U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "size_t wraparound in pre+post must be rejected");

    return 1;
}

/* =========================================================================
 * Test 4: 8.3 Path Construction & Completed Filename Predicate
 * ========================================================================= */
static int test_paths_and_filename_predicate(void)
{
    char tmp_path[BB_STORAGE_MAX_PATH_LEN];
    char final_path[BB_STORAGE_MAX_PATH_LEN];

    TEST_ASSERT(bb_storage_build_event_paths("/sdcard", 1UL,
                                             tmp_path, sizeof(tmp_path),
                                             final_path, sizeof(final_path)) == ESP_OK,
                "build_event_paths event_id=1");
    TEST_ASSERT(strcmp(tmp_path, "/sdcard/E0000001.TMP") == 0, "E0000001.TMP");
    TEST_ASSERT(strcmp(final_path, "/sdcard/E0000001.CSV") == 0, "E0000001.CSV");

    TEST_ASSERT(bb_storage_build_event_paths("/sdcard", BB_STORAGE_MAX_EVENT_ID,
                                             NULL, 0U,
                                             final_path, sizeof(final_path)) == ESP_OK,
                "NULL tmp_path allowed");
    TEST_ASSERT(strcmp(final_path, "/sdcard/EFFFFFFF.CSV") == 0, "EFFFFFFF.CSV");

    TEST_ASSERT(bb_storage_build_event_paths("/sdcard", 1UL,
                                             tmp_path, 20U,
                                             final_path, 21U) == ESP_ERR_INVALID_SIZE,
                "exact boundary: tmp_size=20 (<21) rejected");
    TEST_ASSERT(bb_storage_build_event_paths("/sdcard", 1UL,
                                             tmp_path, 21U,
                                             final_path, 20U) == ESP_ERR_INVALID_SIZE,
                "exact boundary: final_size=20 (<21) rejected");
    TEST_ASSERT(bb_storage_build_event_paths("/sdcard", 1UL,
                                             tmp_path, 21U,
                                             final_path, 21U) == ESP_OK,
                "exact boundary 21 succeeds");

    TEST_ASSERT(bb_storage_build_event_paths("/sdcard", 0UL, tmp_path, sizeof(tmp_path), NULL, 0U) == ESP_ERR_INVALID_ARG,
                "event_id=0 rejected");
    TEST_ASSERT(bb_storage_build_event_paths("../sdcard", 1UL, tmp_path, sizeof(tmp_path), NULL, 0U) == ESP_ERR_INVALID_ARG,
                "traversal mount rejected");

    /* Completed event filename filter */
    TEST_ASSERT(bb_storage_is_completed_event_filename("E0000001.CSV") == true, "E0000001.CSV valid");
    TEST_ASSERT(bb_storage_is_completed_event_filename("EFFFFFFF.CSV") == true, "EFFFFFFF.CSV valid");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E0000001.TMP") == false, ".TMP rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E0000000.CSV") == false, "all zero ID rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("e0000001.csv") == false, "lowercase rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E0000001.csv") == false, "lowercase ext rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("../E0000001.CSV") == false, "traversal rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("/E0000001.CSV") == false, "slash rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("BBTEST00.CSV") == false, "BBTEST00 excluded");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E000001.CSV") == false, "short hex rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E00000001.CSV") == false, "long hex rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E000000G.CSV") == false, "non-hex rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename(NULL) == false, "NULL rejected");

    return 1;
}

/* =========================================================================
 * Test 5: Synthetic Event CSV Serialization, Golden Vector, & CRC-32 Determinism
 * ========================================================================= */
static int test_csv_serialization_and_crc32(void)
{
    static const bb_storage_sample_t samples[4] = {
        { 5000000ULL, -20, (uint8_t)BB_SENSOR_MPU6050_I2C,   10,  -15,  998,  100, -30,  10 },
        { 5010000ULL, -10, (uint8_t)BB_SENSOR_ADXL345_SPI,   18,  -22, 1004,    0,   0,   0 },
        { 5020000ULL,   0, (uint8_t)BB_SENSOR_MPU6050_I2C, 2940, -510, 1250, 1450, 320, -90 },
        { 5030000ULL,  10, (uint8_t)BB_SENSOR_ADXL345_SPI,  410,  -95, 1015,    0,   0,   0 }
    };
    bb_storage_sample_t modified_samples[4];
    bb_event_record_t event;
    char csv_buf[1024];
    size_t csv_len = 9999U;
    uint32_t crc1, crc2;

    TEST_ASSERT(bb_storage_compute_samples_crc32(NULL, 4U) == 0UL, "NULL CRC32 -> 0");
    TEST_ASSERT(bb_storage_compute_samples_crc32(samples, 0U) == 0UL, "0 count CRC32 -> 0");

    memset(&event, 0, sizeof(event));
    event.format_version       = BB_STORAGE_FORMAT_VERSION;
    event.sample_rate_hz       = 100U;
    event.event_id             = 1UL;
    event.trigger_timestamp_us = 5020000ULL;
    event.flags                = (uint32_t)(BB_EVENT_FLAG_TRIG_I2C | BB_EVENT_FLAG_TRIG_SPI |
                                            BB_EVENT_FLAG_CROSS_CONFIRMED | BB_EVENT_FLAG_SIMULATED);
    event.pre_event_samples    = 2U;
    event.post_event_samples   = 1U;
    event.sample_count         = 4U;
    event.samples              = samples;

    /* Buffer too small check zeroes *out_len */
    TEST_ASSERT(bb_storage_format_event_csv(&event, csv_buf, 64U, &csv_len) == ESP_ERR_INVALID_SIZE,
                "small buffer must return ESP_ERR_INVALID_SIZE");
    TEST_ASSERT(csv_len == 0U, "out_len must be cleared to 0 on error");

    /* Full serialization & deterministic golden CRC32/length verification */
    TEST_ASSERT(bb_storage_format_event_csv(&event, csv_buf, sizeof(csv_buf), &csv_len) == ESP_OK,
                "format_event_csv succeeds");
    crc1 = bb_storage_compute_samples_crc32(samples, 4U);
    TEST_ASSERT(crc1 == 0x10BF5ABEUL, "golden 4-sample CRC-32 must equal 0x10BF5ABE");
    TEST_ASSERT(csv_len == 441U && csv_len == strlen(csv_buf),
                "golden 4-sample CSV length must equal 441 bytes");
    TEST_ASSERT(strstr(csv_buf, "# END_EVENT,count=4,crc32=0x10BF5ABE\n") != NULL,
                "CSV must contain expected END_EVENT CRC footer");

    /* Verify CRC32 sensitivity to 1-LSB sample mutation */
    memcpy(modified_samples, samples, sizeof(samples));
    modified_samples[2].accel_x_mg += 1;
    crc2 = bb_storage_compute_samples_crc32(modified_samples, 4U);
    TEST_ASSERT(crc1 != crc2, "CRC32 must change when sample data changes");

    return 1;
}

/* =========================================================================
 * Test 6: Persistence, Extreme Boundaries, Large Events & Strict CSV Verification
 * ========================================================================= */
static int test_write_verify_and_corruption_detection(void)
{
    static const bb_storage_sample_t boundary_samples[3] = {
        { 0ULL,                    (int32_t)(-2147483647LL - 1LL), (uint8_t)0U,   (int32_t)(-2147483647LL - 1LL), (int32_t)(-2147483647LL - 1LL), (int32_t)(-2147483647LL - 1LL), (int32_t)(-2147483647LL - 1LL), (int32_t)(-2147483647LL - 1LL), (int32_t)(-2147483647LL - 1LL) },
        { 900000000000000000ULL,                                0, (uint8_t)127U,                           3100,                           -600,                           1180,                              0,                              0,                              0 },
        { 18446744073709551615ULL,                    2147483647L, (uint8_t)255U,                    2147483647L,                    2147483647L,                    2147483647L,                    2147483647L,                    2147483647L,                    2147483647L }
    };
    bb_event_record_t event;
    bb_storage_config_t cfg = make_valid_test_config();
    bb_storage_stats_t stats;
    char final_path[BB_STORAGE_MAX_PATH_LEN];
    char small_path[12];
    char file_buf[2048];
    char bad_buf[2048];
    FILE *fp;
    size_t nread;
    char *pos;
    bb_storage_sample_t *large_samples;
    size_t i;

    reset_test_env();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "init succeeds");

    /* Verify argument checks on verify_event_file & out_final_path clearing */
    TEST_ASSERT(bb_storage_verify_event_file(NULL, 0UL, 0U) == ESP_ERR_INVALID_ARG, "NULL path");
    TEST_ASSERT(bb_storage_verify_event_file("", 0UL, 0U) == ESP_ERR_INVALID_ARG, "empty path");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000001.CSV",
                                             BB_STORAGE_MAX_EVENT_ID + 1UL, 0U) == ESP_ERR_INVALID_ARG,
                "invalid expected_event_id rejected");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000001.CSV",
                                             0UL, BB_STORAGE_MAX_SAMPLES + 1U) == ESP_ERR_INVALID_ARG,
                "invalid expected_sample_count rejected");

    memset(&event, 0, sizeof(event));
    event.format_version       = BB_STORAGE_FORMAT_VERSION;
    event.sample_rate_hz       = 100U;
    event.event_id             = 1UL;
    event.trigger_timestamp_us = 18446744073709551615ULL;
    event.flags                = 0xCAFEBABEUL;
    event.pre_event_samples    = 1U;
    event.post_event_samples   = 1U;
    event.sample_count         = 3U;
    event.samples              = boundary_samples;

    strcpy(small_path, "OLD");
    TEST_ASSERT(bb_storage_write_event(&event, small_path, sizeof(small_path)) == ESP_ERR_INVALID_SIZE,
                "too-small out_final_path rejected");
    TEST_ASSERT(small_path[0] == '\0', "out_final_path cleared on error");

    /* 1. Write boundary event #1 (INT32_MIN/INT32_MAX/UINT64_MAX) */
    strcpy(final_path, "STALE");
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_OK,
                "write_event boundary #1 succeeds");
    TEST_ASSERT(strcmp(final_path, "./build_test_sdcard/E0000001.CSV") == 0,
                "final_path matches E0000001.CSV");
    TEST_ASSERT(bb_storage_verify_event_file(final_path, 1UL, 3U) == ESP_OK,
                "boundary values survive CSV roundtrip and CRC check");

    /* Duplicate write refused without clobbering existing file & clears out_final_path */
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_ERR_INVALID_STATE,
                "duplicate event_id rejected");
    TEST_ASSERT(final_path[0] == '\0', "out_final_path cleared on duplicate error");

    /* 2. Large event test (2048 samples = ~20s @ 100Hz) */
    large_samples = (bb_storage_sample_t *)malloc(2048U * sizeof(bb_storage_sample_t));
    TEST_ASSERT(large_samples != NULL, "allocate 2048 samples");
    for (i = 0U; i < 2048U; i++) {
        large_samples[i].timestamp_us = 1000000ULL + ((uint64_t)i * 10000ULL);
        large_samples[i].offset_ms    = (int32_t)i - 1024;
        large_samples[i].sensor_id    = (uint8_t)((i % 2U) + 1U);
        large_samples[i].accel_x_mg   = (int32_t)(i * 3U) - 1000;
        large_samples[i].accel_y_mg   = -(int32_t)(i % 500U);
        large_samples[i].accel_z_mg   = 1000;
        large_samples[i].gyro_x_mdps  = (int32_t)(i * 7U);
        large_samples[i].gyro_y_mdps  = -50;
        large_samples[i].gyro_z_mdps  = 25;
    }
    event.event_id     = 42UL;
    event.sample_count = 2048U;
    event.pre_event_samples  = 1024U;
    event.post_event_samples = 1023U;
    event.samples      = large_samples;
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_OK,
                "2048-sample large event write succeeds");
    TEST_ASSERT(bb_storage_verify_event_file(final_path, 42UL, 2048U) == ESP_OK,
                "2048-sample large event verify succeeds");
    free(large_samples);

    TEST_ASSERT(bb_storage_get_stats(&stats) == ESP_OK, "get_stats");
    TEST_ASSERT(stats.events_written == 2UL && stats.last_event_id == 42UL &&
                stats.last_write_bytes > 50000U,
                "stats updated cleanly for 2 events");

    /* Read back boundary E0000001.CSV to generate strict corruption variants */
    fp = fopen("./build_test_sdcard/E0000001.CSV", "rb");
    TEST_ASSERT(fp != NULL, "open E0000001.CSV");
    nread = fread(file_buf, 1U, sizeof(file_buf) - 1U, fp);
    fclose(fp);
    file_buf[nread] = '\0';

    /* Expected mismatch */
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000001.CSV", 99UL, 3U) == ESP_FAIL,
                "wrong expected_event_id fails");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000001.CSV", 1UL, 2U) == ESP_FAIL,
                "wrong expected_sample_count fails");

    /* 3a. 1-char CRC mutation */
    memcpy(bad_buf, file_buf, nread + 1U);
    pos = strstr(bad_buf, "3100");
    TEST_ASSERT(pos != NULL, "locate 3100");
    pos[0] = '4';
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BAD_CRC.CSV", bad_buf, nread), "write BAD_CRC");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BAD_CRC.CSV", 1UL, 3U) == ESP_ERR_INVALID_CRC,
                "CRC corruption detected");

    /* 3b. Truncated missing footer */
    memcpy(bad_buf, file_buf, nread + 1U);
    pos = strstr(bad_buf, "# END_EVENT");
    TEST_ASSERT(pos != NULL, "locate footer");
    *pos = '\0';
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BAD_TRUNC.CSV", bad_buf, strlen(bad_buf)), "write TRUNC");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BAD_TRUNC.CSV", 1UL, 3U) == ESP_FAIL,
                "truncated file rejected");

    /* 3c. Trailing junk on footer / header / row */
    memcpy(bad_buf, file_buf, nread + 1U);
    pos = strstr(bad_buf, "# BLACKBOX_EVENT_FORMAT,v1");
    TEST_ASSERT(pos != NULL && pos[26] == '\n', "locate v1");
    memmove(pos + 29, pos + 26, strlen(pos + 26) + 1U);
    pos[26] = 'a'; pos[27] = 'b'; pos[28] = 'c';
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BAD_HDR_JUNK.CSV", bad_buf, strlen(bad_buf)), "write HDR_JUNK");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BAD_HDR_JUNK.CSV", 1UL, 3U) == ESP_FAIL,
                "header trailing junk rejected");

    memcpy(bad_buf, file_buf, nread + 1U);
    pos = strstr(bad_buf, ",1180,0,0,0\n");
    TEST_ASSERT(pos != NULL, "locate row1 end");
    memmove(pos + 17, pos + 11, strlen(pos + 11) + 1U);
    memcpy(pos + 11, ",extra", 6U);
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BAD_ROW_EXTRA.CSV", bad_buf, strlen(bad_buf)), "write ROW_EXTRA");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BAD_ROW_EXTRA.CSV", 1UL, 3U) == ESP_FAIL,
                "row with 11th field rejected");

    /* 3d. Trailing data after # END_EVENT */
    TEST_ASSERT(nread + 14U < sizeof(bad_buf), "bad_buf capacity");
    memcpy(bad_buf, file_buf, nread);
    memcpy(bad_buf + nread, "# EXTRA_LINE\n", 14U);
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BAD_POST_FOOTER.CSV", bad_buf, nread + 13U), "write POST_FOOTER");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BAD_POST_FOOTER.CSV", 1UL, 3U) == ESP_FAIL,
                "post-footer content rejected");

    /* 3e. Integer overflow / out-of-range sample fields */
    memcpy(bad_buf, file_buf, nread + 1U);
    pos = strstr(bad_buf, ",3100,");
    TEST_ASSERT(pos != NULL, "locate ,3100,");
    memmove(pos + 11, pos + 5, strlen(pos + 5) + 1U);
    memcpy(pos + 1, "2147483648", 10U); /* INT32_MAX + 1 */
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BAD_OVERFLOW.CSV", bad_buf, strlen(bad_buf)), "write OVERFLOW");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BAD_OVERFLOW.CSV", 1UL, 3U) == ESP_FAIL,
                "INT32_MAX+1 in CSV sample rejected");

    /* 3f. Overlong line (>384 bytes) */
    memset(bad_buf, 'A', 420U);
    bad_buf[0] = '#';
    bad_buf[1] = ' ';
    bad_buf[420] = '\n';
    bad_buf[421] = '\0';
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BAD_LONGLINE.CSV", bad_buf, 421U), "write LONGLINE");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BAD_LONGLINE.CSV", 0UL, 0U) == ESP_FAIL,
                "overlong line rejected");

    /* 3g. CRLF line endings must be accepted when data is otherwise valid */
    {
        size_t j = 0U;
        for (i = 0U; i < nread && j + 2U < sizeof(bad_buf); i++) {
            if (file_buf[i] == '\n') {
                bad_buf[j++] = '\r';
            }
            bad_buf[j++] = file_buf[i];
        }
        bad_buf[j] = '\0';
        TEST_ASSERT(write_raw_file("./build_test_sdcard/GOOD_CRLF.CSV", bad_buf, j), "write CRLF");
        TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/GOOD_CRLF.CSV", 1UL, 3U) == ESP_OK,
                    "valid CRLF file passes verification");
    }

    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "deinit succeeds");
    return 1;
}

/* =========================================================================
 * Test 7: Comprehensive Write Fault Injection, Recovery, Listing & Self-Test
 * ========================================================================= */
static int test_fault_injection_recovery_and_listing(void)
{
    static const bb_storage_sample_t samples[3] = {
        { 3000000ULL, -10, (uint8_t)BB_SENSOR_MPU6050_I2C,   10,  -20, 1000,  20, -10,  5 },
        { 3010000ULL,   0, (uint8_t)BB_SENSOR_ADXL345_SPI, 2100, -100,  990,   0,   0,  0 },
        { 3020000ULL,  10, (uint8_t)BB_SENSOR_MPU6050_I2C,  300,  -40, 1005,  50, -25, 10 }
    };
    bb_event_record_t event;
    bb_storage_config_t cfg = make_valid_test_config();
    bb_storage_test_faults_t faults;
    bb_storage_stats_t stats_before, stats_after;
    char listed[8][BB_STORAGE_MAX_FILENAME_LEN];
    char final_path[BB_STORAGE_MAX_PATH_LEN];
    size_t count = 0U;
    size_t i;

    reset_test_env();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "init succeeds");

    memset(&event, 0, sizeof(event));
    event.format_version       = BB_STORAGE_FORMAT_VERSION;
    event.sample_rate_hz       = 100U;
    event.event_id             = 0x77UL;
    event.trigger_timestamp_us = 3010000ULL;
    event.flags                = (uint32_t)BB_EVENT_FLAG_SIMULATED;
    event.pre_event_samples    = 1U;
    event.post_event_samples   = 1U;
    event.sample_count         = 3U;
    event.samples              = samples;

    /* Helper table-driven check across every single-point write fault */
    for (i = 0U; i < 11U; i++) {
        bb_storage_test_reset_faults();
        memset(&faults, 0, sizeof(faults));
        faults.mock_total_bytes = 16ULL * 1024ULL * 1024ULL;
        faults.mock_free_bytes  = 12ULL * 1024ULL * 1024ULL;

        esp_err_t expected_err = ESP_FAIL;
        bb_storage_status_t expected_status = BB_STORAGE_STATUS_WRITE_ERROR;

        switch (i) {
            case 0: faults.fail_fopen = true; break;
            case 1: faults.fail_header_write = true; break;
            case 2: faults.fail_write_enospc = true; expected_err = ESP_ERR_NO_MEM; expected_status = BB_STORAGE_STATUS_FULL; break;
            case 3: faults.fail_write_enospc = true; faults.fail_after_samples = 2U; expected_err = ESP_ERR_NO_MEM; expected_status = BB_STORAGE_STATUS_FULL; break;
            case 4: faults.fail_write_io = true; faults.fail_after_samples = 1U; break;
            case 5: faults.fail_footer_write = true; break;
            case 6: faults.fail_fflush = true; break;
            case 7: faults.fail_fsync = true; break;
            case 8: faults.fail_fclose = true; break;
            case 9: faults.fail_rename = true; break;
            case 10: faults.fail_rename_enospc = true; expected_err = ESP_ERR_NO_MEM; expected_status = BB_STORAGE_STATUS_FULL; break;
        }
        bb_storage_test_set_faults(&faults);

        strcpy(final_path, "NONEMPTY");
        TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == expected_err,
                    "fault injection returns expected error");
        TEST_ASSERT(final_path[0] == '\0', "out_final_path cleared on write fault");
        TEST_ASSERT(bb_storage_get_status() == expected_status,
                    "status reflects FULL vs WRITE_ERROR");
        TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000077.TMP", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                    "partial .TMP removed on failure");
        TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000077.CSV", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                    "final .CSV absent on failure");
    }

    /* Stale leftover .TMP must be replaced cleanly and status recovers to READY */
    bb_storage_test_reset_faults();
    TEST_ASSERT(write_raw_file("./build_test_sdcard/E0000077.TMP", "GARBAGE", 7U),
                "create stale .TMP");
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_OK,
                "recovery write_event succeeds after prior faults");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_READY,
                "status recovers to READY after successful write");
    TEST_ASSERT(bb_storage_verify_event_file(final_path, 0x77UL, 3U) == ESP_OK,
                "recovered event verifies cleanly");

    /* Write second real event (EFFFFFFF.CSV = max ID) to ensure no collision with self_test */
    event.event_id = BB_STORAGE_MAX_EVENT_ID;
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_OK,
                "write max event ID succeeds");

    /* Noise files and subdirectory mimicking E*.CSV must be filtered out */
    TEST_ASSERT(write_raw_file("./build_test_sdcard/E0000099.TMP", "INCOMPLETE", 10U),
                "create noise .TMP");
    TEST_ASSERT(write_raw_file("./build_test_sdcard/BBTEST00.CSV", "SELFTEST", 8U),
                "create noise BBTEST00.CSV");
#ifdef _WIN32
    TEST_ASSERT(CreateDirectoryA("./build_test_sdcard/E00000BB.CSV", NULL) != 0,
                "create decoy directory E00000BB.CSV");
#endif

    TEST_ASSERT(bb_storage_list_completed_events(NULL, 0U, &count) == ESP_OK,
                "count-only list_completed_events succeeds");
    TEST_ASSERT(count == 2U, "count-only must see exactly 2 real completed CSVs");

    memset(listed, 0, sizeof(listed));
    TEST_ASSERT(bb_storage_list_completed_events(listed, 8U, &count) == ESP_OK,
                "list_completed_events succeeds");
    TEST_ASSERT(count == 2U, "array list must see exactly 2 real completed CSVs");
    for (i = 0U; i < count; i++) {
        TEST_ASSERT(bb_storage_is_completed_event_filename(listed[i]),
                    "listed filename satisfies predicate");
        TEST_ASSERT(strcmp(listed[i], "E00000BB.CSV") != 0,
                    "decoy directory E00000BB.CSV must not be listed");
    }

    /* Built-in self-test preserves stats and EFFFFFFF.CSV */
    TEST_ASSERT(bb_storage_get_stats(&stats_before) == ESP_OK, "stats before self_test");
    TEST_ASSERT(bb_storage_self_test() == ESP_OK, "bb_storage_self_test succeeds");
    TEST_ASSERT(bb_storage_get_stats(&stats_after) == ESP_OK, "stats after self_test");
    TEST_ASSERT(stats_before.events_written == stats_after.events_written &&
                stats_before.write_failures == stats_after.write_failures &&
                stats_before.last_event_id == stats_after.last_event_id &&
                stats_before.last_write_bytes == stats_after.last_write_bytes,
                "self_test must leave operational stats unchanged");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/EFFFFFFF.CSV",
                                             BB_STORAGE_MAX_EVENT_ID, 3U) == ESP_OK,
                "self_test must NOT delete real EFFFFFFF.CSV");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/BBTEST00.TMP", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                "self_test removes BBTEST00.TMP");

    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "deinit succeeds");
    return 1;
}

#ifdef _WIN32
/* =========================================================================
 * Test 8: Multi-Threaded Concurrency & Thread-Safety Stress Test
 * ========================================================================= */
typedef struct {
    uint32_t base_event_id;
    uint32_t count;
    int      failures;
} writer_thread_args_t;

typedef struct {
    volatile LONG stop_flag;
    int           failures;
} reader_thread_args_t;

static DWORD WINAPI writer_thread_fn(LPVOID param)
{
    static const bb_storage_sample_t s = {
        4000000ULL, 0, (uint8_t)BB_SENSOR_MPU6050_I2C, 2500, -120, 1005, 80, -20, 5
    };
    writer_thread_args_t *args = (writer_thread_args_t *)param;
    bb_event_record_t ev;
    uint32_t k;

    memset(&ev, 0, sizeof(ev));
    ev.format_version       = BB_STORAGE_FORMAT_VERSION;
    ev.sample_rate_hz       = 100U;
    ev.trigger_timestamp_us = 4000000ULL;
    ev.flags                = (uint32_t)BB_EVENT_FLAG_SIMULATED;
    ev.sample_count         = 1U;
    ev.samples              = &s;

    for (k = 0U; k < args->count; k++) {
        ev.event_id = args->base_event_id + k;
        if (bb_storage_write_event(&ev, NULL, 0U) != ESP_OK) {
            args->failures++;
        }
    }
    return 0;
}

static DWORD WINAPI reader_thread_fn(LPVOID param)
{
    reader_thread_args_t *args = (reader_thread_args_t *)param;
    bb_storage_stats_t stats;
    size_t cnt = 0U;

    while (!args->stop_flag) {
        bb_storage_status_t st = bb_storage_get_status();
        if (st != BB_STORAGE_STATUS_READY) {
            args->failures++;
        }
        if (bb_storage_get_stats(&stats) != ESP_OK) {
            args->failures++;
        }
        if (bb_storage_list_completed_events(NULL, 0U, &cnt) != ESP_OK) {
            args->failures++;
        }
        Sleep(1);
    }
    return 0;
}

static DWORD WINAPI selftest_thread_fn(LPVOID param)
{
    int *failures = (int *)param;
    int k;
    for (k = 0; k < 8; k++) {
        if (bb_storage_self_test() != ESP_OK) {
            (*failures)++;
        }
    }
    return 0;
}

static int test_concurrent_multithreaded_access(void)
{
    bb_storage_config_t cfg = make_valid_test_config();
    bb_storage_stats_t stats;
    writer_thread_args_t w1 = { 0x100UL, 16U, 0 };
    writer_thread_args_t w2 = { 0x200UL, 16U, 0 };
    reader_thread_args_t rd = { 0, 0 };
    int st_failures = 0;
    HANDLE threads[4];
    size_t count = 0U;
    uint32_t k;

    reset_test_env();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "init for concurrency test");

    threads[0] = CreateThread(NULL, 0, writer_thread_fn, &w1, 0, NULL);
    threads[1] = CreateThread(NULL, 0, writer_thread_fn, &w2, 0, NULL);
    threads[2] = CreateThread(NULL, 0, selftest_thread_fn, &st_failures, 0, NULL);
    threads[3] = CreateThread(NULL, 0, reader_thread_fn, &rd, 0, NULL);
    TEST_ASSERT(threads[0] != NULL && threads[1] != NULL &&
                threads[2] != NULL && threads[3] != NULL,
                "all 4 worker threads created");

    WaitForMultipleObjects(3, threads, TRUE, 30000);
    rd.stop_flag = 1;
    WaitForSingleObject(threads[3], 5000);

    for (k = 0U; k < 4U; k++) {
        CloseHandle(threads[k]);
    }

    TEST_ASSERT(w1.failures == 0 && w2.failures == 0, "writer threads had zero failures");
    TEST_ASSERT(st_failures == 0, "self_test thread had zero failures");
    TEST_ASSERT(rd.failures == 0, "reader/status polling thread had zero failures");

    TEST_ASSERT(bb_storage_list_completed_events(NULL, 0U, &count) == ESP_OK && count == 32U,
                "all 32 concurrent events must appear cleanly");
    TEST_ASSERT(bb_storage_get_stats(&stats) == ESP_OK &&
                stats.events_written == 32UL && stats.write_failures == 0UL,
                "stats must show exactly 32 events and 0 failures");

    /* Verify every single concurrent file is valid on disk */
    for (k = 0U; k < 16U; k++) {
        char path1[BB_STORAGE_MAX_PATH_LEN];
        char path2[BB_STORAGE_MAX_PATH_LEN];
        TEST_ASSERT(bb_storage_build_event_paths(TEST_MOUNT_DIR, 0x100UL + k,
                                                 NULL, 0U, path1, sizeof(path1)) == ESP_OK, "path1");
        TEST_ASSERT(bb_storage_build_event_paths(TEST_MOUNT_DIR, 0x200UL + k,
                                                 NULL, 0U, path2, sizeof(path2)) == ESP_OK, "path2");
        TEST_ASSERT(bb_storage_verify_event_file(path1, 0x100UL + k, 1U) == ESP_OK, "verify thread 1 file");
        TEST_ASSERT(bb_storage_verify_event_file(path2, 0x200UL + k, 1U) == ESP_OK, "verify thread 2 file");
    }

    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "deinit succeeds");
    return 1;
}
#endif /* _WIN32 */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("============================================================\n");
    printf(" blackbox_storage Host Unit, Fault & Concurrency Test Suite\n");
    printf(" (NOTE: Uses SYNTHETIC/SIMULATED data on host filesystem)\n");
    printf("============================================================\n");

    RUN_TEST(test_config_validation);
    RUN_TEST(test_lifecycle_and_mount_failures);
    RUN_TEST(test_event_input_validation);
    RUN_TEST(test_paths_and_filename_predicate);
    RUN_TEST(test_csv_serialization_and_crc32);
    RUN_TEST(test_write_verify_and_corruption_detection);
    RUN_TEST(test_fault_injection_recovery_and_listing);
#ifdef _WIN32
    RUN_TEST(test_concurrent_multithreaded_access);
#endif

    reset_test_env();

    printf("------------------------------------------------------------\n");
    printf(" Summary: %d/%d passed, %d failed\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    printf("============================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
