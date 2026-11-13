# 03 — Rencana Tes dan Bukti Nyata `blackbox_storage`

> Dokumen ini mencatat **hasil pengujian aktual yang benar-benar dijalankan** pada tanggal 13 November 2026, serta memisahkan dengan jujur mana yang sudah lulus (**PASS**) dan mana yang belum diuji di perangkat keras fisik (**UNVERIFIED ON HARDWARE**).

---

## 1. Tabel Hasil Pengujian (`blackbox_storage`)

| ID | Pengujian | Cara Uji / Perintah | Hasil / Bukti Aktual | Status |
|---|---|---|---|---|
| **T0** | Pemeriksaan Toolchain & SDK ESP-IDF | Cek `idf.py --version` dan lokasi toolchain `xtensa-esp32s3-elf-gcc` | `idf.py` standalone / CMake / Ninja belum terpasang di `PATH` atau VS Code extension; compiler `xtensa-esp32s3-elf-gcc` (`14.2.0_20241119`) dan SDK header/lib ESP-IDF `release/v5.4` (`2f7dcd862a`) tersedia di `Arduino15/packages/esp32`. | **PARTIAL (SDK & Compiler Tersedia; `idf.py` CLI Tidak Tersedia)** |
| **T1** | Cross-Compilation Komponen untuk Target `esp32s3` | `powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_tests.ps1` (Step 1: `xtensa-esp32s3-elf-gcc -Wall -Wextra -Werror`) | `blackbox_storage.c` -> `blackbox_storage_esp32s3.o` (`147.784` bytes), `main.c` -> `main_esp32s3.o` (`15.004` bytes), `test_blackbox_storage_host.c` -> `test_blackbox_storage_host_check.o` (`81.616` bytes) dengan **0 warnings, 0 errors**. Lihat [`evidence/build-and-test-log.txt`](../evidence/build-and-test-log.txt). | **PASS (CROSS-COMPILE OBJEK — FULL `idf.py build` NOT RUN)** |
| **T2** | Validasi Konfigurasi & Guard Keamanan | `test_config_validation` (`MinGW GCC 4.9.2` & `MATLAB lcc64`) | Menolak `NULL`, `GPIO_NUM_NC`, pin non-ESP32-S3 (`22..25`, `49`), `SPI1_HOST`/`SPI_HOST_MAX`, `format_if_mount_failed=true`, path invalid/whitespace/traversal, `max_open_files`/`max_freq_khz` di luar batas, duplikasi & kolisi CS saat shared bus; menerima `SPI3_HOST` & `manage_spi_bus=false`. | **PASS (HOST UNIT TEST — GCC & lcc64)** |
| **T3** | Lifecycle, Rollback Bus SPI, & Gagal Unmount | `test_lifecycle_and_mount_failures` (`MinGW GCC` & `lcc64`) | `fail_spi_bus` tidak memanggil mount/free; `fail_mount` me-rollback `spi_bus_free()` hanya bila `manage_spi_bus=true`; `fail_unmount` menjaga status `READY` & kepemilikan bus untuk retry tanpa kebocoran resource. | **PASS (FAULT INJECTION HOST)** |
| **T4** | Write, Readback, Nilai Ekstrem, 2048 Sampel & Korupsi CSV | `test_write_verify_and_corruption_detection` + `bb_storage_self_test()` | Event batas `INT32_MIN`/`INT32_MAX`/`UINT64_MAX`, event besar **2048 sampel**, dan akhiran CRLF lulus verifikasi (`ESP_OK`). Mutasi CRC -> `ESP_ERR_INVALID_CRC`; file terpotong, junk header/row, post-footer, baris >384B, dan overflow `INT32_MAX+1` ditolak (`ESP_FAIL`). | **PASS (SIMULASI HOST) — UNVERIFIED ON HARDWARE** |
| **T5** | Serialisasi Event Simulasi (CSV `v1` & Golden CRC) | `test_csv_serialization_and_crc32` & verifikasi langsung [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv) | Menghasilkan CSV `v1` sepanjang **441 bytes** dengan footer `# END_EVENT,count=4,crc32=0x10BF5ABE`; file simulasi diverifikasi langsung oleh `bb_storage_verify_event_file()` -> `ESP_OK`. | **PASS (SIMULASI HOST)** |
| **T6** | Injeksi Fault Single-Point (`fopen`–`fsync`–`rename`/`ENOSPC`) & Pemulihan | `test_fault_injection_recovery_and_listing` | 11 titik kegagalan (`fopen`, header, mid-write `ENOSPC`/IO, footer, `fflush`, `fsync`, `fclose`, `rename`, `rename_enospc`) membersihkan `.TMP`, menyetel status `FULL`/`WRITE_ERROR`, lalu pulih ke `READY` saat tulis berikutnya berhasil. | **PASS (FAULT INJECTION HOST) — UNVERIFIED ON HARDWARE** |
| **T7** | Pemisahan File `.TMP`, Filter Direktori, & `self_test` (`BBTEST00`) | `test_paths_and_filename_predicate` & `test_fault_injection_recovery_and_listing` | Mengabaikan `.TMP`, `BBTEST00.CSV`, dan direktori palsu `E00000BB.CSV/`; `bb_storage_self_test()` mempertahankan `events_written` & tidak mengganggu event `EFFFFFFF.CSV` (`BB_STORAGE_MAX_EVENT_ID`). | **PASS (HOST UNIT TEST)** |
| **T8** | Uji Konkurensi Multi-Thread (4 Thread Simultan) | `test_concurrent_multithreaded_access` (`MinGW GCC` & `lcc64`) | 2 thread writer (32 event total), 1 thread `self_test`, & 1 thread reader (`get_status`, `get_stats`, `list_completed_events`) berjalan paralel dengan **0 error**, seluruh 32 file lulus verifikasi CRC-32. | **PASS (HOST CONCURRENCY — FREERTOS TARGET NOT RUN)** |
| **T9** | Full Link Firmware (`idf.py build` -> `.elf` / `.bin`) | `idf.py build` di ESP-IDF environment | Tidak dijalankan karena `idf.py`, CMake, Ninja, & VS Code ESP-IDF extension tidak terpasang lokal. | **NOT RUN (TOOLING BELUM TERPASANG)** |
| **T10** | Mount microSD Fisik & Uji Hardware di ESP32-S3 | Flash & jalankan di ESP32-S3 dengan modul microSD terhubung | Port serial COM kosong & pinout GPIO SPI tim (`MOSI`, `MISO`, `SCLK`, `CS`) masih belum ditetapkan (`GPIO_NUM_NC`). | **NOT RUN (UNVERIFIED ON HARDWARE)** |

