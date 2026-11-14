# 04 — Handoff Bagian Storage untuk Presentasi Senin, 16 November 2026

> **Siap dipakai untuk presentasi progres.** Dokumen ini menyajikan secara jujur apa yang **sudah selesai diimplementasikan dan diverifikasi secara kompilasi/unit-test**, serta apa yang **masih menunggu perangkat keras fisik dan integrasi tim**.

---

## 1. Penjelasan 30 Detik (Untuk Ketua / Presenter)

> "Komponen `blackbox_storage` sudah selesai diimplementasikan dan di-hardening sebagai komponen reusable ESP-IDF (`blackbox_storage.h`, `blackbox_storage.c`, dan `CMakeLists.txt`). Komponen ini menangani mounting microSD via SDSPI dan FATFS VFS, mendukung mode bus SPI mandiri maupun berbagi bus dengan sensor ADXL345 lewat opsi `manage_spi_bus`, memakai mutex FreeRTOS statis serta pembacaan status *lock-free/atomic*, dan menyimpan rekaman event dalam format CSV `v1` lengkap dengan checksum CRC-32. Supaya komponen Interface (HTTP server) tidak pernah mengunduh file yang baru tertulis separuh, kami menerapkan siklus tulis dua tahap `.TMP` -> `fflush`/`fsync`/`fclose` -> `rename` menjadi `.CSV`. Kode sudah lulus **full build firmware ESP-IDF `v5.4.4` target `esp32s3`** tanpa warning/error (`embedded_blackbox.elf`, `embedded_blackbox.bin` `205.056` B, `bootloader.bin`, `partition-table.bin`) serta lulus **8 dari 8 suite unit, fault-injection, & concurrency test** pada host (MinGW GCC & `lcc64`). Yang masih belum diuji adalah perilaku kartu microSD fisik (**UNVERIFIED ON HARDWARE**), menunggu konfirmasi pin GPIO SPI, spesifikasi flash/PSRAM board ESP32-S3 tim, serta sinkronisasi kontrak integrasi (`blackbox_core` & `blackbox_interface`)."

---

## 2. Checklist Status Deliverable Storage

