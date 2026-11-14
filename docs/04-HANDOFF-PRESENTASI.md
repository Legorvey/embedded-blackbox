# 04 — Handoff Bagian Storage, Kesiapan Integrasi & Materi Presentasi

> **Status Dokumen:** Siap dipakai untuk serah-terima komponen `blackbox_storage` ke tim (`blackbox_core` & `blackbox_interface`) serta materi presentasi progres **Senin, 16 November 2026**. Seluruh hasil build dan uji otomatis di bawah ini diverifikasi langsung di lingkungan pengembangan, sedangkan uji kartu microSD fisik pada board ESP32-S3 masih berstatus **`UNVERIFIED ON HARDWARE`**.

---

## 1. Penjelasan Ringkas Komponen `blackbox_storage`

`blackbox_storage` adalah komponen **reusable ESP-IDF** untuk modul **Storage** pada proyek *Embedded Event Black Box (ESP32-S3)*. Tugas utamanya menerima snapshot rekaman event gerak (`bb_event_record_t`) dari `blackbox_core`, menuliskannya secara deterministik ke kartu microSD berformat FAT (`SDSPI` + `FATFS VFS`) dalam format **CSV `v1` + checksum CRC-32**, lalu menyediakan daftar file event yang sudah selesai (`E*.CSV`) dan status modul secara aman untuk `blackbox_interface` (Web Server HTTP).

**Batas Tanggung Jawab (Separation of Concerns):**
- **Milik `blackbox_storage`:** mount/unmount microSD via SDSPI, manajemen/sharing bus SPI (`manage_spi_bus`), proteksi konkurensi VFS/mutex internal, format CSV `v1`, siklus hidup file dua tahap (`.TMP` -> `.CSV`), verifikasi baca-balik + CRC-32, serta self-test non-destruktif (`BBTEST00`).
- **Bukan tugas Storage:** membaca MPU6050/ADXL345, mengelola ring-buffer/threshold deteksi event (`blackbox_core`), atau menyajikan endpoint HTTP/dashboard (`blackbox_interface`).

---

## 2. Fungsionalitas yang Sudah Diimplementasikan

