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

#ifndef BLACKBOX_STORAGE_H
#define BLACKBOX_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef BB_STORAGE_HOST_TEST
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#else
/*
 * Minimal ESP-IDF type & error-code shims for isolated host-side unit testing.
 * When compiled inside ESP-IDF (!defined(BB_STORAGE_HOST_TEST)), real SDK
 * headers above are used directly.
 */
typedef int esp_err_t;
typedef int gpio_num_t;
typedef int spi_host_device_t;

#define ESP_OK                    0
#define ESP_FAIL                 -1
#define ESP_ERR_NO_MEM            0x101
#define ESP_ERR_INVALID_ARG       0x102
#define ESP_ERR_INVALID_STATE     0x103
#define ESP_ERR_INVALID_SIZE      0x104
#define ESP_ERR_NOT_FOUND         0x105
#define ESP_ERR_NOT_SUPPORTED     0x106
#define ESP_ERR_TIMEOUT           0x107
#define ESP_ERR_INVALID_CRC       0x109

#define GPIO_NUM_NC              (-1)
#define SPI1_HOST                 0
#define SPI2_HOST                 1
#define SPI3_HOST                 2
#define SPI_HOST_MAX              3
#define SDSPI_DEFAULT_HOST        SPI2_HOST
#define SDMMC_FREQ_DEFAULT        20000
#endif /* BB_STORAGE_HOST_TEST */