- [x] **Struktur proyek & konfigurasi ESP-IDF siap:** `CMakeLists.txt`, `sdkconfig.defaults` (`CONFIG_FATFS_FS_LOCK=16`, stack `6144`), `main/`, `components/blackbox_storage/`
- [x] **Source code komponen Storage lengkap & ter-hardening:**
  - [`components/blackbox_storage/include/blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h)
  - [`components/blackbox_storage/blackbox_storage.c`](../components/blackbox_storage/blackbox_storage.c)
  - [`components/blackbox_storage/CMakeLists.txt`](../components/blackbox_storage/CMakeLists.txt)
  - [`components/blackbox_storage/README.md`](../components/blackbox_storage/README.md)
- [x] **Contoh file output CSV berlabel simulasi (441 bytes, CRC `0x10BF5ABE`):** [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv)
- [x] **Cross-Compilation Objek Target `esp32s3` (ESP-IDF v5.4 SDK):** **PASS** (`xtensa-esp32s3-elf-gcc` 14.2.0 `-Wall -Wextra -Werror`, `0 warnings` — lihat [`evidence/build-and-test-log.txt`](../evidence/build-and-test-log.txt))
- [x] **Host Unit, Fault-Injection & Concurrency Tests:** **PASS (8/8 test suites lulus di MinGW GCC `-std=gnu11` & MATLAB `lcc64`)**
- [x] **Full Firmware Build (`idf.py build` pada ESP-IDF `v5.4.4`, `esp32s3`):** **PASS (`0 warnings/errors`)** — `embedded_blackbox.elf` (`3.804.372` B), `embedded_blackbox.bin` (`205.056` B; `80%` sisa di partisi `factory` `1M`), `bootloader.bin` (`20.928` B), `partition-table.bin` (`3.072` B)
- [ ] **Konfirmasi Spesifikasi Board & Hardware Fisik (ESP32-S3 + microSD):** **BELUM DIUJI (`UNVERIFIED ON HARDWARE`)** — menunggu board fisik, spesifikasi flash (`2MB` DIO default vs ukuran flash board tim & PSRAM), dan konfirmasi pin GPIO SPI (`MOSI`, `MISO`, `SCLK`, `CS`)
- [ ] **Sinkronisasi Remote ke GitHub (`https://github.com/Legorvey/embedded-blackbox.git`):** Commit fitur tersimpan lokal di branch `feat/blackbox-storage-hardening`; push menunggu izin manual pemilik repo

---

## 3. Panduan Tanya-Jawab Dosen Saat Presentasi

**Q1: Kenapa microSD diakses lewat SPI (SDSPI), dan bagaimana pembagian bus dengan ADXL345?**  
**Jawab:** Diagram spesifikasi tugas dosen menempatkan kartu SD pada jalur **SPI** bersama perangkat SPI lain (ADXL345). ESP-IDF v5.4 menyediakan driver resmi `sdspi_host` dan `esp_vfs_fat_sdspi_mount()` untuk menghubungkan FATFS melalui SPI. Karena tim belum memutuskan secara final apakah ADXL345 dan microSD memakai 1 bus SPI yang sama (`SPI2_HOST`) atau controller terpisah (`SPI2_HOST` dan `SPI3_HOST`), `blackbox_storage` sudah dirancang mendukung keduanya lewat opsi `cfg.manage_spi_bus` (`true` = bus dikelola Storage, `false` = bus berbagi sudah di-init pemilik eksternal dengan DMA aktif).

**Q2: Kenapa komponen Storage dipisah dari Core dan tidak langsung membaca sensor?**  
**Jawab:** Agar pembacaan sensor gerak (target 100 Hz) tidak terblokir oleh latensi penulisan kartu microSD yang tidak deterministik. `blackbox_core` mengumpulkan sampel di RAM dan menyerahkan *snapshot* event ke `blackbox_storage` untuk ditulis dari task berprioritas lebih rendah.

**Q3: Library apa yang dipelajari dan apa rekayasa yang ditambahkan oleh anggota Storage?**  
**Jawab:** Kami mempelajari API resmi ESP-IDF v5.4 (`esp_vfs_fat.h`, `driver/sdspi_host.h`, `sdmmc_cmd.h`) dan contoh referensi `storage/sd_card/sdspi`. Kami tidak memodifikasi kode internal driver FATFS bawaan Espressif, melainkan membangun komponen reusable `blackbox_storage` di atasnya dengan penambahan:
1. Dukungan kepemilikan bus SPI fleksibel (`manage_spi_bus`) untuk mengantisipasi berbagi bus SPI dengan ADXL345 serta *rollback* `spi_bus_free()` jika mount gagal.
2. Proteksi keamanan konfigurasi (`format_if_mount_failed = true` ditolak agar tidak menghapus kartu SD tanpa sengaja, dan pin default `GPIO_NUM_NC` wajib diisi eksplisit).
3. Siklus hidup file dua tahap (`E<id>.TMP` -> `E<id>.CSV`) agar server HTTP di `blackbox_interface` hanya melihat file yang sudah selesai.
4. Checksum integritas **CRC-32** pada footer `# END_EVENT` beserta fungsi verifikasi baca-balik (`bb_storage_verify_event_file`).

**Q4: Apakah data dijamin 100% aman kalau listrik cabut mendadak saat menulis?**  
**Jawab:** Tidak 100% bebas risiko korupsi FAT, dan itu kami dokumentasikan secara jujur. Mekanisme `.TMP` + `fflush`/`fsync` + `rename` ke `.CSV` beserta CRC-32 mencegah pembacaan file separuh pada operasi normal dan mendeteksi file yang terpotong, tetapi sistem berkas FAT32 sendiri bukan *journaling filesystem* yang tahan mati daya mendadak saat tabel cluster FAT sedang diperbarui.

---

## 4. Template Pesan Singkat ke Ketua Tim

> "Bro, bagian `blackbox_storage` gue udah siap diimplementasikan lengkap sama unit test dan dokumentasinya. Scope yang udah jalan: init/unmount SDSPI + FATFS VFS (support bus SPI sendiri maupun sharing sama ADXL345 lewat flag `manage_spi_bus`), format event CSV v1 + checksum CRC-32, proteksi file sementara `.TMP` -> `.CSV` biar Interface nggak baca file setengah matang, dan filter daftar file selesai (`bb_storage_list_completed_events`).
>
> Status pengujian:
> 1. **Full firmware build ESP-IDF `v5.4.4` (`idf.py build`, target `esp32s3`):** **PASS (`0 warnings/errors`)** — menghasilkan `embedded_blackbox.elf`, `embedded_blackbox.bin` (`205.056` B; `80%` sisa di partisi `factory` `1M`), `bootloader.bin` (`20.928` B), dan `partition-table.bin` (`3.072` B); objek `blackbox_storage_esp32s3.o` (`147.728` B) & `main_esp32s3.o` (`15.004` B) juga lulus `-Wall -Wextra -Werror` di `tests/run_tests.ps1`.
> 2. **Host unit, fault-injection & concurrency test (`tests/run_tests.ps1`):** **PASS (8/8 test suites lulus di MinGW GCC 4.9.2 & MATLAB `lcc64`, data simulasi host)**.
> 3. **Hardware microSD fisik & konfigurasi board:** **UNVERIFIED ON HARDWARE** — menunggu konfirmasi pin GPIO SPI (`MOSI`, `MISO`, `SCLK`, `CS`, masih `GPIO_NUM_NC`), spesifikasi flash/PSRAM board (saat ini default IDF `2MB` DIO), serta sinkronisasi aturan akses `vfs_fat` dengan Interface & SPI ADXL345.
>
> Kontrak API buat Core & Interface udah gue tulis rapi di `docs/02-KONTRAK-INTEGRASI.md`, bukti log ada di `docs/03-PENGUJIAN-DAN-BUKTI.md` & `evidence/`, dan poin presentasi 30 detik buat hari Senin udah gue taruh di `docs/04-HANDOFF-PRESENTASI.md`."
