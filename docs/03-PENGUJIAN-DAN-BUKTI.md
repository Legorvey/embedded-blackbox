# 03 — Rencana Tes dan Bukti Nyata `blackbox_storage`

> Dokumen ini mencatat **hasil pengujian aktual yang benar-benar dijalankan** pada tanggal 13 November 2026, serta memisahkan dengan jujur mana yang sudah lulus (**PASS**) dan mana yang belum diuji di perangkat keras fisik (**UNVERIFIED ON HARDWARE**).

---

## 1. Tabel Hasil Pengujian (`blackbox_storage`)

| ID | Pengujian | Cara Uji / Perintah | Hasil / Bukti Aktual | Status |
|---|---|---|---|---|
| **T0** | Pemeriksaan Toolchain & SDK ESP-IDF | Cek `idf.py --version` dan lokasi toolchain `xtensa-esp32s3-elf-gcc` | `idf.py` standalone belum aktif di `PATH` global; namun compiler `xtensa-esp32s3-elf-gcc` (`14.2.0_20241119`) dan SDK header/lib ESP-IDF `release/v5.4` (`2f7dcd862a`) untuk `esp32s3` tersedia di `Arduino15/packages/esp32`. | **PARTIAL (SDK & Compiler ESP32-S3 Tersedia; `idf.py` CLI Belum di PATH)** |
| **T1** | Cross-Compilation Komponen untuk Target `esp32s3` | `powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_tests.ps1` (Step 1: `xtensa-esp32s3-elf-gcc -Wall -Wextra -Werror`) | `blackbox_storage.c` -> `blackbox_storage_esp32s3.o` (`100952` bytes) dan `main.c` -> `main_esp32s3.o` (`14992` bytes) berhasil dikompilasi terhadap header asli ESP-IDF v5.4 (`esp32s3`) dengan **0 warnings, 0 errors**. Lihat [`evidence/build-and-test-log.txt`](../evidence/build-and-test-log.txt). | **PASS (CROSS-COMPILE OK — FULL `idf.py build` LINK NOT RUN)** |
| **T2** | Validasi Konfigurasi & Guard Keamanan | `test_config_validation` di [`tests/test_blackbox_storage_host.c`](../tests/test_blackbox_storage_host.c) | Menolak `NULL`, menolak pin default `GPIO_NUM_NC` (`-1`), menolak `format_if_mount_failed = true` (`ESP_ERR_INVALID_ARG`), menolak path traversal `../sdcard`, menolak duplikasi pin SPI, dan menerima mode berbagi bus (`manage_spi_bus = false`). | **PASS (HOST UNIT TEST)** |
| **T3** | Write, Readback & Verifikasi CRC-32 | `test_write_verify_and_corruption_detection` + `bb_storage_self_test()` | Menulis event sintetis ke `E0000001.CSV` & `E000002A.CSV`, membaca ulang dan memverifikasi header, jumlah sampel, serta CRC-32 (`ESP_OK`). Mutasi 1 digit sampel terdeteksi sebagai `ESP_ERR_INVALID_CRC`; pemotongan footer `# END_EVENT` terdeteksi sebagai `ESP_FAIL`. | **PASS (SIMULASI HOST) — UNVERIFIED ON HARDWARE** |
| **T4** | Serialisasi Event Simulasi (CSV `v1`) | `test_csv_serialization_and_crc32` | Menghasilkan berkas CSV `v1` dengan sampel gabungan MPU6050 (I2C) & ADXL345 (SPI) dan footer `# END_EVENT,count=4,crc32=0x90C76B22`. Contoh hasil disimpan di [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv) (*berlabel SIMULASI*). | **PASS (SIMULASI HOST)** |
| **T5** | Penanganan SD Tidak Terpasang / Mount Gagal | `test_lifecycle_and_mount_failures` (`fail_spi_bus`, `fail_mount`) | Saat SPI init atau mount gagal, `bb_storage_init()` mengembalikan `ESP_FAIL`, membersihkan bus SPI yang telanjur di-init, dan mengubah status ke `BB_STORAGE_STATUS_MOUNT_FAILED` tanpa crash. Uji cabut kartu fisik belum dilakukan. | **PASS (FAULT INJECTION HOST) — UNVERIFIED ON HARDWARE** |
| **T6** | Penanganan SD Penuh (`ENOSPC`) / Error Tulis (`EIO`) / Gagal Rename | `test_fault_injection_and_listing` (`fail_write_enospc`, `fail_write_io`, `fail_rename`) | Saat gagal di tengah penulisan, file sementara `.TMP` ditutup dan dihapus otomatis (`remove()`), penghitung `write_failures` bertambah, dan status berubah ke `FULL` (`ESP_ERR_NO_MEM`) atau `WRITE_ERROR` (`ESP_FAIL`). | **PASS (FAULT INJECTION HOST) — UNVERIFIED ON HARDWARE** |
| **T7** | Pemisahan File Belum Selesai (`.TMP`) vs Selesai (`.CSV`) | `test_paths_and_filename_predicate` & `test_fault_injection_and_listing` | File `E0000099.TMP` diabaikan sepenuhnya oleh `bb_storage_is_completed_event_filename()` dan `bb_storage_list_completed_events()`; hanya `E0000001.CSV` dan `E000002A.CSV` yang dikembalikan ke Interface. | **PASS (HOST UNIT TEST)** |
| **T8** | Mount microSD Fisik di Board ESP32-S3 | Sambungkan modul microSD ke pin ESP32-S3 terverifikasi, jalankan firmware | Belum ada board ESP32-S3 terhubung ke port serial (`GetPortNames()` kosong) dan pin GPIO belum ditetapkan tim. | **NOT RUN (UNVERIFIED ON HARDWARE)** |
| **T9** | Konkurensi FreeRTOS & Berbagi Bus SPI dengan ADXL345 | Uji akuisisi sensor 100 Hz bersamaan dengan penulisan SD | Mutex internal `SemaphoreHandle_t` dan opsi `manage_spi_bus = false` sudah diimplementasikan; pengujian runtime menunggu integrasi `blackbox_core` dan `blackbox_imu_spi`. | **NOT RUN (INTEGRATION PENDING)** |