Seluruh API publik terdokumentasi di [`components/blackbox_storage/include/blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h) dan [`components/blackbox_storage/README.md`](../components/blackbox_storage/README.md):

1. **Inisialisasi, Validasi Keamanan & Teardown (`bb_storage_init`, `bb_storage_deinit`):**
   - Mendukung mode bus SPI eksklusif (`cfg.manage_spi_bus = true`) maupun berbagi bus SPI dengan sensor ADXL345 (`cfg.manage_spi_bus = false`, host `SPI2_HOST` atau `SPI3_HOST`).
   - Menolak tegas `format_if_mount_failed = true` (`ESP_ERR_INVALID_ARG`), menolak host `SPI1_HOST`, menolak pin invalid ESP32-S3 (`GPIO_NUM_NC`, `22..25`, `>= 49`), menolak duplikasi/kolisi pin, serta membatasi panjang `mount_point <= 13` karakter (`ESP_VFS_PATH_MAX - 2` pada ESP-IDF `v5.4.4`).
   - Melakukan *rollback* `spi_bus_free()` bila mount gagal saat bus dimiliki Storage; pada `bb_storage_deinit()`, selalu membersihkan status lokal (`UNINITIALIZED`, handle kartu `NULL`) dan melepas bus SPI yang dimiliki walaupun `esp_vfs_fat_sdcard_unmount()` mengembalikan error (mencegah *use-after-free* karena driver ESP-IDF sudah membebaskan `card` di dalam `unmount`).
2. **Pemantauan Status Lock-Free & Statistik Kapasitas (`bb_storage_get_status`, `bb_storage_status_to_str`, `bb_storage_get_stats`):**
   - `bb_storage_get_status()` memakai pembacaan atomik (`__atomic_load_n`) sehingga dapat dipanggil cepat oleh UI/Interface tanpa terblokir I/O kartu SD.
   - `bb_storage_get_stats()` melaporkan total/sisa kapasitas FAT dan statistik penulisan/error (direset ke `0` saat unmounted).
3. **Serialisasi CSV `v1` & Checksum CRC-32 (`bb_storage_validate_event`, `bb_storage_compute_samples_crc32`, `bb_storage_format_event_csv`):**
   - Format kolom fixed-point integer (`mg`, `mdps`) yang deterministik, bebas *float NaN/Inf*, menampung hingga `2048` sampel per event, lengkap dengan metadata header dan footer `# END_EVENT,count=...,crc32=0x...` (**IEEE 802.3 CRC-32**).
4. **Siklus Tulis Dua Tahap Anti-Partial Read (`bb_storage_write_event`, `bb_storage_build_event_paths`):**
   - Penamaan 8.3 FAT-compliant (`E<7-digit-hex>.TMP` -> `E<7-digit-hex>.CSV`).
   - Menolak menimpa file `.CSV` dengan `event_id` yang sama (`ESP_ERR_INVALID_STATE`), membersihkan `.TMP` lama, menulis ke `.TMP`, menjalankan `fflush()` + `fsync()` + `fclose()`, lalu atomik `rename()` menjadi `.CSV`.
   - Jika gagal tulis (`ENOSPC` -> status `FULL` / `ESP_ERR_NO_MEM`, I/O lain -> status `WRITE_ERROR` / `ESP_FAIL`), berkas `.TMP` langsung dihapus dan penulisan berikutnya tetap dapat memulihkan status ke `READY`.
5. **Verifikasi Ketat, Filter Listing & Self-Test (`bb_storage_verify_event_file`, `bb_storage_list_completed_events`, `bb_storage_is_completed_event_filename`, `bb_storage_self_test`):**
   - Parser ketat tanpa `sscanf`: memeriksa seluruh header, urutan indeks sampel, batas integer `int32_t`/`uint64_t`, batas panjang baris, ketiadaan baris pasca-footer, jumlah sampel, serta kecocokan CRC-32.
   - `bb_storage_list_completed_events()` memfilter keluar `.TMP`, direktori `E*.CSV/`, file asing, dan `BBTEST00.CSV`.

---

## 3. Hasil Build & Pengujian Terverifikasi

Bukti lengkap tersimpan di [`docs/03-PENGUJIAN-DAN-BUKTI.md`](03-PENGUJIAN-DAN-BUKTI.md) dan [`evidence/build-and-test-log.txt`](../evidence/build-and-test-log.txt):

| Jenis Pengujian | Perintah / Lingkungan | Hasil Terverifikasi | Catatan Jujur |
|---|---|---|---|
| **Full Firmware Build ESP-IDF (`esp32s3`)** | `idf.py build` & `idf.py size` pada **ESP-IDF `v5.4.4`** (CMake `3.30.2`, Ninja `1.12.1`, GCC `14.2.0`) | **PASS (`0 warnings / 0 errors`)** — `embedded_blackbox.elf` (`3.804.892` B), `embedded_blackbox.bin` (`205.056` B; `idf.py size` `204.936` B, `80%` sisa di partisi `factory` `1M`), `bootloader.bin` (`20.928` B), `partition-table.bin` (`3.072` B), `libblackbox_storage.a` (`189.180` B; `.obj` `.text=11.436, .data=8, .bss=208`) | Membuktikan kompilasi & linking penuh ke binary ESP32-S3 sukses; `main/main.c` starter hanya ter-link minimal (`1.186` B dari archive) karena keluar dini pada `GPIO_NUM_NC` |
| **Cross-Compile Header Check (`esp32s3`)** | `.\tests\run_tests.ps1` Step 1 (`xtensa-esp32s3-elf-gcc` `-Wall -Wextra -Werror`, SDK IDF v5.4) | **PASS (`0 warnings`)** — `blackbox_storage_esp32s3.o` (`148.936` B), `main_esp32s3.o` (`15.004` B), `test_blackbox_storage_host_check.o` (`80.988` B) | Menguji kompilasi ketat tanpa warning terhadap header ESP32-S3 |
| **Host Unit, Fault-Injection & Concurrency Tests** | `.\tests\run_tests.ps1` Step 2 (`MinGW GCC 4.9.2 -std=gnu11` & `MATLAB lcc64`) | **PASS (`8/8 suites` di kedua compiler, `0 failed`)** — validasi pin/config, init/deinit fault injection, proteksi overflow, CSV `v1` + golden CRC `0x10BF5ABE` (`441` B, [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv)), 2048 sampel, 11 titik fault write/rename, listing, & 4-thread concurrency (32 event paralel) | Memakai stub FreeRTOS/ESP-IDF + filesystem host (`BB_STORAGE_HOST_TEST`); **bukan** eksekusi FreeRTOS/SDSPI pada kartu microSD fisik |
| **Pengujian Perangkat Keras Fisik microSD** | `idf.py flash monitor` pada ESP32-S3 + socket microSD fisik | **NOT RUN (`UNVERIFIED ON HARDWARE`)** | Pin SPI masih `GPIO_NUM_NC` (`-1`), board fisik belum terhubung & spesifikasi flash/PSRAM tim belum dikonfirmasi |

---

## 4. Batasan Teknis Jujur (`Honest Limitations`)

1. **Belum Diuji di Hardware Fisik (`UNVERIFIED ON HARDWARE`):** Kestabilan sinyal SPI, inisialisasi kartu microSD spesifik, konsumsi arus socket, serta interferensi wiring fisik belum terbukti sampai checklist pada [`docs/07-HARDWARE-BRINGUP-CHECKLIST.md`](07-HARDWARE-BRINGUP-CHECKLIST.md) dijalankan langsung pada board ESP32-S3 tim.
2. **Bukan Journaling Filesystem (Risiko Mati Daya Mendadak pada FAT32):** Mekanisme `.TMP -> fsync -> rename .CSV` mencegah pembaca membaca file separuh pada operasi normal, dan CRC-32 mendeteksi file terpotong/korup, tetapi **sistem berkas FAT16/FAT32 tetap berisiko mengalami korupsi tabel cluster FAT bila catu daya terputus tepat saat sektor FAT sedang dimodifikasi**.
3. **Operasi Write/Stat Bersifat Blocking (I/O Sinkron):**
   - `bb_storage_write_event()` melakukan penulisan SPI/FAT penuh secara sinkron dan bisa memakan waktu puluhan hingga ratusan ms tergantung kecepatan kartu microSD.
   - `bb_storage_get_stats()` memanggil `esp_vfs_fat_info()` -> `f_getfree()`, yang pada panggilan pertama terhadap kartu tanpa nilai `FSInfo` valid dapat memindai seluruh FAT selama beberapa detik sambil memegang `bb_lock()`. Jangan memanggilnya dari ISR, task akuisisi 100 Hz, atau loop polling UI berkala.
4. **Batasan Driver `vfs_fat.c` & `sdspi_host.c` ESP-IDF `v5.4.4`:**
   - Pada `vfs_fat.c`, cache `dir_path` dan `cached_fileinfo` di `vfs_fat_stat()` / `opendir()` / `readdir()` tidak dilindungi `fat_ctx->lock`, dan `f_mount(0)` saat unmount tidak reentrant. `blackbox_interface` wajib menaati aturan pembacaan aman di Bagian 5.
   - Pada `sdspi_host.c`, `deinit_slot()` mengembalikan pin `gpio_cs` ke mode input ketika unmount/gagal mount, dan transfer SD mengunci host SPI selama polling sibuk kartu (`poll_busy()`).
5. **Starter App & Kebersihan Konfigurasi:**
   - [`main/main.c`](../main/main.c) mendefinisikan `app_main()` sebagai harness pengaman (langsung keluar jika pin `GPIO_NUM_NC`) — **tidak boleh** digabung ke aplikasi utama tim.
   - [`sdkconfig.defaults`](../sdkconfig.defaults) starter memakai default Flash **2MB DIO** dan **PSRAM nonaktif**; proyek tim harus menyesuaikan ini dengan modul ESP32-S3 fisik sebenarnya.

---

## 5. Panduan Integrasi Tim (`blackbox_core` & `blackbox_interface`)

Rujukan detail juga tersedia di [`docs/02-KONTRAK-INTEGRASI.md`](02-KONTRAK-INTEGRASI.md) dan [`docs/05-KEPUTUSAN-DAN-PERTANYAAN.md`](05-KEPUTUSAN-DAN-PERTANYAAN.md).

### A. Langkah Pasang Komponen di Proyek Utama Tim

1. Salin **hanya direktori** [`components/blackbox_storage/`](../components/blackbox_storage/) ke `components/blackbox_storage/` pada repositori integrasi tim (**jangan** menyalin `main/` atau `CMakeLists.txt` root starter agar tidak bentrok dengan `app_main()` dan `project()` tim).
2. Pada `CMakeLists.txt` komponen pemanggil (`blackbox_core`, `blackbox_interface`, atau `main`), tambahkan `REQUIRES blackbox_storage`.
3. Salin konfigurasi wajib berikut ke `sdkconfig.defaults` proyek utama tim *(wajib karena default stok ESP-IDF `CONFIG_FATFS_FS_LOCK=0` menonaktifkan file lock FatFs)*:
   ```ini
   CONFIG_FATFS_LFN_HEAP=y
   CONFIG_FATFS_MAX_LFN=64
   CONFIG_FATFS_FS_LOCK=16
   CONFIG_FREERTOS_HZ=1000
   ```
   Sesuaikan pula ukuran Flash/mode SPI Flash/PSRAM board fisik serta partisi aplikasi (starter memakai `1M`, tim yang menambah Wi-Fi/HTTP perlu tabel partisi dan flash size sesuai hardware).
4. Berikan stack **minimal `4096–6144` byte** untuk task FreeRTOS yang memanggil `bb_storage_write_event()` / `bb_storage_verify_event_file()` (karena buffer parsing 384 B + call tree stdio/FATFS/SDSPI).

### B. Kontrak Integrasi untuk `blackbox_core` (Anggota 5)

- **Urutan Init & Topologi SPI:**
  - *Disarankan (bila pin cukup):* gunakan host SPI berbeda dari ADXL345 (`SPI3_HOST` vs `SPI2_HOST`) dengan `cfg.manage_spi_bus = true` agar I/O kartu SD tidak mengganggu determinisme pembacaan akselerometer 100 Hz.
  - *Bila berbagi satu bus SPI (`SPI2_HOST`):* `blackbox_core` memanggil `spi_bus_initialize()` **satu kali sebelum** ADXL345 & `bb_storage_init()` dengan **DMA aktif** (`SPI_DMA_CH_AUTO`) dan `max_transfer_sz >= 4000`, menahan semua CS (`ADXL345 CS` & `SD CS`) tetap **HIGH** sebelum `bb_storage_init()`, memastikan ada pull-up eksternal `3.3V` pada `SD_CS`, serta mengatur `cfg.manage_spi_bus = false`.
  - Isi `cfg` dengan `BB_STORAGE_CONFIG_DEFAULT()`, set `cfg.host_id`, pin GPIO terverifikasi, `cfg.mount_point` (default `"/sdcard"`, maks `13` char), pastikan `cfg.format_if_mount_failed = false`, lalu panggil `bb_storage_init(&cfg)` pada fase startup sistem.
- **Pola Task, Kepemilikan Buffer & Masa Pakai Data:**
  - Task akuisisi sensor 100 Hz **dilarang** memanggil `bb_storage_write_event()`. Gunakan FreeRTOS Queue/event snapshot ke task penyimpan berprioritas lebih rendah.
  - `bb_storage_write_event(&event_record, saved_path, sizeof(saved_path))` bersifat **sinkron**: modul selesai membaca struktur `event_record` dan array `event_record.samples` sebelum fungsi return. **Pemanggil tetap memiliki penuh memori buffer tersebut** dan boleh langsung memakai ulang atau men-`free()`-nya begitu `bb_storage_write_event()` selesai.
- **Siklus Hidup File & Konsistensi `event_id`:**
  - `event_id` wajib berada dalam `1 .. BB_STORAGE_MAX_EVENT_ID` (`0x0FFFFFFF`) dan unik di kartu. Saat boot ulang, `blackbox_core` harus menentukan `next_event_id` dari NVS atau memindai hasil `bb_storage_list_completed_events()` agar tidak tabrakan (`ESP_ERR_INVALID_STATE`).
  - Setiap event melewati fase `E<7-hex>.TMP` -> `fflush` -> `fsync` -> `fclose` -> `rename` menjadi `E<7-hex>.CSV`.

### C. Kontrak Integrasi untuk `blackbox_interface` (Anggota 4)

- **Pemantauan Status:** Panggil `bb_storage_get_status()` (non-blocking / atomic) di status dashboard/JSON endpoint. Hanya panggil `bb_storage_get_stats()` atas permintaan manual/jarang (hindari polling periodik setiap detik).
- **Listing File Selesai:**
  - Panggil `bb_storage_list_completed_events(NULL, 0, &total)` untuk menghitung jumlah event selesai (`E*.CSV`, mengecualikan `.TMP`, direktori palsu, dan `BBTEST00.CSV`), lalu panggil ulang dengan buffer `char files[N][BB_STORAGE_MAX_FILENAME_LEN]` (`16` byte per entry).
  - *Catatan:* urutan daftar mengikuti slot direktori FAT (belum tersortir) dan berhenti di `max_files`. Sortir/paginasi dilakukan di memori Interface.
- **Aturan Wajib Akses File & Konkurensi (`vfs_fat.c` ESP-IDF `v5.4.4`):**
  1. **Validasi nama file HTTP sebelum membuka file:** cek query/parameter filename dari HTTP memakai **`bb_storage_is_completed_event_filename(filename)`** sebelum membentuk `"/sdcard/" + filename` (atau sebelum memanggil `fopen` / `bb_storage_verify_event_file`). Ini memblokir serangan path traversal (`../`) sekaligus memblokir unduhan `.TMP` atau `BBTEST00.CSV`.
  2. **Dilarang memanggil `opendir()`, `readdir()`, atau `stat()` langsung pada `/sdcard`** saat writer aktif, karena cache `dir_path`/`cached_fileinfo` di `vfs_fat.c` tidak terlindungi mutex FATFS. Untuk mengetahui ukuran file saat mengirim header HTTP `Content-Length`, buka dulu file dengan `FILE *fp = fopen(full_path, "rb")` lalu panggil **`fstat(fileno(fp), &st)`** (atau gunakan *HTTP chunked transfer encoding*).
  3. **Sisakan minimal 1 slot FD untuk writer:** batasi stream HTTP SD terbuka secara simultan hingga **`<= cfg.max_open_files - 1`** (maksimal `3` stream paralel jika memakai default `max_open_files = 4`).
  4. **Tutup seluruh stream sebelum `bb_storage_deinit()`:** sebelum unmount dijalankan oleh Core, semua handle `FILE *` HTTP harus sudah ditutup agar tidak memicu *use-after-free* pada konteks VFS yang dibebaskan.

---

## 6. Naskah Presentasi Singkat (Bahasa Indonesia — Siap Baca ~60–90 Detik)

> "Selamat pagi/siang Bapak/Ibu Dosen dan teman-teman. Untuk subsistem **Storage (`blackbox_storage`)**, komponen reusable ESP-IDF sudah selesai kami implementasikan dan harden di atas driver resmi SDSPI dan FATFS ESP-IDF v5.4.
>
> Secara arsitektur, `blackbox_storage` dipisah dari task akuisisi sensor 100 Hz agar latensi penulisan microSD tidak memblokir pembacaan MPU6050 dan ADXL345. Komponen ini mendukung baik bus SPI mandiri maupun berbagi bus dengan ADXL345 melalui flag `manage_spi_bus`, melindungi konfigurasi dengan menolak `format_if_mount_failed = true` dan pin `GPIO_NUM_NC`, memakai mutex FreeRTOS statis serta pembacaan status berbasis atomik yang *lock-free*, dan menulis data event dalam format CSV `v1` dengan nama 8.3 dan checksum **CRC-32**.
>
> Untuk mencegah subsistem Web/Interface mengunduh file event yang belum selesai ditulis, setiap perekaman memakai mekanisme dua tahap: ditulis ke berkas sementara `.TMP`, di-`fflush` dan `fsync` hingga ditutup, baru di-`rename` menjadi `.CSV`.
>
> Dari sisi verifikasi saat ini: proyek sudah **lulus kompilasi dan linking penuh firmware ESP-IDF `v5.4.4` target `esp32s3` tanpa warning** menghasilkan `embedded_blackbox.elf`, `embedded_blackbox.bin` sebesar 205 KB, `bootloader.bin`, dan `partition-table.bin`. Selain itu, komponen telah **lulus 8 dari 8 suite unit test, fault-injection pada 11 titik kegagalan I/O, dan uji konkurensi multi-thread** menggunakan host simulator.
>
> Secara jujur, pengujian kartu **microSD fisik pada board ESP32-S3 belum kami jalankan (`UNVERIFIED ON HARDWARE`)** karena menunggu konfirmasi pin GPIO SPI tim, spesifikasi flash/PSRAM board final, serta perakitan integrasi bersama subsistem Core, ADXL345, dan Interface menggunakan checklist bring-up yang sudah kami siapkan."

---

## 7. Checklist Status Deliverable & Tindak Lanjut Tim

- [x] **Struktur proyek & konfigurasi ESP-IDF siap:** `CMakeLists.txt`, `sdkconfig.defaults` (`CONFIG_FATFS_FS_LOCK=16`, stack `6144`), `main/`, `components/blackbox_storage/`
- [x] **Source code komponen Storage ter-hardening & kontrak sinkron:** [`blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h), [`blackbox_storage.c`](../components/blackbox_storage/blackbox_storage.c), [`CMakeLists.txt`](../components/blackbox_storage/CMakeLists.txt), [`README.md`](../components/blackbox_storage/README.md)
- [x] **Contoh file output CSV berlabel simulasi (`441` B, CRC `0x10BF5ABE`):** [`evidence/example-event-simulated.csv`](../evidence/example-event-simulated.csv)
- [x] **Cross-Compilation Objek Target `esp32s3` & Host Tests (`8/8`):** **PASS** pada GCC `14.2.0` (`-Wall -Wextra -Werror`, `0 warnings`), MinGW GCC `4.9.2`, dan MATLAB `lcc64`
- [x] **Full Firmware Build (`idf.py build` pada ESP-IDF `v5.4.4`, `esp32s3`):** **PASS (`0 warnings/errors`)** — `embedded_blackbox.elf` (`3.804.892` B), `embedded_blackbox.bin` (`205.056` B; `80%` sisa di `factory` `1M`), `bootloader.bin` (`20.928` B), `partition-table.bin` (`3.072` B), `libblackbox_storage.a` (`189.180` B)
- [x] **Panduan Hardware Bring-Up Fisik Tersedia:** [`docs/07-HARDWARE-BRINGUP-CHECKLIST.md`](07-HARDWARE-BRINGUP-CHECKLIST.md)
- [ ] **Konfirmasi Board ESP32-S3, Wiring SPI & Uji Kartu microSD Fisik:** **BELUM DIUJI (`UNVERIFIED ON HARDWARE`)** — ikuti seluruh tahap A–C di [`docs/07-HARDWARE-BRINGUP-CHECKLIST.md`](07-HARDWARE-BRINGUP-CHECKLIST.md)
- [ ] **Sinkronisasi Remote ke GitHub:** Commit disimpan lokal di branch `feat/blackbox-storage-hardening`; push menunggu izin manual pemilik repositori

---

## 8. Panduan Tanya-Jawab Dosen Saat Presentasi

**Q1: Kenapa microSD diakses lewat SPI (SDSPI), dan bagaimana pembagian bus dengan ADXL345?**  
**Jawab:** Arsitektur sistem menempatkan microSD pada bus SPI. ESP-IDF menyediakan driver resmi `sdspi_host` dan `esp_vfs_fat_sdspi_mount()`. Karena topologi final board bisa memakai 1 bus bersama (`SPI2_HOST`) atau 2 bus terpisah (`SPI2_HOST` + `SPI3_HOST`), `blackbox_storage` mendukung keduanya lewat opsi `cfg.manage_spi_bus`, sementara rekomendasi utama kami adalah memisahkan bus bila pin GPIO mencukupi agar blocking I/O SD tidak mengganggu sampling ADXL345 100 Hz.

**Q2: Kenapa komponen Storage dipisah dari Core dan tidak langsung membaca sensor?**  
**Jawab:** Agar akuisisi sensor gerak (target 100 Hz) tetap deterministik dan tidak terjeda oleh latensi penulisan kartu SD. `blackbox_core` menampung sampel di RAM lalu mengirim snapshot event melalui queue ke task penyimpanan prioritas rendah yang memanggil `bb_storage_write_event()`.

**Q3: Library apa yang dipakai dan apa rekayasa tambahan di sisi Storage?**  
**Jawab:** Kami memakai API resmi ESP-IDF v5.4.4 (`esp_vfs_fat.h`, `driver/sdspi_host.h`, `sdmmc_cmd.h`) tanpa memodifikasi kode kernel FATFS/SDSPI, lalu menambahkan: (1) pemisahan kepemilikan SPI bus + rollback aman, (2) validasi keras anti-auto-format (`format_if_mount_failed=false`) & validasi pin `esp32s3` terhadap `GPIO_NUM_NC` / pin Flash, (3) protokol dua tahap `.TMP -> fsync -> .CSV` + checksum **CRC-32**, serta (4) parser verifikasi ketat & filter listing untuk Interface.

**Q4: Apakah data dijamin 100% aman kalau listrik putus mendadak saat menulis?**  
**Jawab:** Tidak 100% imun terhadap korupsi tabel FAT, dan kami menyatakannya secara jujur. Siklus `.TMP -> fsync -> rename .CSV` mencegah server HTTP mengunduh berkas separuh dan CRC-32 mendeteksi berkas rusak/terpotong, namun FAT32 bukan *journaling filesystem* yang kebal pemadaman daya mendadak di tengah pembaruan sektor FAT.

---

## 9. Template Pesan Serah-Terima ke Ketua Tim

> "Halo/Bro, komponen `blackbox_storage` sudah siap untuk handoff integrasi tim dan presentasi Senin, 16 November 2026:
> 1. **Status Kode & Build:** Komponen `components/blackbox_storage/` sudah selesai di-hardening dan lulus **full build firmware ESP-IDF `v5.4.4` target `esp32s3` (`0 warnings/errors`)**, menghasilkan `embedded_blackbox.elf`, `embedded_blackbox.bin` (`205.056` B), `bootloader.bin`, dan `libblackbox_storage.a` (`189.180` B). Seluruh **8/8 host unit, fault-injection, & concurrency test** juga **PASS** di MinGW GCC & MATLAB `lcc64` (data simulasi).
> 2. **Status Hardware:** Uji kartu microSD fisik masih **`UNVERIFIED ON HARDWARE`** karena belum ada board fisik, pin GPIO SPI masih `GPIO_NUM_NC`, dan spesifikasi Flash/PSRAM board final belum dikonfirmasi.
> 3. **Dokumen Acuan Tim:**
>    - Cara integrasi Core & Interface (API, kepemilikan buffer, aturan `vfs_fat.c`, & setting wajib `CONFIG_FATFS_FS_LOCK=16`): `docs/04-HANDOFF-PRESENTASI.md` & `docs/02-KONTRAK-INTEGRASI.md`.
>    - Checklist bring-up board fisik, kompatibilitas 3.3V/pull-up CS, sharing ADXL345, & uji aman microSD: `docs/07-HARDWARE-BRINGUP-CHECKLIST.md`.
>    - Naskah presentasi 60 detik + tanya-jawab & bukti log: `docs/04-HANDOFF-PRESENTASI.md` & `evidence/build-and-test-log.txt`."