#ifdef __cplusplus
extern "C" {
#endif

/** Current CSV event record format version. */
#define BB_STORAGE_FORMAT_VERSION       1U

/** Default VFS mount point for the microSD FAT partition. */
#define BB_STORAGE_DEFAULT_MOUNT_POINT  "/sdcard"

/** Maximum length of the VFS mount point string (excluding null terminator). */
#define BB_STORAGE_MAX_MOUNT_PATH_LEN   31U

/** Maximum 8.3 filename buffer size including null terminator (e.g. "E0000001.CSV"). */
#define BB_STORAGE_MAX_FILENAME_LEN     16U

/** Maximum full file path buffer size including null terminator. */
#define BB_STORAGE_MAX_PATH_LEN         64U

/**
 * Maximum number of sensor samples permitted in a single event record.
 * At 100 Hz for 2 sensors over a 10-second window (5s pre + 5s post), a typical
 * event contains ~1000-2000 samples. Bounding at 2048 prevents unbounded writes.
 */
#define BB_STORAGE_MAX_SAMPLES          2048U

/**
 * Maximum valid event_id (28-bit hex -> 7 hex digits after 'E' prefix).
 * Guarantees 8.3 short-filename compatibility ("E0000001.TMP" -> "E0000001.CSV")
 * even when FATFS Long File Names (LFN) are disabled in sdkconfig.
 */
#define BB_STORAGE_MAX_EVENT_ID         0x0FFFFFFFUL

/**
 * @brief High-level operational state of the blackbox_storage component.
 */
typedef enum {
    BB_STORAGE_STATUS_UNINITIALIZED = 0, /**< Component not initialized or already deinitialized */
    BB_STORAGE_STATUS_READY         = 1, /**< microSD mounted and ready for event recording */
    BB_STORAGE_STATUS_MOUNT_FAILED  = 2, /**< SPI init or FATFS mount failed (card absent/unformatted/wiring) */
    BB_STORAGE_STATUS_WRITE_ERROR   = 3, /**< Most recent file write, flush, close, or rename failed */
    BB_STORAGE_STATUS_FULL          = 4  /**< Storage has insufficient free space for recording */
} bb_storage_status_t;

/**
 * @brief Identifier for the sensor source of a motion sample.
 *        PROVISIONAL CONTRACT — must be confirmed with blackbox_core.
 */
typedef enum {
    BB_SENSOR_NONE        = 0,
    BB_SENSOR_MPU6050_I2C = 1, /**< Student 1: MPU6050 6-axis IMU over I2C */
    BB_SENSOR_ADXL345_SPI = 2  /**< Student 2: ADXL345 3-axis accelerometer over SPI */
} bb_sensor_id_t;

/**
 * @brief Bitmask flags describing event trigger context.
 *        PROVISIONAL CONTRACT — must be confirmed with blackbox_core.
 */
typedef enum {
    BB_EVENT_FLAG_NONE            = 0U,
    BB_EVENT_FLAG_TRIG_I2C        = (1U << 0), /**< Triggered by MPU6050 threshold */
    BB_EVENT_FLAG_TRIG_SPI        = (1U << 1), /**< Triggered by ADXL345 threshold */
    BB_EVENT_FLAG_CROSS_CONFIRMED = (1U << 2), /**< Confirmed by both sensors */
    BB_EVENT_FLAG_SIMULATED       = (1U << 7)  /**< Synthetic/simulated test data (not real crash/motion) */
} bb_event_flags_t;

/**
 * @brief Hardware and filesystem configuration for `blackbox_storage`.
 *
 * @important GPIO pins default to `GPIO_NUM_NC` (-1) in `BB_STORAGE_CONFIG_DEFAULT()`.
 *            Do NOT guess or hardcode unverified ESP32-S3 pins. The application or
 *            board configuration must explicitly assign verified GPIO numbers before
 *            calling `bb_storage_init()`.
 *
 * @important `mount_point` must start with `'/'`, contain no trailing `'/'`, `'\\'`,
 *            `..`, whitespace, or control characters, and fit within `ESP_VFS_PATH_MAX` (15 chars)
 *            on ESP-IDF targets (up to `BB_STORAGE_MAX_MOUNT_PATH_LEN` in host simulation).
 *
 * @important When `manage_spi_bus == false` (shared SPI bus with `blackbox_imu_spi`/ADXL345),
 *            the external SPI bus owner MUST initialize the bus with DMA enabled and
 *            `max_transfer_sz >= 4000`, and keep all co-resident SPI CS lines de-asserted HIGH
 *            before calling `bb_storage_init()`.
 *
 * @important `format_if_mount_failed` MUST remain `false`. To protect user data on
 *            microSD cards, `bb_storage_init()` rejects `format_if_mount_failed == true`
 *            with `ESP_ERR_INVALID_ARG`.
 */
typedef struct {
    const char       *mount_point;            /**< VFS mount prefix, e.g. "/sdcard" (<= 15 chars on ESP-IDF) */
    spi_host_device_t host_id;                /**< SPI controller (`SPI2_HOST` or `SPI3_HOST` on ESP32-S3) */
    gpio_num_t        gpio_mosi;              /**< SPI MOSI pin (required if manage_spi_bus == true) */
    gpio_num_t        gpio_miso;              /**< SPI MISO pin (required if manage_spi_bus == true) */
    gpio_num_t        gpio_sclk;              /**< SPI SCLK pin (required if manage_spi_bus == true) */
    gpio_num_t        gpio_cs;                /**< microSD Chip Select (CS) pin (always required) */
    bool              manage_spi_bus;         /**< true: Storage calls spi_bus_initialize/spi_bus_free;
                                                   false: SPI bus is already initialized by external owner
                                                   (e.g. when sharing SPI bus with ADXL345) */
    bool              format_if_mount_failed; /**< MUST be false; auto-formatting SD cards is forbidden */
    int               max_open_files;         /**< Max simultaneous open files in FATFS VFS (1..16) */
    int               max_freq_khz;           /**< SDSPI clock frequency in kHz (e.g. 20000 for 20 MHz,
                                                   or lower such as 4000-10000 on breadboard wiring) */
} bb_storage_config_t;

/**
 * @brief Safe default configuration initializer.
 *
 * Sets `mount_point = "/sdcard"`, `host_id = SDSPI_DEFAULT_HOST`, `manage_spi_bus = true`,
 * `format_if_mount_failed = false`, `max_open_files = 4`, `max_freq_khz = SDMMC_FREQ_DEFAULT`,
 * and all GPIO pins to `GPIO_NUM_NC` (-1) so the caller must supply verified pins explicitly.
 */
#define BB_STORAGE_CONFIG_DEFAULT() { \
    .mount_point            = BB_STORAGE_DEFAULT_MOUNT_POINT, \
    .host_id                = SDSPI_DEFAULT_HOST, \
    .gpio_mosi              = GPIO_NUM_NC, \
    .gpio_miso              = GPIO_NUM_NC, \
    .gpio_sclk              = GPIO_NUM_NC, \
    .gpio_cs                = GPIO_NUM_NC, \
    .manage_spi_bus         = true, \
    .format_if_mount_failed = false, \
    .max_open_files         = 4, \
    .max_freq_khz           = SDMMC_FREQ_DEFAULT \
}