---

## 2. Tabel Metrik Kinerja (Baseline vs Pengembangan)

> Sesuai aturan integritas akademik, angka latensi perangkat keras **tidak dikarang** sebelum board ESP32-S3 dan kartu microSD fisik diuji.

| Metrik | Baseline Referensi | Pengembangan (`blackbox_storage`) | Status / Cara Ukur Saat Hardware Tersedia |
|---|---|---|---|
| Write latency (ms) | *Belum diukur (butuh microSD fisik)* | *Belum diukur (butuh microSD fisik)* | Ukur selisih `esp_timer_get_time()` sebelum dan sesudah `bb_storage_write_event()` untuk N=100, 500, 1000 sampel |
| Ukuran berkas CSV (bytes) | N/A | **411 bytes** (untuk 4 sampel + metadata + CRC-32; ~42 bytes per baris sampel tambahan) | Diukur langsung dari output `bb_storage_format_event_csv()` |
| Ukuran objek kompilasi (`.o` ESP32-S3) | N/A | `blackbox_storage_esp32s3.o`: **100.952 bytes** (dengan `-gdwarf-4 -ggdb` debug info) | Diukur dari hasil `xtensa-esp32s3-elf-gcc` (GCC 14.2.0) |
| Deteksi korupsi / file parsial | Contoh SDSPI standar tidak memiliki CRC atau filter `.TMP` | **100% terdeteksi** pada uji mutasi sampel (`ESP_ERR_INVALID_CRC`), pemotongan footer (`ESP_FAIL`), dan filter `.TMP` | Terverifikasi melalui `test_blackbox_storage_host.c` |

---

## 3. Bukti yang Tersimpan di Repositori

- [`evidence/build-and-test-log.txt`](../evidence/build-and-test-log.txt): Log terminal asli eksekusi `tests/run_tests.ps1` (cross-compile `esp32s3` + 7 suite unit test host).
- [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv): Contoh berkas output CSV `v1` hasil serialisasi data sintetis (**berlabel SIMULASI**).
- [`tests/run_tests.ps1`](../tests/run_tests.ps1) & [`tests/test_blackbox_storage_host.c`](../tests/test_blackbox_storage_host.c): Kode uji yang dapat dijalankan ulang kapan saja.
