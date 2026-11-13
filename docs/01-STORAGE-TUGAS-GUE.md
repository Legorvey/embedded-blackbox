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
7. **Pengujian kompilasi objek ESP32-S3 & unit test host**: **PASS (8/8 suite host & 0 warning cross-compile `.o`)**; namun **full link firmware `idf.py build` (`.elf`/`.bin`) berstatus NOT RUN** (belum ada CLI `idf.py`/CMake/Ninja standalone di mesin ini), dan pengujian di microSD fisik berstatus **UNVERIFIED ON HARDWARE** menunggu konfirmasi pin GPIO board tim.

## Matriks 6 Tingkat Status Verifikasi

| Tingkat | Status | Penjelasan Jujur |
|---|---|---|
| 1. Fungsionalitas terimplementasi | **SELESAI** | API, SDSPI/FATFS mount, CSV v1, CRC-32, `.TMP` -> `.CSV`, static mutex FreeRTOS, lock-free status, strict verifier, `BBTEST00` self-test |
| 2. Teruji unit test di host | **PASS (8/8)** | Dijalankan via `tests/run_tests.ps1` pada MinGW GCC (`-Wall -Wextra -Werror`) dan MATLAB `lcc64` |
| 3. Terkompilasi silang (`esp32s3` objek) | **PASS** | `xtensa-esp32s3-elf-gcc` 14.2.0 terhadap header ESP-IDF v5.4 menghasilkan `.o` (`0 warnings`) |
| 4. Firmware ESP-IDF ter-*link* penuh (`.elf`/`.bin`) | **NOT RUN** | Belum ada `idf.py`/CMake/Ninja/ekstensi VS Code ESP-IDF terpasang di mesin lokal |
| 5. Perilaku hardware fisik | **UNVERIFIED ON HARDWARE** | Belum ada ESP32-S3 terhubung di port serial & pin GPIO tim masih `GPIO_NUM_NC` |
| 6. Pekerjaan tersisa / integrasi | **PENDING** | Build `idf.py` penuh, penetapan pin GPIO SPI, dan kesepakatan kontrak dengan Core, ADXL345, serta Interface |

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
| S2 — komponen reusable | `blackbox_storage.h`, `blackbox_storage.c`, `CMakeLists.txt` lulus cross-compile objek `esp32s3` (`0 warnings/errors`); full `idf.py build` `.elf`/`.bin` **NOT RUN** | **PASS (CROSS-COMPILE OBJEK) — LINK `idf.py` NOT RUN** |
| S3 — file event + status | Serialisasi CSV `v1`, CRC-32 `0x10BF5ABE`, `.TMP` -> `.CSV`, hardening `fsync`/parser ketat/mutex statis, & **8/8** unit/fault/concurrency test lulus | **PASS (HOST UNIT TEST 8/8)** |
| S4 — integrasi & pengukuran | Kontrak API didokumentasikan di `docs/02-KONTRAK-INTEGRASI.md`; menunggu integrasi dengan Core, Interface, dan hardware | **INTEGRATION PENDING** |

## Kasus gagal yang sudah ditangani & di-hardening di kode

- **Pin belum dikonfigurasi (`GPIO_NUM_NC`), pin invalid ESP32-S3 (`22..25`, `>=49`), host salah (`SPI1_HOST`), atau `format_if_mount_failed = true`**: Ditolak langsung oleh `bb_storage_init()` dengan `ESP_ERR_INVALID_ARG`.
- **SD card tidak dipasang saat boot / mount gagal / unmount gagal**: Pada mount gagal, `bb_storage_init()` melepas bus SPI yang dibuatnya (`spi_bus_free`). Jika `bb_storage_deinit()` gagal unmount, state tetap *initialized* agar kepemilikan bus tidak bocor dan `deinit()` bisa di-retry.
- **SD penuh (`ENOSPC`) atau gagal tulis (`EIO`/`fsync`/rename)**: Nilai balik `fopen`, `fputs`, `fflush`, `fsync`, `fclose`, dan `rename` diperiksa semua; `errno` ditangkap sebelum logging; file `.TMP` dihapus otomatis; status berubah ke `FULL` (`ESP_ERR_NO_MEM`) atau `WRITE_ERROR` (`ESP_FAIL`), dan pulih kembali ke `READY` jika tulis berikutnya berhasil.
- **Korupsi data / baris invalid / file terpotong**: Parser ketat (tanpa `sscanf`) pada `bb_storage_verify_event_file()` menolak baris melebihi batas, junk pasca-footer, kolom tambahan, integer overflow 32-bit, maupun ketidaksesuaian `# sample_count` & CRC-32 (`ESP_ERR_INVALID_CRC`).
- **Akses konkuren & `self_test` non-destruktif**: Memakai mutex FreeRTOS statis (`xSemaphoreCreateMutexStatic`), pembacaan status *lock-free/atomic* di `bb_storage_get_status()`, filter direktori palsu `E*.CSV/` di `bb_storage_list_completed_events()`, dan `bb_storage_self_test()` memakai file khusus `BBTEST00.TMP`/`BBTEST00.CSV` di bawah lock agar tidak bertabrakan dengan event nyata (`BB_STORAGE_MAX_EVENT_ID`).

## Referensi terbaik yang digunakan

- Contoh resmi: https://github.com/espressif/esp-idf/tree/v5.4/examples/storage/sd_card/sdspi
- API FAT/VFS: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/storage/fatfs.html
- SPI shared-bus: https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/sdspi_share.html
