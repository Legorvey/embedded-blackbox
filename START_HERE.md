# START HERE — Embedded Black Box / Storage

> **Untuk pemilik komponen `blackbox_storage`.** Repositori ini (`https://github.com/Legorvey/embedded-blackbox.git`) sudah berisi implementasi komponen `components/blackbox_storage/`, suite pengujian host/cross-compile di `tests/`, bukti verifikasi di `evidence/`, serta dokumentasi teknis di `docs/`. Firmware telah **lulus full build ESP-IDF `v5.4.4` (`esp32s3`)**, namun tetap jaga kejujuran status pengujian: **jangan mengklaim pengujian hardware microSD fisik sudah lulus sebelum benar-benar dijalankan pada board ESP32-S3 (`UNVERIFIED ON HARDWARE`).**
>
> **Jika kamu rekan tim (`Core`, `ADXL345 SPI`, `MPU6050 I2C`, `Interface`, atau Ketua Tim):** mulai dari [**`TEAM_HANDOFF.md`**](TEAM_HANDOFF.md) sebagai titik masuk utama onboarding dan serah terima tim.

## Urutan praktis untuk mempelajari & melanjutkan repo ini

1. **Pahami batas ruang lingkup & keamanan** di [`AGENTS.md`](AGENTS.md) dan [`README.md`](README.md).
2. **Baca dokumentasi komponen & tim** (15–20 menit):
   - [`docs/00-PETA-KONTEKS.md`](docs/00-PETA-KONTEKS.md) — sumber tugas dosen vs rancangan kelompok.
   - [`docs/01-STORAGE-TUGAS-GUE.md`](docs/01-STORAGE-TUGAS-GUE.md) — scope kerja & status milestone `S0–S4`.
   - [`docs/02-KONTRAK-INTEGRASI.md`](docs/02-KONTRAK-INTEGRASI.md) — kontrak API ke `blackbox_core` & `blackbox_interface`.
   - [`docs/06-BELAJAR-STORAGE.md`](docs/06-BELAJAR-STORAGE.md) — materi belajar SPI, SDSPI, FATFS, mutex FreeRTOS, dan alur `.TMP` -> `.CSV`.
3. **Jalankan verifikasi lokal** kapan saja melalui PowerShell di root proyek:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_tests.ps1
   ```
   Perintah ini memverifikasi kompilasi objek target `esp32s3` (`xtensa-esp32s3-elf-gcc` + header ESP-IDF v5.4) serta **8 suite unit, fault-injection, & concurrency test** di host.
4. **Cek pertanyaan integrasi yang masih terbuka** di [`docs/05-KEPUTUSAN-DAN-PERTANYAAN.md`](docs/05-KEPUTUSAN-DAN-PERTANYAAN.md) sebelum sinkronisasi dengan Core, ADXL345 (`blackbox_imu_spi`), dan Interface.
5. **Saat serah terima / presentasi** ke tim, arahkan anggota tim ke pintu masuk utama [`TEAM_HANDOFF.md`](TEAM_HANDOFF.md), serta gunakan naskah & detail integrasi di [`docs/04-HANDOFF-PRESENTASI.md`](docs/04-HANDOFF-PRESENTASI.md) dan bukti terverifikasi di [`docs/03-PENGUJIAN-DAN-BUKTI.md`](docs/03-PENGUJIAN-DAN-BUKTI.md).
6. **Sebelum uji hardware fisik & flashing pertama kali**, ikuti seluruh daftar periksa keselamatan 3.3V, pemilihan GPIO, dan skenario uji bertahap di [`docs/07-HARDWARE-BRINGUP-CHECKLIST.md`](docs/07-HARDWARE-BRINGUP-CHECKLIST.md).

## Alur kerja Git (repo sudah aktif)

- Repositori Git sudah diinisialisasi dan tersambung ke `origin` (`https://github.com/Legorvey/embedded-blackbox.git`). **Jangan menjalankan `git init` ulang.**
- Gunakan branch fitur (misal `feat/blackbox-storage-hardening`) dan commit kecil per tujuan dengan format Conventional Commits.
- **Jangan pernah commit** file PDF materi pribadi, screenshot WhatsApp, kredensial, ataupun artefak build (`build/`, `build_test_host/`, `*.o`, `*.exe`). Aturan pengecualian sudah diatur di [`.gitignore`](.gitignore).

## Jangan sampai lupa

- **VS Code = editor**; **ESP-IDF = framework + build tools + driver**; **FreeRTOS = task scheduler**; **GitHub = kolaborasi/riwayat kode**.
- `format_if_mount_failed` **wajib tetap `false`** (`bb_storage_init()` menolak `true` dengan `ESP_ERR_INVALID_ARG`).
- Pin GPIO default sengaja disetel `GPIO_NUM_NC` (`-1`) sampai pinout board ESP32-S3 fisik dikonfirmasi bersama tim.
- Lihat [`source_refs/README.md`](source_refs/README.md) untuk daftar referensi resmi ESP-IDF `v5.4`.