/**
 * @brief Single timestamped inertial measurement sample within an event window.
 *        PROVISIONAL CONTRACT — subject to team agreement in `docs/02-KONTRAK-INTEGRASI.md`.
 *
 * Fixed-point integer units (`mg` and `mdps`) are used for deterministic CSV
 * serialization and to avoid floating-point formatting overhead or NaN/Inf issues:
 * - `1000 mg = 1.0 g` (~9.80665 m/s^2)
 * - `1000 mdps = 1.0 deg/s` (set to 0 for accelerometer-only sensors like ADXL345)
 */
typedef struct {
    uint64_t timestamp_us; /**< Monotonic timestamp in microseconds (e.g. esp_timer_get_time()) */
    int32_t  offset_ms;    /**< Offset relative to trigger in ms (<0 pre-event, 0 trigger, >0 post-event) */
    uint8_t  sensor_id;    /**< Sensor source identifier (`bb_sensor_id_t`) */
    int32_t  accel_x_mg;   /**< X-axis linear acceleration in milli-g */
    int32_t  accel_y_mg;   /**< Y-axis linear acceleration in milli-g */
    int32_t  accel_z_mg;   /**< Z-axis linear acceleration in milli-g */
    int32_t  gyro_x_mdps;  /**< X-axis angular rate in milli-deg/s (0 if N/A) */
    int32_t  gyro_y_mdps;  /**< Y-axis angular rate in milli-deg/s (0 if N/A) */
    int32_t  gyro_z_mdps;  /**< Z-axis angular rate in milli-deg/s (0 if N/A) */
} bb_storage_sample_t;

/**
 * @brief Complete motion event snapshot submitted by `blackbox_core` for persistence.
 *
 * @note Buffer Ownership Contract:
 *       `bb_storage_write_event()` is synchronous and reads `event` and `event->samples`
 *       before returning. The caller (`blackbox_core` or dedicated storage task) retains
 *       ownership of `event` and `event->samples` and may reuse or free them immediately
 *       after `bb_storage_write_event()` returns.
 *
 * @warning Do NOT call `bb_storage_write_event()` directly inside a high-frequency
 *          sensor acquisition ISR or time-critical sampling callback. Invoke it from
 *          a lower-priority persistence task fed by an event queue.
 */
typedef struct {
    uint16_t                   format_version;       /**< Must be `BB_STORAGE_FORMAT_VERSION` (1) */
    uint16_t                   sample_rate_hz;       /**< Nominal sensor sampling rate in Hz (e.g. 100) */
    uint32_t                   event_id;             /**< Unique event ID (1 .. BB_STORAGE_MAX_EVENT_ID) */
    uint64_t                   trigger_timestamp_us; /**< Monotonic timestamp when event triggered (us) */
    uint32_t                   flags;                /**< Bitmask of `bb_event_flags_t` */
    size_t                     pre_event_samples;    /**< Count of pre-trigger samples included */
    size_t                     post_event_samples;   /**< Count of post-trigger samples included */
    size_t                     sample_count;         /**< Total samples in `samples` array (1 .. BB_STORAGE_MAX_SAMPLES) */
    const bb_storage_sample_t *samples;              /**< Pointer to contiguous array of `sample_count` samples */
} bb_event_record_t;

