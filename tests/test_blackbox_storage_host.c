/*
 * SPDX-FileCopyrightText: 2026 Embedded Event Black Box Team (UNPAD)
 *
 * SPDX-License-Identifier: MIT
 *
 * File:    tests/test_blackbox_storage_host.c
 * Purpose: Host-side unit and fault-injection test suite for `blackbox_storage`.
 *          Exercises configuration validation, lifecycle state transitions,
 *          synthetic event CSV serialization, CRC-32 integrity checks,
 *          two-phase (.TMP -> .CSV) persistence, and error handling.
 *
 * Note:    All sensor data in this file is explicitly SYNTHETIC / SIMULATED.
 *          Running this suite on the Windows host validates component logic,
 *          NOT physical ESP32-S3 SPI wiring or microSD electrical behavior.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void cleanup_test_files(void)
{
    (void)remove("./build_test_sdcard/E0000001.TMP");
    (void)remove("./build_test_sdcard/E0000001.CSV");
    (void)remove("./build_test_sdcard/E000002A.TMP");
    (void)remove("./build_test_sdcard/E000002A.CSV");
    (void)remove("./build_test_sdcard/E0000077.TMP");
    (void)remove("./build_test_sdcard/E0000077.CSV");
    (void)remove("./build_test_sdcard/E0000088.TMP");
    (void)remove("./build_test_sdcard/E0000088.CSV");
    (void)remove("./build_test_sdcard/E0000099.TMP");
    (void)remove("./build_test_sdcard/E0000099.CSV");
    (void)remove("./build_test_sdcard/EFFFFFFF.TMP");
    (void)remove("./build_test_sdcard/EFFFFFFF.CSV");
    (void)remove("./build_test_sdcard/CORRUPT.CSV");
    (void)remove("./build_test_sdcard/TRUNC.CSV");
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
    cfg.mount_point    = TEST_MOUNT_DIR;
    cfg.host_id        = SPI2_HOST;
    cfg.gpio_mosi      = 11;
    cfg.gpio_miso      = 13;
    cfg.gpio_sclk      = 12;
    cfg.gpio_cs        = 10;
    cfg.manage_spi_bus = true;
    cfg.format_if_mount_failed = false;
    return cfg;
}

/* =========================================================================
 * Test 1: Configuration Validation
 * ========================================================================= */
static int test_config_validation(void)
{
    bb_storage_config_t cfg;

    bb_storage_test_reset_faults();

    /* 1. NULL config must fail */
    TEST_ASSERT(bb_storage_init(NULL) == ESP_ERR_INVALID_ARG,
                "NULL config should return ESP_ERR_INVALID_ARG");

    /* 2. Default config has GPIO_NUM_NC (-1) and must be rejected until pins are set */
    cfg = make_default_config();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "Default unconfigured GPIO_NUM_NC pins should be rejected");

    /* 3. format_if_mount_failed = true must be rejected by safety rule */
    cfg = make_valid_test_config();
    cfg.format_if_mount_failed = true;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "format_if_mount_failed=true must be rejected");

    /* 4. Invalid mount points (NULL, trailing slash, path traversal) */
    cfg = make_valid_test_config();
    cfg.mount_point = NULL;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "NULL mount_point must be rejected");

    cfg.mount_point = "/sdcard/";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "Trailing slash in mount_point must be rejected");

    cfg.mount_point = "../sdcard";
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "Path traversal in mount_point must be rejected");

    /* 5. Duplicate SPI pins when manage_spi_bus == true */
    cfg = make_valid_test_config();
    cfg.gpio_mosi = 12;
    cfg.gpio_sclk = 12;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_ARG,
                "Duplicate SPI pins must be rejected");

    /* 6. Shared SPI bus mode (manage_spi_bus == false) only requires gpio_cs */
    cfg = make_default_config();
    cfg.mount_point    = TEST_MOUNT_DIR;
    cfg.manage_spi_bus = false;
    cfg.gpio_cs        = 10;
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK,
                "manage_spi_bus=false with valid gpio_cs should succeed");
    TEST_ASSERT(bb_storage_deinit() == ESP_OK,
                "deinit after shared-bus init should succeed");

    return 1;
}

/* =========================================================================
 * Test 2: Lifecycle State Machine & Mount Failure Handling
 * ========================================================================= */