---

## 2. Tabel Metrik Kinerja (Baseline vs Pengembangan)

> Sesuai aturan integritas akademik, angka latensi perangkat keras **tidak dikarang** sebelum board ESP32-S3 dan kartu microSD fisik diuji.

| Metrik | Baseline Referensi | Pengembangan (`blackbox_storage`) | Status / Cara Ukur Saat Hardware Tersedia |
|---|---|---|---|
| Write latency (ms) | *Belum diukur (butuh microSD fisik)* | *Belum diukur (butuh microSD fisik)* | Ukur selisih `esp_timer_get_time()` sebelum dan sesudah `bb_storage_write_event()` untuk N=100, 500, 2048 sampel |
| Ukuran berkas CSV (bytes) | N/A | **441 bytes** (untuk 4 sampel + metadata + CRC-32 `0x10BF5ABE`; diukur juga hingga 2048 sampel >50 KB) | Diukur langsung dari output `bb_storage_format_event_csv()` dan berkas aktual `example-event-simulated.csv` |
| Ukuran objek kompilasi (`.o` ESP32-S3) | N/A | `blackbox_storage_esp32s3.o`: **147.784 bytes**; `main_esp32s3.o`: **15.004 bytes** (GCC 14.2.0 ESP-IDF v5.4 `-Wall -Wextra -Werror`) | Diukur dari hasil `tests/run_tests.ps1` |
| Deteksi korupsi / file parsial | Contoh SDSPI standar tidak memiliki CRC atau filter `.TMP` | **100% terdeteksi pada 8/8 suite host** (mutasi CRC, trunc, junk, overflow, `.TMP`, direktori `E*.CSV/`) | Terverifikasi pada MinGW GCC 4.9.2 (`-std=gnu11`) & MATLAB `lcc64` |

---

## 3. Bukti yang Tersimpan di Repositori

- [`evidence/build-and-test-log.txt`](../evidence/build-and-test-log.txt): Log terminal asli eksekusi `tests/run_tests.ps1` (cross-compile objek `esp32s3` + 8/8 suite unit/fault/concurrency test pada MinGW GCC & `lcc64`).
- [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv): Contoh berkas output CSV `v1` (`441 bytes`, CRC-32 `0x10BF5ABE`) hasil serialisasi data sintetis (**berlabel SIMULASI**), terverifikasi lulus `bb_storage_verify_event_file()`.
- [`tests/run_tests.ps1`](../tests/run_tests.ps1) & [`tests/test_blackbox_storage_host.c`](../tests/test_blackbox_storage_host.c): Suite uji otomatis yang dapat dijalankan ulang kapan saja.