/**
 * @brief Diagnostic statistics and filesystem capacity snapshot.
 */
typedef struct {
    bb_storage_status_t status;           /**< Current operational status */
    uint32_t            events_written;   /**< Total events finalized since init */
    uint32_t            write_failures;   /**< Total failed write attempts since init */
    uint32_t            last_event_id;    /**< ID of the most recently finalized event (0 if none) */
    size_t              last_write_bytes; /**< Size in bytes of the most recently written event file */
    esp_err_t           last_error;       /**< Most recent error code (ESP_OK if none) */
    uint64_t            total_bytes;      /**< Total FAT partition capacity in bytes (0 if unavailable) */
    uint64_t            free_bytes;       /**< Free FAT partition space in bytes (0 if unavailable) */
} bb_storage_stats_t;

/**
 * @brief Initialize `blackbox_storage` and mount the FAT-formatted microSD card via SDSPI.
 *
 * Sequence:
 * 1. Ensures the statically backed internal mutex exists and acquires it safely.
 * 2. Validates `cfg` (non-null, valid `mount_point`, valid `host_id` `SPI2_HOST`/`SPI3_HOST`,
 *    `format_if_mount_failed == false`, valid ESP32-S3 `gpio_cs`, and valid/unique
 *    `gpio_mosi/miso/sclk` when `manage_spi_bus == true`).
 * 3. If `cfg->manage_spi_bus == true`, initializes the SPI bus with `spi_bus_initialize()`.
 * 4. Mounts the microSD FAT partition via `esp_vfs_fat_sdspi_mount()` with `format_if_mount_failed = false`.
 * 5. On mount failure, cleans up any SPI bus it initialized so no resources leak.
 *
 * @param[in] cfg Pointer to storage configuration.
 * @return
 *   - `ESP_OK` on successful mount
 *   - `ESP_ERR_INVALID_ARG` if `cfg` is NULL, `host_id`/pins are invalid, or `format_if_mount_failed` is true
 *   - `ESP_ERR_INVALID_STATE` if already initialized
 *   - Other `esp_err_t` codes propagated from `spi_bus_initialize()` or `esp_vfs_fat_sdspi_mount()`
 */
esp_err_t bb_storage_init(const bb_storage_config_t *cfg);

/**
 * @brief Unmount the microSD FAT filesystem and release resources owned by `blackbox_storage`.
 *
 * Calls `esp_vfs_fat_sdcard_unmount()`. If unmounting fails, storage remains marked
 * initialized and `spi_bus_free()` is NOT called so ownership is preserved for retry.
 * Once unmounting succeeds, calls `spi_bus_free()` ONLY if `manage_spi_bus == true`
 * succeeded during `bb_storage_init()`.
 *
 * @return
 *   - `ESP_OK` on success
 *   - `ESP_ERR_INVALID_STATE` if not currently initialized
 *   - Error code from `esp_vfs_fat_sdcard_unmount()` or `spi_bus_free()` on failure
 */
esp_err_t bb_storage_deinit(void);

/**
 * @brief Return the current high-level status of the Storage component.
 *
 * Reads status lock-free/atomically so UI and interface polling tasks do not stall
 * waiting on ongoing microSD writes.
 */
bb_storage_status_t bb_storage_get_status(void);

/**
 * @brief Return a human-readable string for a `bb_storage_status_t` value.
 */
const char *bb_storage_status_to_str(bb_storage_status_t status);

/**
 * @brief Retrieve a snapshot of storage statistics and FAT filesystem capacity.
 *
 * @param[out] out_stats Pointer to stats structure to populate.
 * @return
 *   - `ESP_OK` on success
 *   - `ESP_ERR_INVALID_ARG` if `out_stats` is NULL
 */
esp_err_t bb_storage_get_stats(bb_storage_stats_t *out_stats);