static int test_lifecycle_and_mount_failures(void)
{
    bb_storage_config_t cfg = make_valid_test_config();
    bb_storage_test_faults_t faults;

    bb_storage_test_reset_faults();

    /* Deinit when not initialized returns ESP_ERR_INVALID_STATE */
    TEST_ASSERT(bb_storage_deinit() == ESP_ERR_INVALID_STATE,
                "deinit before init must return ESP_ERR_INVALID_STATE");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_UNINITIALIZED,
                "initial status must be UNINITIALIZED");

    /* Simulate SPI bus init failure */
    memset(&faults, 0, sizeof(faults));
    faults.fail_spi_bus = true;
    bb_storage_test_set_faults(&faults);
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_FAIL,
                "simulated SPI bus failure should return ESP_FAIL");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_MOUNT_FAILED,
                "status after SPI bus failure should be MOUNT_FAILED");

    /* Simulate SD card absent / mount failure */
    memset(&faults, 0, sizeof(faults));
    faults.fail_mount = true;
    bb_storage_test_set_faults(&faults);
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_FAIL,
                "simulated mount failure should return ESP_FAIL");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_MOUNT_FAILED,
                "status after mount failure should be MOUNT_FAILED");

    /* Normal mount */
    bb_storage_test_reset_faults();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK,
                "normal init should return ESP_OK");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_READY,
                "status after init should be READY");
    TEST_ASSERT(strcmp(bb_storage_status_to_str(bb_storage_get_status()), "READY") == 0,
                "status string should be READY");

    /* Repeated init while mounted must fail with ESP_ERR_INVALID_STATE */
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_ERR_INVALID_STATE,
                "repeated init must return ESP_ERR_INVALID_STATE");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_READY,
                "status must remain READY after rejected repeated init");

    /* Deinit and repeated deinit */
    TEST_ASSERT(bb_storage_deinit() == ESP_OK,
                "deinit should return ESP_OK");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_UNINITIALIZED,
                "status after deinit should be UNINITIALIZED");
    TEST_ASSERT(bb_storage_deinit() == ESP_ERR_INVALID_STATE,
                "repeated deinit must return ESP_ERR_INVALID_STATE");

    return 1;
}

/* =========================================================================
 * Test 3: Event Record Input Validation
 * ========================================================================= */
static int test_event_input_validation(void)
{
    bb_storage_sample_t sample = {
        1000000ULL, 0, (uint8_t)BB_SENSOR_MPU6050_I2C, 0, 0, 1000, 0, 0, 0
    };
    bb_event_record_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.format_version       = BB_STORAGE_FORMAT_VERSION;
    rec.sample_rate_hz       = 100U;
    rec.event_id             = 1UL;
    rec.trigger_timestamp_us = 1000000ULL;
    rec.flags                = (uint32_t)BB_EVENT_FLAG_SIMULATED;
    rec.pre_event_samples    = 0U;
    rec.post_event_samples   = 1U;
    rec.sample_count         = 1U;
    rec.samples              = &sample;

    TEST_ASSERT(bb_storage_validate_event(NULL) == ESP_ERR_INVALID_ARG,
                "NULL event must fail");
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_OK,
                "valid event must pass");

    rec.format_version = 99U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "unsupported format_version must fail");
    rec.format_version = BB_STORAGE_FORMAT_VERSION;

    rec.event_id = 0UL;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "event_id == 0 must fail");

    rec.event_id = BB_STORAGE_MAX_EVENT_ID + 1UL;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "event_id > BB_STORAGE_MAX_EVENT_ID must fail");
    rec.event_id = 1UL;

    rec.sample_count = 0U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "sample_count == 0 must fail");

    rec.sample_count = BB_STORAGE_MAX_SAMPLES + 1U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_SIZE,
                "oversized sample_count must return ESP_ERR_INVALID_SIZE");
    rec.sample_count = 1U;

    rec.samples = NULL;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "NULL samples pointer must fail");
    rec.samples = &sample;

    rec.pre_event_samples  = 1U;
    rec.post_event_samples = 1U;
    TEST_ASSERT(bb_storage_validate_event(&rec) == ESP_ERR_INVALID_ARG,
                "pre + post > sample_count must fail");

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
                "build_event_paths should succeed for event_id=1");
    TEST_ASSERT(strcmp(tmp_path, "/sdcard/E0000001.TMP") == 0,
                "tmp_path should be /sdcard/E0000001.TMP");
    TEST_ASSERT(strcmp(final_path, "/sdcard/E0000001.CSV") == 0,
                "final_path should be /sdcard/E0000001.CSV");

    TEST_ASSERT(bb_storage_build_event_paths("/sdcard", 1UL,
                                             tmp_path, 10U,
                                             final_path, sizeof(final_path)) == ESP_ERR_INVALID_SIZE,
                "too-small buffer must return ESP_ERR_INVALID_SIZE");

    /* Verify completed event filename filter for blackbox_interface */
    TEST_ASSERT(bb_storage_is_completed_event_filename("E0000001.CSV") == true,
                "E0000001.CSV is a valid completed filename");
    TEST_ASSERT(bb_storage_is_completed_event_filename("EFFFFFFF.CSV") == true,
                "EFFFFFFF.CSV is a valid completed filename");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E0000001.TMP") == false,
                "E0000001.TMP (incomplete write) must be rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("E0000000.CSV") == false,
                "E0000000.CSV (zero ID) must be rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("e0000001.csv") == false,
                "lowercase filename must be rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("../E0000001.CSV") == false,
                "path traversal must be rejected");
    TEST_ASSERT(bb_storage_is_completed_event_filename("HELLO.TXT") == false,
                "unrelated file must be rejected");

    return 1;
}

