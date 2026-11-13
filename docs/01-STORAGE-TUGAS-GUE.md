# 01 — Bagian gue: `blackbox_storage`

## Dalam satu kalimat

**Terima rekaman event yang sudah disiapkan Core, simpan ke microSD secara terukur dan dapat diverifikasi, lalu laporkan hasilnya.** Kamu tidak bertanggung jawab atas pembacaan sensor/deteksi event, web UI atau driver SPI ADXL345, walau harus koordinasi dengannya.

## Status Implementasi Saat Ini

1. **Inisialisasi microSD via SPI & mount FATFS (`/sdcard`)**: **SUDAH DIIMPLEMENTASIKAN** (`bb_storage_init`, `bb_storage_deinit`) menggunakan `esp_vfs_fat_sdspi_mount()` dengan dukungan mode kepemilikan bus sendiri (`manage_spi_bus = true`) maupun berbagi bus (`manage_spi_bus = false`).
2. **Tulis file data uji, `fflush`/`fclose`, baca balik & verifikasi**: **SUDAH DIIMPLEMENTASIKAN** (`bb_storage_self_test()`, `bb_storage_verify_event_file()`).
3. **Komponen ESP-IDF reusable**: **SUDAH DIIMPLEMENTASIKAN** (`components/blackbox_storage/include/blackbox_storage.h`, `components/blackbox_storage/blackbox_storage.c`, `components/blackbox_storage/CMakeLists.txt`, `components/blackbox_storage/README.md`).
4. **Format file event berversi (`v1`) + CRC-32**: **SUDAH DIIMPLEMENTASIKAN** (`bb_storage_format_event_csv`, `bb_storage_write_event`) menggunakan format CSV berversi (`# BLACKBOX_EVENT_FORMAT,v1`) dan footer `# END_EVENT,count=...,crc32=0x...`.
5. **Status & penanganan error tanpa auto-format**: **SUDAH DIIMPLEMENTASIKAN** (`BB_STORAGE_STATUS_UNINITIALIZED`, `READY`, `MOUNT_FAILED`, `WRITE_ERROR`, `FULL`; `format_if_mount_failed = true` ditolak dengan `ESP_ERR_INVALID_ARG`).
6. **Pemisahan file belum selesai vs selesai untuk Interface**: **SUDAH DIIMPLEMENTASIKAN** menggunakan penulisan dua tahap (`E<id>.TMP` -> `rename` ke `E<id>.CSV`) dan fungsi `bb_storage_list_completed_events()` / `bb_storage_is_completed_event_filename()`.
7. **Pengujian kompilasi ESP32-S3 & unit test host**: **LULUS (PASS)**; pengujian fisik di hardware microSD masih berstatus **UNVERIFIED ON HARDWARE** menunggu konfirmasi pin GPIO board tim.

## Batas pekerjaan

- **Kamu (`blackbox_storage`)**: mount/unmount (atau berkolaborasi dengan pemilik bus), serialisasi & file record, listing file yang selesai (`E*.CSV`), status/error/statistik, dan tes Storage.
- **Anggota Core (`blackbox_core`)**: mengekstrak rekaman pre/post event, mengelola buffer histori/algoritma, dan membuat task/queue yang memanggil `bb_storage_write_event()` di luar task sampling kritis.
- **Anggota Interface (`blackbox_interface`)**: SoftAP, HTTP, halaman dan download; memakai `bb_storage_list_completed_events()` agar hanya mengakses file `.CSV` yang sudah difinalisasi.
- **Anggota ADXL345 (`blackbox_imu_spi`)**: sensor SPI; wajib sepakat pemakaian host SPI/CS/init order jika berbagi bus dengan microSD (`manage_spi_bus = false`).

## Status 5 Milestone Storage

| Tahap | Bukti aktual | Status saat ini |
|---|---|---|
| S0 — identifikasi ESP-IDF & hardware | Toolchain `xtensa-esp32s3-elf-gcc` 14.2.0 & SDK ESP-IDF v5.4 (`esp32s3`) teridentifikasi; pin GPIO fisik belum dikonfirmasi tim | **SEBAGIAN TERVERIFIKASI (Pin Fisik TODO)** |
| S1 — mount & tes file microSD | Kode `bb_storage_init` & `bb_storage_self_test` siap; diuji dengan simulasi host, belum dijalankan di board fisik | **KODE SIAP — UNVERIFIED ON HARDWARE** |
| S2 — komponen reusable | `blackbox_storage.h`, `blackbox_storage.c`, `CMakeLists.txt` lulus cross-compile `esp32s3` (`0 warnings/errors`) | **PASS (TERVERIFIKASI KOMPILASI)** |
| S3 — file event + status | Serialisasi CSV `v1`, CRC-32, `.TMP` -> `.CSV`, dan 7 suite unit/fault-injection test lulus (`7/7 PASS`) | **PASS (HOST UNIT TEST)** |
| S4 — integrasi & pengukuran | Kontrak API didokumentasikan di `docs/02-KONTRAK-INTEGRASI.md`; menunggu integrasi dengan Core, Interface, dan hardware | **INTEGRATION PENDING** |

## Kasus gagal yang sudah ditangani di kode

- **Pin belum dikonfigurasi (`GPIO_NUM_NC`) atau `format_if_mount_failed = true`**: Ditolak langsung oleh `bb_storage_init()` dengan `ESP_ERR_INVALID_ARG`.
- **SD card tidak dipasang saat boot / mount gagal**: `bb_storage_init()` mengembalikan kode error, melepas bus SPI jika sudah telanjur diinisialisasi (`spi_bus_free`), dan menyetel status `BB_STORAGE_STATUS_MOUNT_FAILED` tanpa membuat sistem crash.
- **SD penuh (`ENOSPC`) atau gagal ditulis (`EIO`)**: Nilai balik `fprintf`, `fflush`, `fclose`, dan `rename` diperiksa; file `.TMP` otomatis dihapus agar tidak meninggalkan sampah parsial, dan status berubah ke `BB_STORAGE_STATUS_FULL` atau `BB_STORAGE_STATUS_WRITE_ERROR`.
- **Korupsi data / file terpotong**: `bb_storage_verify_event_file()` memeriksa keberadaan footer `# END_EVENT`, kecocokan jumlah baris sampel, dan menghitung ulang checksum CRC-32 (`ESP_ERR_INVALID_CRC` bila ada byte yang berubah).
- **Sudden power loss**: Didokumentasikan secara jujur bahwa `.TMP` + `rename` menjaga visibilitas file saat operasi normal, tetapi FATFS tidak menjamin keutuhan tabel FAT bila listrik cabut mendadak di tengah penulisan sektor fisik.

## Referensi terbaik yang digunakan

- Contoh resmi: https://github.com/espressif/esp-idf/tree/v5.4/examples/storage/sd_card/sdspi
- API FAT/VFS: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/storage/fatfs.html
- SPI shared-bus: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/sdspi_share.html