/**
 * @brief Validate an event record structure before serialization or writing.
 *
 * Checks non-null pointer, `format_version == BB_STORAGE_FORMAT_VERSION`,
 * `event_id` within `1 .. BB_STORAGE_MAX_EVENT_ID`, `sample_count` within
 * `1 .. BB_STORAGE_MAX_SAMPLES`, non-null `samples`, and consistent
 * `pre_event_samples + post_event_samples <= sample_count`.
 *
 * @param[in] event Pointer to event record.
 * @return
 *   - `ESP_OK` if valid
 *   - `ESP_ERR_INVALID_ARG` or `ESP_ERR_INVALID_SIZE` if invalid
 */
esp_err_t bb_storage_validate_event(const bb_event_record_t *event);

/**
 * @brief Compute deterministic CRC-32 (IEEE 802.3) over an array of samples.
 *
 * Used to write and verify the `# END_EVENT,count=...,crc32=0x...` footer line
 * in event CSV files.
 *
 * @param[in] samples Pointer to sample array.
 * @param[in] count   Number of samples.
 * @return 32-bit CRC value (0 if `samples` is NULL or `count == 0`).
 */
uint32_t bb_storage_compute_samples_crc32(const bb_storage_sample_t *samples, size_t count);

/**
 * @brief Build the temporary (`.TMP`) and finalized (`.CSV`) file paths for an event ID.
 *
 * Uses 8.3 short filenames (`E<7-hex-digits>.TMP` and `E<7-hex-digits>.CSV`) inside
 * `mount_point`, preventing path traversal and ensuring compatibility even when FATFS
 * Long File Names (LFN) are disabled.
 *
 * @param[in]  mount_point    Base VFS mount path (e.g. "/sdcard").
 * @param[in]  event_id       Event ID (`1 .. BB_STORAGE_MAX_EVENT_ID`).
 * @param[out] out_tmp_path   Buffer for temporary path (may be NULL if not needed).
 * @param[in]  tmp_size       Size of `out_tmp_path` buffer.
 * @param[out] out_final_path Buffer for finalized path (may be NULL if not needed).
 * @param[in]  final_size     Size of `out_final_path` buffer.
 * @return
 *   - `ESP_OK` on success
 *   - `ESP_ERR_INVALID_ARG` if `mount_point` is invalid or `event_id` is out of range
 *   - `ESP_ERR_INVALID_SIZE` if output buffers are too small
 */
esp_err_t bb_storage_build_event_paths(const char *mount_point,
                                       uint32_t event_id,
                                       char *out_tmp_path,
                                       size_t tmp_size,
                                       char *out_final_path,
                                       size_t final_size);

/**
 * @brief Check whether a filename represents a finalized, completed event recording.
 *
 * Returns `true` ONLY for filenames matching `E<7 uppercase hex digits>.CSV`
 * (e.g. `"E0000001.CSV"`). Returns `false` for in-progress `.TMP` files, hidden files,
 * paths containing `/` or `\`, or any other filename.
 *
 * `blackbox_interface` can use this predicate (or `bb_storage_list_completed_events()`)
 * to guarantee it never exposes partially written `.TMP` files to HTTP clients.
 *
 * @param[in] filename Basename to inspect (not a full path).
 * @return `true` if `filename` is a completed event `.CSV` file, `false` otherwise.
 */
bool bb_storage_is_completed_event_filename(const char *filename);

/**
 * @brief Serialize an event record into the versioned Black Box CSV format in memory.
 *
 * Useful for host-side unit testing, buffer size calculation, or previewing output
 * without touching hardware.
 *
 * @param[in]  event    Pointer to validated event record.
 * @param[out] out_buf  Destination character buffer.
 * @param[in]  buf_size Capacity of `out_buf` in bytes.
 * @param[out] out_len  Optional pointer receiving number of bytes written (excluding null terminator).
 * @return
 *   - `ESP_OK` on success
 *   - `ESP_ERR_INVALID_ARG` if `event` or `out_buf` is invalid
 *   - `ESP_ERR_INVALID_SIZE` if `buf_size` is too small to hold the serialized CSV
 */