/* =========================================================================
 * Test 5: Synthetic Event CSV Serialization & CRC-32 Determinism
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
    size_t csv_len = 0U;
    uint32_t crc1, crc2;

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

    /* Buffer too small check */
    TEST_ASSERT(bb_storage_format_event_csv(&event, csv_buf, 64U, &csv_len) == ESP_ERR_INVALID_SIZE,
                "small buffer must return ESP_ERR_INVALID_SIZE");

    /* Full serialization */
    TEST_ASSERT(bb_storage_format_event_csv(&event, csv_buf, sizeof(csv_buf), &csv_len) == ESP_OK,
                "format_event_csv should succeed");
    TEST_ASSERT(csv_len > 200U && csv_len == strlen(csv_buf),
                "csv_len must match strlen(csv_buf)");
    TEST_ASSERT(strstr(csv_buf, "# BLACKBOX_EVENT_FORMAT,v1\n") != NULL,
                "CSV must contain format header");
    TEST_ASSERT(strstr(csv_buf, "# event_id,1\n") != NULL,
                "CSV must contain event_id metadata");
    TEST_ASSERT(strstr(csv_buf, "# END_EVENT,count=4,crc32=0x") != NULL,
                "CSV must contain END_EVENT footer with count and crc32");

    /* Verify CRC32 sensitivity to sample mutation */
    memcpy(modified_samples, samples, sizeof(samples));
    crc1 = bb_storage_compute_samples_crc32(samples, 4U);
    modified_samples[2].accel_x_mg += 1;
    crc2 = bb_storage_compute_samples_crc32(modified_samples, 4U);
    TEST_ASSERT(crc1 != 0UL && crc1 != crc2,
                "CRC32 must change when sample data changes");

    return 1;
}

/* =========================================================================
 * Test 6: End-to-End File Persistence, Verification & Corruption Detection
 * ========================================================================= */
