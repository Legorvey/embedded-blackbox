/*
 * SPDX-FileCopyrightText: 2026 Embedded Event Black Box Team (UNPAD)
 *
 * SPDX-License-Identifier: MIT
 *
 * File:    main/main.c
 * Purpose: Minimal ESP-IDF application entry point and isolated smoke test
 *          for the `blackbox_storage` component.
 *
 * Note:    GPIO pins for the microSD SPI interface are intentionally left at
 *          `GPIO_NUM_NC` (-1) until the team confirms the exact ESP32-S3 board
 *          pinout and SPI bus sharing topology with ADXL345.
 */

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "blackbox_storage.h"

static const char *TAG = "blackbox_main";

/*
 * TODO(team-hardware): Replace GPIO_NUM_NC with verified ESP32-S3 board pins
 * ONLY after confirming the exact ESP32-S3 development board, microSD module
 * voltage level, and SPI bus ownership with Student 2 (ADXL345).
 */
#ifndef BB_STORAGE_PIN_MOSI
#define BB_STORAGE_PIN_MOSI GPIO_NUM_NC
#endif
#ifndef BB_STORAGE_PIN_MISO
#define BB_STORAGE_PIN_MISO GPIO_NUM_NC
#endif
#ifndef BB_STORAGE_PIN_SCLK
#define BB_STORAGE_PIN_SCLK GPIO_NUM_NC
#endif
#ifndef BB_STORAGE_PIN_CS
#define BB_STORAGE_PIN_CS   GPIO_NUM_NC
#endif

static void run_synthetic_serialization_smoke_test(void)
{
    static const bb_storage_sample_t synthetic_samples[3] = {
        { 990000ULL,  -10, (uint8_t)BB_SENSOR_MPU6050_I2C,   12,  -18,  995,  105, -40,  15 },
        { 1000000ULL,   0, (uint8_t)BB_SENSOR_ADXL345_SPI, 2850, -420, 1120,    0,   0,   0 },
        { 1010000ULL,  10, (uint8_t)BB_SENSOR_MPU6050_I2C,  140,  -85, 1010,  320, -90,  45 }
    };

    const bb_event_record_t synthetic_event = {
        .format_version       = BB_STORAGE_FORMAT_VERSION,
        .sample_rate_hz       = 100U,
        .event_id             = 1UL,
        .trigger_timestamp_us = 1000000ULL,
        .flags                = (uint32_t)(BB_EVENT_FLAG_TRIG_SPI | BB_EVENT_FLAG_SIMULATED),
        .pre_event_samples    = 1U,
        .post_event_samples   = 1U,
        .sample_count         = 3U,
        .samples              = synthetic_samples
    };

    char csv_buf[1024];
    size_t csv_len = 0U;

    esp_err_t err = bb_storage_format_event_csv(&synthetic_event, csv_buf, sizeof(csv_buf), &csv_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Synthetic CSV serialization smoke test PASS (%u bytes, SIMULATED data)",
                 (unsigned)csv_len);
    } else {
        ESP_LOGE(TAG, "Synthetic CSV serialization smoke test FAIL: 0x%x", (unsigned)err);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Embedded Event Black Box — Storage Component Starter");

    run_synthetic_serialization_smoke_test();

    bb_storage_config_t cfg = BB_STORAGE_CONFIG_DEFAULT();
    cfg.gpio_mosi = BB_STORAGE_PIN_MOSI;
    cfg.gpio_miso = BB_STORAGE_PIN_MISO;
    cfg.gpio_sclk = BB_STORAGE_PIN_SCLK;
    cfg.gpio_cs   = BB_STORAGE_PIN_CS;

    if (cfg.gpio_cs == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "UNVERIFIED ON HARDWARE: microSD SPI GPIO pins are not configured (GPIO_NUM_NC).");
        ESP_LOGW(TAG, "Set verified ESP32-S3 pins before running physical microSD mount tests.");
        return;
    }

    esp_err_t err = bb_storage_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Storage initialization failed (status=%s, err=0x%x)",
                 bb_storage_status_to_str(bb_storage_get_status()), (unsigned)err);
        return;
    }

    err = bb_storage_self_test();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Hardware microSD self-test PASS");
    } else {
        ESP_LOGE(TAG, "Hardware microSD self-test FAIL: 0x%x", (unsigned)err);
    }

    (void)bb_storage_deinit();
}