esp_err_t bb_storage_format_event_csv(const bb_event_record_t *event,
                                      char *out_buf,
                                      size_t buf_size,
                                      size_t *out_len);

/**
 * @brief Persist a completed motion event record to the mounted microSD card.
 *
 * File Lifecycle Policy (Two-Phase Write + Rename):
 * 1. Validates `event` and checks that storage is initialized (allows recovery writes
 *    after an earlier `BB_STORAGE_STATUS_WRITE_ERROR` or `BB_STORAGE_STATUS_FULL`).
 * 2. Refuses to overwrite an already-finalized `E<id>.CSV` file (`ESP_ERR_INVALID_STATE`).
 * 3. Removes any stale `E<id>.TMP` left over from an interrupted prior attempt.
 * 4. Writes header, metadata, samples, and `# END_EVENT` CRC32 footer to `E<id>.TMP`.
 * 5. Flushes (`fflush` + `fsync`) and closes `E<id>.TMP`. Failure in write, flush,
 *    fsync, close, or rename removes `E<id>.TMP`, updates error statistics, and
 *    leaves `out_final_path[0] == '\0'`. `ENOSPC` maps to `ESP_ERR_NO_MEM` / `FULL`.
 * 6. Renames `E<id>.TMP` -> `E<id>.CSV` so `blackbox_interface` only ever sees completed files.
 *
 * @note FAT Power-Loss Limitation:
 *       While `.TMP` + flush + fsync + rename prevents readers from seeing partial writes
 *       during normal runtime, FAT filesystems on microSD are NOT power-loss atomic. Sudden
 *       power removal during SD sector writes can still corrupt FAT cluster chains.
 *
 * @param[in]  event          Pointer to event record to persist.
 * @param[out] out_final_path Optional buffer to receive the finalized file path (e.g. "/sdcard/E0000001.CSV").
 * @param[in]  final_path_size Size of `out_final_path` buffer (ignored if `out_final_path` is NULL).
 * @return
 *   - `ESP_OK` on durable write and finalization
 *   - `ESP_ERR_INVALID_ARG` / `ESP_ERR_INVALID_SIZE` on invalid event input or output buffer
 *   - `ESP_ERR_INVALID_STATE` if storage is not mounted or `event_id` already exists
 *   - `ESP_ERR_NO_MEM` if storage is full (`ENOSPC`)
 *   - `ESP_FAIL` on file open, write, flush, fsync, close, or rename failure
 */
esp_err_t bb_storage_write_event(const bb_event_record_t *event,
                                 char *out_final_path,
                                 size_t final_path_size);

/**
 * @brief Verify that a finalized event CSV file exists, is readable, and has valid
 *        metadata headers, column header, sequential sample rows, `# END_EVENT`
 *        completion footer, sample count, and CRC32.
 *
 * @param[in] file_path             Full path to the `.CSV` file to verify.
 * @param[in] expected_event_id     Expected event ID (`0` to skip ID check, otherwise `1..BB_STORAGE_MAX_EVENT_ID`).
 * @param[in] expected_sample_count Expected sample count (`0` to accept count in footer, otherwise `1..BB_STORAGE_MAX_SAMPLES`).
 * @return
 *   - `ESP_OK` if file is complete and valid
 *   - `ESP_ERR_INVALID_ARG` if `file_path` is NULL/empty or expected bounds exceed limits
 *   - `ESP_ERR_NOT_FOUND` if file cannot be opened
 *   - `ESP_ERR_INVALID_CRC` or `ESP_FAIL` if file is truncated, malformed, or mismatched
 */
esp_err_t bb_storage_verify_event_file(const char *file_path,
                                       uint32_t expected_event_id,
                                       size_t expected_sample_count);