static int test_write_verify_and_corruption_detection(void)
{
    static const bb_storage_sample_t samples[3] = {
        { 2000000ULL, -10, (uint8_t)BB_SENSOR_MPU6050_I2C,    5,  -12, 1001,  50, -10,  5 },
        { 2010000ULL,   0, (uint8_t)BB_SENSOR_ADXL345_SPI, 3100, -600, 1180,   0,   0,  0 },
        { 2020000ULL,  10, (uint8_t)BB_SENSOR_MPU6050_I2C,  220,  -40,  995, 180, -35, 20 }
    };
    bb_event_record_t event;
    bb_storage_config_t cfg = make_valid_test_config();
    bb_storage_stats_t stats;
    char final_path[BB_STORAGE_MAX_PATH_LEN];
    char file_buf[1024];
    FILE *fp;
    size_t nread;
    char *pos;

    memset(&event, 0, sizeof(event));
    event.format_version       = BB_STORAGE_FORMAT_VERSION;
    event.sample_rate_hz       = 100U;
    event.event_id             = 1UL;
    event.trigger_timestamp_us = 2010000ULL;
    event.flags                = (uint32_t)(BB_EVENT_FLAG_TRIG_SPI | BB_EVENT_FLAG_SIMULATED);
    event.pre_event_samples    = 1U;
    event.post_event_samples   = 1U;
    event.sample_count         = 3U;
    event.samples              = samples;

    cleanup_test_files();
    bb_storage_test_reset_faults();

    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "init should succeed");

    /* Write event #1 */
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_OK,
                "write_event #1 should succeed");
    TEST_ASSERT(strcmp(final_path, "./build_test_sdcard/E0000001.CSV") == 0,
                "final_path should match E0000001.CSV");

    /* Verify event #1 on disk */
    TEST_ASSERT(bb_storage_verify_event_file(final_path, 1UL, 3U) == ESP_OK,
                "verify_event_file should pass on valid written file");

    /* Duplicate event_id write must be rejected to prevent silent data overwrite */
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_ERR_INVALID_STATE,
                "duplicate event_id must return ESP_ERR_INVALID_STATE");

    /* Write event #42 (0x2A) */
    event.event_id = 42UL;
    TEST_ASSERT(bb_storage_write_event(&event, final_path, sizeof(final_path)) == ESP_OK,
                "write_event #42 should succeed");
    TEST_ASSERT(bb_storage_verify_event_file(final_path, 42UL, 3U) == ESP_OK,
                "verify_event_file should pass for event #42");

    /* Check stats */
    TEST_ASSERT(bb_storage_get_stats(&stats) == ESP_OK, "get_stats should succeed");
    TEST_ASSERT(stats.events_written == 2UL, "events_written should be 2");
    TEST_ASSERT(stats.last_event_id == 42UL, "last_event_id should be 42");
    TEST_ASSERT(stats.last_write_bytes > 200U, "last_write_bytes should be populated");

    /* Test CRC corruption detection */
    fp = fopen(final_path, "rb");
    TEST_ASSERT(fp != NULL, "open E000002A.CSV for corruption test");
    nread = fread(file_buf, 1U, sizeof(file_buf) - 1U, fp);
    fclose(fp);
    file_buf[nread] = '\0';

    pos = strstr(file_buf, "3100");
    TEST_ASSERT(pos != NULL, "locate sample value 3100 in CSV");
    pos[0] = '4'; /* Mutate 3100 -> 4100 without updating CRC */

    fp = fopen("./build_test_sdcard/CORRUPT.CSV", "wb");
    TEST_ASSERT(fp != NULL, "create CORRUPT.CSV");
    fwrite(file_buf, 1U, nread, fp);
    fclose(fp);
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/CORRUPT.CSV", 42UL, 3U) == ESP_ERR_INVALID_CRC,
                "corrupted sample must be rejected with ESP_ERR_INVALID_CRC");

    /* Test truncated file (missing # END_EVENT footer) */
    pos = strstr(file_buf, "# END_EVENT");
    TEST_ASSERT(pos != NULL, "locate # END_EVENT footer");
    *pos = '\0';
    fp = fopen("./build_test_sdcard/TRUNC.CSV", "wb");
    TEST_ASSERT(fp != NULL, "create TRUNC.CSV");
    fwrite(file_buf, 1U, strlen(file_buf), fp);
    fclose(fp);
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/TRUNC.CSV", 42UL, 3U) == ESP_FAIL,
                "truncated file without # END_EVENT must fail verification");

    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "deinit should succeed");
    return 1;
}

/* =========================================================================
 * Test 7: Fault Injection (ENOSPC, I/O Error, Incomplete .TMP Filtering)
 * ========================================================================= */
