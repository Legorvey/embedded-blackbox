# 04 — Handoff Bagian Storage untuk Presentasi Senin, 16 November 2026

> **Siap dipakai untuk presentasi progres.** Dokumen ini menyajikan secara jujur apa yang **sudah selesai diimplementasikan dan diverifikasi secara kompilasi/unit-test**, serta apa yang **masih menunggu perangkat keras fisik dan integrasi tim**.

---

## 1. Penjelasan 30 Detik (Untuk Ketua / Presenter)

> "Komponen `blackbox_storage` sudah selesai diimplementasikan sebagai komponen reusable ESP-IDF (`blackbox_storage.h`, `blackbox_storage.c`, dan `CMakeLists.txt`). Komponen ini menangani mounting microSD via SDSPI dan FATFS VFS, mendukung mode bus SPI mandiri maupun berbagi bus dengan sensor ADXL345, serta menyimpan rekaman event dalam format CSV berversi lengkap dengan checksum CRC-32. Supaya komponen Interface (HTTP server) tidak pernah mengunduh file yang baru tertulis separuh, kami menerapkan mekanisme tulis dua tahap: data ditulis dulu ke file sementara `.TMP`, di-*flush*, lalu di-*rename* menjadi `.CSV` setelah utuh. Kode sudah lulus *cross-compilation* untuk target **ESP32-S3 (ESP-IDF v5.4)** tanpa warning/error dan lulus **7 dari 7 pengujian unit & fault-injection di host**. Pengujian pada kartu microSD fisik berstatus *UNVERIFIED ON HARDWARE* menunggu penetapan pin GPIO board ESP32-S3 tim."

---

## 2. Checklist Status Deliverable Storage

- [x] **Struktur proyek ESP-IDF siap:** `CMakeLists.txt`, `sdkconfig.defaults`, `main/`, `components/blackbox_storage/`
- [x] **Source code komponen Storage lengkap:**
  - [`components/blackbox_storage/include/blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h)
  - [`components/blackbox_storage/blackbox_storage.c`](../components/blackbox_storage/blackbox_storage.c)
  - [`components/blackbox_storage/CMakeLists.txt`](../components/blackbox_storage/CMakeLists.txt)
  - [`components/blackbox_storage/README.md`](../components/blackbox_storage/README.md)
- [x] **Contoh file output CSV berlabel simulasi:** [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv)
- [x] **Hasil Cross-Compilation Target `esp32s3` (ESP-IDF v5.4 SDK):** **PASS** (`xtensa-esp32s3-elf-gcc` 14.2.0 dengan `-Wall -Wextra -Werror`, 0 warnings, 0 errors — lihat [`evidence/build-and-test-log.txt`](../evidence/build-and-test-log.txt))
- [x] **Hasil Unit Test & Fault-Injection di Host:** **PASS (7/7 test suites lulus)**
- [ ] **Hasil Uji di Hardware Fisik (ESP32-S3 + microSD):** **BELUM DIUJI (`UNVERIFIED ON HARDWARE`)** — menunggu board fisik dan konfirmasi pin GPIO SPI (`MOSI`, `MISO`, `SCLK`, `CS`)
- [ ] **Commit & Push ke GitHub (`https://github.com/Legorvey/embedded-blackbox.git`):** Menunggu review dan eksekusi manual oleh pemilik Storage

---

## 3. Panduan Tanya-Jawab Dosen Saat Presentasi

**Q1: Kenapa microSD diakses lewat SPI (SDSPI), bukan SDMMC 4-bit?**  
**Jawab:** Karena diagram spesifikasi tugas dosen secara eksplisit meminta SD card terhubung melalui bus **SPI** bersama sensor SPI. ESP-IDF v5.4 menyediakan driver resmi `sdspi_host` dan `esp_vfs_fat_sdspi_mount()` yang menjembatani bus SPI dengan sistem berkas FATFS.

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
> 1. Cross-compile target `esp32s3` pakai `xtensa-esp32s3-elf-gcc` (ESP-IDF v5.4 headers, `-Wall -Wextra -Werror`): **PASS (0 warnings/errors)**.
> 2. Host unit & fault-injection test (`tests/run_tests.ps1`): **PASS (7/7 test suites lulus)**.
> 3. Hardware microSD fisik: **UNVERIFIED ON HARDWARE** karena pin GPIO SPI board kita (`MOSI`, `MISO`, `SCLK`, `CS`) belum dikunci.
>
> Kontrak API buat Core & Interface udah gue tulis rapi di `docs/02-KONTRAK-INTEGRASI.md`, bukti log ada di `docs/03-PENGUJIAN-DAN-BUKTI.md` & `evidence/`, dan poin presentasi 30 detik buat hari Senin udah gue taruh di `docs/04-HANDOFF-PRESENTASI.md`."