/**
 * @brief List finalized event filenames (`E*.CSV`) currently stored on the microSD card.
 *
 * Automatically filters out directories, incomplete `.TMP` files, or unrelated files
 * so `blackbox_interface` only lists completed recordings.
 *
 * @param[out] out_filenames Array of filename buffers (`char[max_files][BB_STORAGE_MAX_FILENAME_LEN]`).
 *                           May be NULL if caller only wants to count completed files via `out_count`.
 * @param[in]  max_files     Maximum number of filenames to store in `out_filenames`.
 * @param[out] out_count     Receives number of completed event files found (up to `max_files` if array provided).
 * @return
 *   - `ESP_OK` on success
 *   - `ESP_ERR_INVALID_ARG` if `out_count` is NULL (or `max_files == 0` when `out_filenames != NULL`)
 *   - `ESP_ERR_INVALID_STATE` if storage is not mounted
 *   - `ESP_FAIL` if mount directory cannot be opened
 */
esp_err_t bb_storage_list_completed_events(char out_filenames[][BB_STORAGE_MAX_FILENAME_LEN],
                                           size_t max_files,
                                           size_t *out_count);

/**
 * @brief Run a non-destructive read/write/verify self-test on the mounted microSD card.
 *
 * Holds the internal mutex for the full duration and uses dedicated internal paths
 * (`BBTEST00.TMP` -> `BBTEST00.CSV`, excluded by `bb_storage_is_completed_event_filename()`),
 * writing a small synthetic test event (`event_id = BB_STORAGE_MAX_EVENT_ID`, flagged
 * `BB_EVENT_FLAG_SIMULATED`), verifying it strictly, removing the test file afterward,
 * and leaving operational event counters untouched when passing.
 *
 * @return
 *   - `ESP_OK` if write, readback verification, and cleanup all succeed
 *   - Error code otherwise
 */
esp_err_t bb_storage_self_test(void);

#ifdef BB_STORAGE_HOST_TEST
/**
 * @brief Fault-injection hooks available ONLY in host-side unit test builds (`BB_STORAGE_HOST_TEST`).
 */
typedef struct {
    bool     fail_mount;        /**< Simulate esp_vfs_fat_sdspi_mount failure */
    bool     fail_spi_bus;      /**< Simulate spi_bus_initialize failure */
    bool     fail_unmount;      /**< Simulate esp_vfs_fat_sdcard_unmount failure */
    bool     fail_spi_bus_free; /**< Simulate spi_bus_free failure */
    bool     fail_fopen;        /**< Simulate fopen(.TMP) failure */
    bool     fail_header_write; /**< Simulate header fprintf failure */
    bool     fail_write_enospc; /**< Simulate disk full (ENOSPC) mid-write */
    bool     fail_write_io;     /**< Simulate low-level SPI/SD I/O error mid-write */
    size_t   fail_after_samples;/**< Number of samples to write before fail_write_enospc/io triggers (0 = sample 0) */
    bool     fail_footer_write; /**< Simulate footer write failure */
    bool     fail_fflush;       /**< Simulate fflush failure */
    bool     fail_fsync;        /**< Simulate fsync failure */
    bool     fail_fclose;       /**< Simulate fclose failure */
    bool     fail_rename;       /**< Simulate rename(.TMP -> .CSV) failure */
    bool     fail_rename_enospc;/**< Simulate rename failing with ENOSPC */
    uint64_t mock_total_bytes;  /**< Simulated card total bytes */
    uint64_t mock_free_bytes;   /**< Simulated card free bytes */
} bb_storage_test_faults_t;

typedef struct {
    uint32_t spi_bus_init_calls; /**< Count of simulated spi_bus_initialize() attempts */
    uint32_t spi_bus_free_calls; /**< Count of simulated spi_bus_free() calls */
    uint32_t mount_calls;        /**< Count of simulated mount attempts */
    uint32_t unmount_calls;      /**< Count of simulated unmount attempts */
} bb_storage_test_counters_t;

void bb_storage_test_set_faults(const bb_storage_test_faults_t *faults);
void bb_storage_test_reset_faults(void);
void bb_storage_test_get_counters(bb_storage_test_counters_t *out_counters);
#endif /* BB_STORAGE_HOST_TEST */

#ifdef __cplusplus
}
#endif

#endif /* BLACKBOX_STORAGE_H */