static int test_fault_injection_and_listing(void)
{
    static const bb_storage_sample_t sample = {
        3000000ULL, 0, (uint8_t)BB_SENSOR_ADXL345_SPI, 2100, -100, 990, 0, 0, 0
    };
    bb_event_record_t event;
    bb_storage_config_t cfg = make_valid_test_config();
    bb_storage_test_faults_t faults;
    char listed[8][BB_STORAGE_MAX_FILENAME_LEN];
    size_t count = 0U;
    size_t i;
    FILE *fp;

    memset(&event, 0, sizeof(event));
    event.format_version       = BB_STORAGE_FORMAT_VERSION;
    event.sample_rate_hz       = 100U;
    event.event_id             = 0x77UL;
    event.trigger_timestamp_us = 3000000ULL;
    event.flags                = (uint32_t)BB_EVENT_FLAG_SIMULATED;
    event.pre_event_samples    = 0U;
    event.post_event_samples   = 0U;
    event.sample_count         = 1U;
    event.samples              = &sample;

    bb_storage_test_reset_faults();
    TEST_ASSERT(bb_storage_init(&cfg) == ESP_OK, "init should succeed");

    /* 1. Inject ENOSPC (Storage Full) mid-write */
    memset(&faults, 0, sizeof(faults));
    faults.fail_write_enospc = true;
    bb_storage_test_set_faults(&faults);

    TEST_ASSERT(bb_storage_write_event(&event, NULL, 0U) == ESP_ERR_NO_MEM,
                "ENOSPC fault should return ESP_ERR_NO_MEM");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_FULL,
                "status should be BB_STORAGE_STATUS_FULL after ENOSPC");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000077.TMP", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                "partial .TMP file must be cleaned up on ENOSPC failure");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000077.CSV", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                ".CSV file must not exist after ENOSPC failure");

    /* 2. Inject low-level I/O error mid-write */
    memset(&faults, 0, sizeof(faults));
    faults.fail_write_io = true;
    bb_storage_test_set_faults(&faults);
    event.event_id = 0x88UL;

    TEST_ASSERT(bb_storage_write_event(&event, NULL, 0U) == ESP_FAIL,
                "I/O fault should return ESP_FAIL");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_WRITE_ERROR,
                "status should be BB_STORAGE_STATUS_WRITE_ERROR after I/O fault");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000088.TMP", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                "partial .TMP file must be cleaned up on I/O failure");

    /* 2b. Inject rename(.TMP -> .CSV) failure */
    memset(&faults, 0, sizeof(faults));
    faults.fail_rename = true;
    bb_storage_test_set_faults(&faults);
    event.event_id = 0x99UL;

    TEST_ASSERT(bb_storage_write_event(&event, NULL, 0U) == ESP_FAIL,
                "rename fault should return ESP_FAIL");
    TEST_ASSERT(bb_storage_get_status() == BB_STORAGE_STATUS_WRITE_ERROR,
                "status should be BB_STORAGE_STATUS_WRITE_ERROR after rename fault");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000099.TMP", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                "partial .TMP file must be cleaned up on rename failure");
    TEST_ASSERT(bb_storage_verify_event_file("./build_test_sdcard/E0000099.CSV", 0UL, 0U) == ESP_ERR_NOT_FOUND,
                ".CSV file must not exist after rename failure");

    /* 3. Create a simulated leftover .TMP file and verify listing excludes it */
    bb_storage_test_reset_faults();
    fp = fopen("./build_test_sdcard/E0000099.TMP", "wb");
    TEST_ASSERT(fp != NULL, "create simulated incomplete E0000099.TMP");
    fprintf(fp, "# BLACKBOX_EVENT_FORMAT,v1\nincomplete_data");
    fclose(fp);

    TEST_ASSERT(bb_storage_list_completed_events(listed, 8U, &count) == ESP_OK,
                "list_completed_events should succeed");
    /* From Test 6, E0000001.CSV and E000002A.CSV are valid completed event filenames */
    TEST_ASSERT(count == 2U, "only the 2 completed E*.CSV files should be listed");
    for (i = 0U; i < count; i++) {
        TEST_ASSERT(bb_storage_is_completed_event_filename(listed[i]),
                    "every listed file must satisfy is_completed_event_filename");
        TEST_ASSERT(strstr(listed[i], ".TMP") == NULL,
                    "no .TMP file may appear in completed event list");
    }

    /* 4. Built-in self-test */
    TEST_ASSERT(bb_storage_self_test() == ESP_OK,
                "bb_storage_self_test() should pass");

    TEST_ASSERT(bb_storage_deinit() == ESP_OK, "deinit should succeed");
    cleanup_test_files();
    return 1;
}

int main(void)
{
    printf("============================================================\n");
    printf(" blackbox_storage Host Unit & Fault-Injection Test Suite\n");
    printf(" (NOTE: Uses SYNTHETIC/SIMULATED data on host filesystem)\n");
    printf("============================================================\n");

    RUN_TEST(test_config_validation);
    RUN_TEST(test_lifecycle_and_mount_failures);
    RUN_TEST(test_event_input_validation);
    RUN_TEST(test_paths_and_filename_predicate);
    RUN_TEST(test_csv_serialization_and_crc32);
    RUN_TEST(test_write_verify_and_corruption_detection);
    RUN_TEST(test_fault_injection_and_listing);

    printf("------------------------------------------------------------\n");
    printf(" Summary: %d/%d passed, %d failed\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    printf("============================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
