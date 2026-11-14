# 05 — Decision Log dan Pertanyaan Integrasi untuk Ketua Tim

> Dokumen ini mencatat keputusan teknis yang telah diambil untuk komponen `blackbox_storage` agar pengembangan tidak terhambat, sekaligus mencantumkan pertanyaan yang memerlukan konfirmasi ketua/tim pada tahap integrasi.

---

## 1. Log Keputusan Teknis (`blackbox_storage`)

| Topik | Status Saat Ini | Keputusan yang Diambil di `blackbox_storage` | Tindak Lanjut Tim |
|---|---|---|---|
| Kepemilikan `blackbox_storage` | **Aktif dikerjakan** | Komponen `components/blackbox_storage/` telah diimplementasikan lengkap beserta unit test host | Konfirmasi pembagian final saat sinkronisasi repo tim |
| Versi ESP-IDF & Target | **Terverifikasi lokal (`esp32s3`, ESP-IDF `v5.4.4`)** | Diuji melalui full build ESP-IDF `v5.4.4` (`C:\Espressif\v5.4.4\esp-idf`, GCC `14.2.0`, CMake `3.30.2`, Ninja `1.12.1`) serta header SDK `release/v5.4` (`2f7dcd862a`) di `run_tests.ps1` | Selaraskan versi ESP-IDF `v5.4.4` ke seluruh anggota tim saat integrasi |
| Pin GPIO SPI (`MOSI`, `MISO`, `SCLK`, `CS`) & Host Controller | **Belum dikonfirmasi dari hardware tim (`TODO`)** | Default di `BB_STORAGE_CONFIG_DEFAULT()` dan `main/main.c` disetel ke `GPIO_NUM_NC` (`-1`); `bb_storage_init()` memvalidasi `SPI2_HOST`/`SPI3_HOST` dan nomor GPIO sah ESP32-S3 (`GPIO_IS_VALID_*`) | Tetapkan pin setelah board ESP32-S3 fisik dan wiring disepakati |
| Berbagi Bus SPI antara microSD dan ADXL345 | **Belum diputuskan tim** | Disediakan opsi `cfg.manage_spi_bus`: `true` jika Storage mengelola bus SPI sendiri, `false` jika bus SPI sudah diinisialisasi pemilik eksternal (wajib DMA aktif & `max_transfer_sz >= 4000`) | Koordinasi dengan Anggota 2 (ADXL345) dan Anggota 5 (Core) |
| Kebijakan Format Kartu SD | **Keputusan Keamanan (Locked)** | `format_if_mount_failed` wajib `false`; jika disetel `true`, `bb_storage_init()` menolak dengan `ESP_ERR_INVALID_ARG` demi mencegah penghapusan data kartu SD | Siapkan kartu microSD yang sudah diformat FAT32 dari PC |
| Konfigurasi Build & FATFS VFS (`sdkconfig.defaults`) | **Terverifikasi di `idf.py build` (`v5.4.4`)** | `mount_point` dibatasi maks `13` char (`ESP_VFS_PATH_MAX - 2`, selaras `vfs_fat.c` `strlcpy(..., sizeof(base_path) - 1)`); `CONFIG_FATFS_FS_LOCK=16` dan `CONFIG_ESP_MAIN_TASK_STACK_SIZE=6144` diaktifkan dan cocok 1:1 di `sdkconfig` | Konfirmasikan ukuran flash (saat ini default IDF `2MB` DIO), partisi (`Single factory 1M`), & PSRAM sesuai board fisik tim sebelum flashing |
| Format Berkas Event MVP | **Ditetapkan Provisional (`CSV v1` + CRC-32)** | Menggunakan nama berkas 8.3 (`E0000001.TMP` -> `E0000001.CSV`), header metadata `# BLACKBOX_EVENT_FORMAT,v1`, satuan integer `mg` & `mdps`, serta footer `# END_EVENT` dengan CRC-32; self-test memakai `BBTEST00.TMP`/`.CSV` | Minta persetujuan Anggota 5 (Core) dan Anggota 4 (Interface) atas [`docs/02-KONTRAK-INTEGRASI.md`](02-KONTRAK-INTEGRASI.md) |
| Pembuatan Task FreeRTOS & Thread-Safety | **Keputusan Arsitektur** | `blackbox_storage` memakai mutex FreeRTOS statis (`xSemaphoreCreateMutexStatic`) dan pembacaan status *lock-free/atomic*; `bb_storage_write_event()` dipanggil sinkron dari writer task milik Core | Koordinasi prioritas task writer & kedalaman queue dengan Anggota 5 (Core) |
| Interpretasi "Edit & Reduce Library" dari Dosen | **Perlu Konfirmasi Dosen/Ketua** | Diimplementasikan sebagai komponen reusable `.h`/`.c` di atas driver resmi ESP-IDF (`esp_vfs_fat` + `sdspi_host`), sejalan dengan contoh `nn2_wifi.h`/`.c` pada slide dosen | Konfirmasi ke dosen apakah komponen wrapper/adapter di atas ESP-IDF sudah memenuhi ekspektasi |

---

## 2. Daftar Pertanyaan Singkat untuk Ketua & Anggota Tim

1. **Untuk Ketua & Seluruh Tim (Hardware Pinout & Spesifikasi Board):**  
   Board ESP32-S3 varian apa yang akhirnya dipakai tim (ukuran Flash 4/8/16 MB, mode SPI Flash QIO/DIO/OPI, PSRAM ada/tidak, butuh partisi > 1 MB/OTA untuk Wi-Fi+HTTP atau tidak), dan berapa nomor GPIO untuk `MOSI`, `MISO`, `SCLK`, serta `CS` microSD?
2. **Untuk Anggota 2 (`blackbox_imu_spi` — ADXL345) & Anggota 5 (`blackbox_core`):**  
   Apakah microSD dan ADXL345 jadi memakai 1 bus SPI yang sama (`SPI2_HOST`) atau dipisah (`SPI2_HOST` dan `SPI3_HOST`)? Jika berbagi 1 bus, pastikan pin CS ADXL345 ditarik *HIGH* sebelum `bb_storage_init()` dipanggil dengan `manage_spi_bus = false`, dan pertimbangkan apakah latensi tulis microSD (`poll_busy()`) memengaruhi sampling 100 Hz ADXL345.
3. **Untuk Anggota 5 (`blackbox_core`):**  
   Apakah struktur `bb_storage_sample_t` (satuan integer `mg` dan `mdps`, `timestamp_us`, `offset_ms`) dan batas `BB_STORAGE_MAX_SAMPLES = 2048` di [`blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h) sudah cocok dengan *rolling buffer* Core? (Catatan: task writer Core disarankan stack minimal `>= 4096-6144 B`.)
4. **Untuk Anggota 4 (`blackbox_interface`):**  
   Apakah daftar file dari `bb_storage_list_completed_events()` (urutan slot FAT, dua-tahap `NULL, 0, &total` agar tidak terpotong diam-diam di `max_files`) dan kontrak penutupan stream HTTP sebelum `bb_storage_deinit()` (tanpa `opendir()`/`stat()` langsung pada `vfs_fat`) sudah mencukupi, atau perlu helper baca terkelola?
