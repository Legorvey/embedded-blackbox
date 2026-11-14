# 07 — Hardware Bring-Up Checklist (ESP32-S3 + microSD SDSPI)

> **Status Verifikasi Saat Ini:** Seluruh operasi kartu microSD fisik pada board ESP32-S3 masih **`UNVERIFIED ON HARDWARE`**. Dokumen ini adalah daftar periksa langkah demi langkah untuk menyalakan, memverifikasi kelistrikan, menyepakati pin GPIO, menguji fungsi baca/tulis, serta mengukur timing dan error secara aman di perangkat fisik tim.

---

## 0. Aturan Keselamatan Keras (Wajib Dibaca Sebelum Wiring / Flashing)

1. **Jangan menebak pin GPIO:** Starter [`main/main.c`](../main/main.c) sengaja memakai nilai sentinel `GPIO_NUM_NC` (`-1`) dan keluar aman tanpa memanggil `bb_storage_init()`. Jangan mengisi nomor pin sembarangan tanpa mengecek board ESP32-S3 fisik dan kesepakatan wiring tim.
2. **Jangan pernah mengaktifkan auto-format:** `format_if_mount_failed` selalu terkunci ke `false`. `bb_storage_init()` menolak nilai `true` dengan `ESP_ERR_INVALID_ARG` demi mencegah penghapusan data kartu microSD secara tidak sengaja.
3. **Pastikan kompatibilitas `3.3V` sebelum menyalakan daya:** GPIO ESP32-S3 **tidak toleran `5V`**. Jangan hubungkan jalur data atau kartu microSD mentah ke tegangan `5V`.
4. **Jangan mengintegrasikan `main/` starter ke proyek tim:** Proyek utama tim hanya menyertakan komponen [`components/blackbox_storage/`](../components/blackbox_storage/README.md); [`main/main.c`](../main/main.c) dan [`CMakeLists.txt`](../CMakeLists.txt) root di repo ini adalah harness terisolasi untuk pengembangan dan verifikasi modul Storage.

---

## 1. Checklist Identifikasi Board ESP32-S3 Fisik

Isi data di bawah berdasarkan label modul/board fisik yang akan dipakai tim sebelum menjalankan `idf.py flash`:

- [ ] **Nama & varian board / modul persis:** (mis. ESP32-S3-DevKitC-1, WROOM-1-N8, WROOM-1-N16R8, custom PCB — **isi dari hardware nyata**)
- [ ] **Kapasitas Flash SPI onboard:** `[ ] 2MB` / `[ ] 4MB` / `[ ] 8MB` / `[ ] 16MB` / lainnya: `____`
- [ ] **Mode Flash:** `[ ] DIO` / `[ ] QIO` / `[ ] OPI` (`80 MHz` / `____ MHz`)
- [ ] **PSRAM onboard:** `[ ] Tidak ada` / `[ ] Quad SPI PSRAM` / `[ ] Octal (OPI) PSRAM` (`____ MB`)
- [ ] **Port USB pemrograman & log serial:**
  - `[ ] USB-to-UART bridge` (CH340/CP2102/FTDI — `COM__`)
  - `[ ] USB Serial/JTAG native ESP32-S3` (`GPIO 19` D-, `GPIO 20` D+)
- [ ] **Penyelarasan konfigurasi proyek (`sdkconfig`):**
  - Starter [`sdkconfig.defaults`](../sdkconfig.defaults) memakai default ESP-IDF `2MB` flash (`dio`), PSRAM **off**, dan partisi `Single factory app` (`1MB`).
  - Sebelum flashing proyek terpadu tim (yang juga memuat Wi-Fi/HTTP Server `blackbox_interface` dan Core), sesuaikan ukuran Flash, mode PSRAM, dan tabel partisi pada `sdkconfig.defaults` proyek tim, lalu lakukan `idf.py fullclean && idf.py build`.

---

## 2. Checklist Modul microSD & Kompatibilitas Elektrik

Periksa breakout/socket microSD yang dipakai tim secara fisik sebelum memasang kabel jumper:

- [ ] **Identifikasi tipe modul microSD:**
  - `[ ] Breakout pasif 3.3V` (tanpa regulator LDO & tanpa IC level shifter — jalur kartu langsung ke header pin) -> **harus disuplai `3.3V`**.
  - `[ ] Breakout aktif 5V` (memiliki IC LDO AMS1117-3.3V dan level shifter TXS0108/74LVC125) -> **umumnya disuplai `5V / VBUS`** di pin `VCC`; jangan suplai `3.3V` ke input LDO 3.3V karena dropout regulator (~1.1V) menyebabkan kartu brown-out ke ~2.2V.
- [ ] **Cek logika pin `MOSI`, `MISO`, `SCLK`, `CS`:** seluruh sinyal ke GPIO ESP32-S3 berada di level **0V – 3.3V** (tidak ada pull-up ke 5V pada jalur MISO/MOSI/SCLK/CS).
- [ ] **Cek resistor pull-up (`10kΩ–47kΩ`) pada pin `CS`:**
  - **Wajib** ada pull-up eksternal `3.3V` pada jalur `SD_CS` bila bus SPI dibagi bersama ADXL345, karena driver SDSPI ESP-IDF (`sdspi_host.c` `deinit_slot()`) mereset pin `gpio_cs` ke mode input ketika `bb_storage_deinit()` dipanggil atau saat mount gagal.
- [ ] **Kualitas kabel & catu daya:**
  - Panjang kabel jumper sependek mungkin (< 10–15 cm), hindari konektor longgar di breadboard.
  - Pin `GND` ESP32-S3 dan modul microSD terhubung pada ground bersama (*common ground*).
  - Arus suplai `3.3V` mencukupi lonjakan arus tulis kartu microSD (puncak ~50–100+ mA) dengan kapasitor decoupling lokal dekat socket bila perlu.
- [ ] **Kecepatan clock SPI awal:**
  - Saat uji kabel jumper pertama kali, uji terlebih dahulu pada frekuensi konservatif (mis. `4000 kHz` – `10000 kHz`), baru naikkan bertahap menuju `SDMMC_FREQ_DEFAULT` (`20000 kHz`) bila waveform dan stabilitas baca/tulis bersih.

---

## 3. Checklist Pemilihan Pin GPIO SPI (Isi Setelah Disepakati Tim)

> **Catatan:** Tabel berikut sengaja dikosongkan (`TBD`) agar tidak mengasumsikan wiring board. Validasi software di `bb_storage_init()` akan menolak host `SPI1_HOST`, pin `< 0`, pin `22..25`, pin `>= 49`, duplikasi antar pin, dan `gpio_cs` yang bentrok dengan bus SPI.

| Sinyal | Host SPI | GPIO Terpilih (Isi Tim) | Catatan Pengecekan Board |
|---|---|---|---|
| `MOSI` | `SPI2_HOST` / `SPI3_HOST` | `TBD` | Wajib output-capable |
| `MISO` | `SPI2_HOST` / `SPI3_HOST` | `TBD` | Bebas konflik strapping/flash |
| `SCLK` | `SPI2_HOST` / `SPI3_HOST` | `TBD` | Wajib output-capable |
| `SD_CS` | idem | `TBD` | Cek pull-up eksternal `3.3V` |
| `ADXL345_CS` *(jika bus dibagi)* | `SPI2_HOST` / terpisah | `TBD` | Tahan HIGH sebelum SD init |

### Pin ESP32-S3 yang Harus Dihindari / Diperiksa Hati-Hati

- [ ] **Bukan pin terlarang silicon ESP32-S3:** `GPIO 22..25` (tidak ter-bond) dan `GPIO >= 49` (tidak valid).
- [ ] **Bukan pin internal Flash/PSRAM (`SPI0/SPI1`):** hindari `GPIO 26..32`, serta hindari `GPIO 33..37` pada modul dengan **Octal Flash atau Octal PSRAM** (mis. varian `N8R8` / `N16R8`).
- [ ] **Bukan strapping pin sensitif boot:** `GPIO 0`, `GPIO 3`, `GPIO 45`, `GPIO 46` (kecuali skematik board membuktikan aman terhadap kondisi boot/strap saat card terpasang).
- [ ] **Tidak bentrok dengan jalur komunikasi debug/console:**
  - `GPIO 19`, `GPIO 20` bila USB Serial/JTAG native dipakai untuk flashing/monitor.
  - `GPIO 43` (`U0TXD`), `GPIO 44` (`U0RXD`) bila UART0 dipakai sebagai console.
- [ ] **Tidak bentrok dengan sensor lain:** pastikan pin SPI tidak bertabrakan dengan I2C MPU6050 (`SDA`, `SCL`), pin interrupt sensor, maupun LED/tombol onboard.

---

## 4. Checklist Kepemilikan Bus SPI & Koeksistensi dengan ADXL345

Tentukan satu arsitektur di tim sebelum integrasi ke `blackbox_core`:

- [ ] **Opsi A — Bus SPI Terpisah *(Direkomendasikan jika pin GPIO mencukupi)*:**
  - Contoh: ADXL345 (`blackbox_imu_spi`) di satu host SPI, sedangkan `blackbox_storage` memakai host SPI yang berbeda (`SPI3_HOST` atau `SPI2_HOST`) dengan `cfg.manage_spi_bus = true`.
  - Keuntungan: operasi tulis/polling microSD tidak memblokir pembacaan akselerometer 100 Hz.
- [ ] **Opsi B — Satu Bus SPI Bersama (`SPI2_HOST` dengan `cfg.manage_spi_bus = false`):**
  - [ ] `blackbox_core` memanggil `spi_bus_initialize()` **satu kali sebelum** inisialisasi ADXL345 dan `bb_storage_init()`, dengan **DMA aktif** (`SPI_DMA_CH_AUTO`) dan `max_transfer_sz >= 4000` byte.
  - [ ] Pin `CS` ADXL345 dan `SD_CS` dikonfigurasi dan ditahan **HIGH (tidak aktif)** sebelum `bb_storage_init()` dipanggil (persyaratan resmi SDSPI shared-bus ESP-IDF saat kartu memasuki mode SPI).
  - [ ] Pin `SD_CS` memiliki pull-up eksternal agar tetap HIGH bila `bb_storage_deinit()` dijalankan atau mount gagal.
  - [ ] Driver ADXL345 memakai `esp_driver_spi` (`spi_bus_add_device`), tidak mengakses register GPIO SPI secara langsung.
  - [ ] Tim memperhitungkan bahwa `sdspi_host` ESP-IDF menahan lock bus SPI selama transfer blok dan polling sibuk kartu (`poll_busy()`), dan pemanggilan pertama `bb_storage_get_stats()` (`f_getfree()`) dapat memindai FAT jika sektor `FSInfo` tidak terisi — hal ini dapat menyebabkan jeda puluhan hingga ratusan ms pada transaksi ADXL345 yang berbagi host SPI yang sama.

---

## 5. Checklist Persiapan Kartu microSD (FAT32) di PC

- [ ] **Backup data:** pastikan tidak ada data penting pada kartu microSD uji (gunakan kartu khusus pengujian lab).
- [ ] **Format kartu microSD ke `FAT32` di PC/laptop:**
  - Untuk kartu <= 32 GB (SDHC): format sebagai `FAT32` (*Allocation unit size default* / `16 KB`).
  - Untuk kartu > 32 GB (SDXC): **jangan pakai `exFAT`** kecuali `CONFIG_FATFS_EXFAT` sengaja diaktifkan dan disepakati; gunakan utilitas resmi/FAT32 formatter bila menguji kartu > 32 GB atau gunakan kartu SDHC 4–32 GB.
- [ ] **Verifikasi kartu dapat dibaca/ditulis di PC:** buat/hapus file uji di PC lalu eject dengan aman untuk memastikan kartu tidak dalam keadaan *read-only* atau rusak fisik.
- [ ] **Cek konflik nama file awal:** pastikan belum ada `E0000001.CSV` tersisa dari tes sebelumnya bila skenario uji akan menulis `event_id = 1` (karena `blackbox_storage` menolak menimpa `event_id` yang sudah ada dengan `ESP_ERR_INVALID_STATE`).
- [ ] **Pastikan `mount_point` <= `13` karakter:** default `"/sdcard"` (`7` karakter) sudah aman terhadap batas `ESP_VFS_PATH_MAX - 2` (`13`) pada ESP-IDF `v5.4.4`.

---

## 6. Checklist Uji Fisik Bertahap (Mount -> Write -> Read/Verify -> Negative Tests)

Jalankan pengujian hanya melalui task FreeRTOS berstack memadai (**minimal `4096–6144` byte**, atau sesuai `CONFIG_ESP_MAIN_TASK_STACK_SIZE=6144`), **bukan** dari ISR atau task akuisisi sensor 100 Hz.

### Tahap A — Smoke Test & Self-Test Non-Destruktif
- [ ] **A1. Konfigurasi aman:** `cfg.format_if_mount_failed = false`, `cfg.max_open_files = 4` (atau sesuai batas <= `16`), pin GPIO terisi sesuai hasil Checklist 3.
- [ ] **A2. Init & info kartu:** `bb_storage_init(&cfg)` mengembalikan `ESP_OK`; `bb_storage_get_status() == BB_STORAGE_STATUS_READY`; `sdmmc_card_print_info()` menampilkan nama, kapasitas, dan kecepatan kartu yang benar.
- [ ] **A3. Statistik awal:** `bb_storage_get_stats(&stats)` mengembalikan `total_bytes > 0`, `free_bytes > 0`, `events_written == 0`.
- [ ] **A4. Built-in self-test:** `bb_storage_self_test()` mengembalikan `ESP_OK` (menulis `BBTEST00.TMP` -> `BBTEST00.CSV`, memverifikasi CRC-32, lalu menghapus berkas tes; `events_written` tetap `0`).

### Tahap B — Uji Tulis Event Sintetis, Read-Back Verify & Inspeksi PC
- [ ] **B1. Tulis event uji kecil (`N = 3` sampel, `event_id = 1`, flag `BB_EVENT_FLAG_SIMULATED`):** `bb_storage_write_event()` mengembalikan `ESP_OK` dan `saved_path = "/sdcard/E0000001.CSV"`.
- [ ] **B2. Verifikasi read-back di ESP32-S3:** `bb_storage_verify_event_file("/sdcard/E0000001.CSV", 1, 3)` mengembalikan `ESP_OK`.
- [ ] **B3. Tulis event ukuran penuh (`N = 2000` atau `2048` sampel, `event_id = 2`):** `bb_storage_write_event()` dan `bb_storage_verify_event_file()` keduanya `ESP_OK`.
- [ ] **B4. Cek listing untuk Interface:** `bb_storage_list_completed_events()` melaporkan `count == 2` (`E0000001.CSV`, `E0000002.CSV`), tanpa menampilkan berkas `.TMP` maupun `BBTEST00.CSV`.
- [ ] **B5. Unmount aman:** `bb_storage_deinit()` mengembalikan `ESP_OK` dan `bb_storage_get_status() == BB_STORAGE_STATUS_UNINITIALIZED`.
- [ ] **B6. Inspeksi manual di PC:** Cabut kartu hanya setelah `deinit` selesai / board dimatikan. Buka `E0000001.CSV` & `E0000002.CSV` di PC dan pastikan format `# BLACKBOX_EVENT_FORMAT,v1`, metadata, jumlah baris, `# END_EVENT`, serta tidak ada berkas `.TMP` tertinggal.

### Tahap C — Uji Kasus Negatif & Ketahanan Kegagalan
- [ ] **C1. Boot tanpa kartu microSD:** lepas kartu -> `bb_storage_init()` mengembalikan kode error non-`ESP_OK`, status menjadi `BB_STORAGE_STATUS_MOUNT_FAILED`, aplikasi tidak panic/guru meditation, dan pin CS/bus dibersihkan aman.
- [ ] **C2. Perlindungan anti-timpa (`event_id` duplikat):** mencoba menulis ulang `event_id = 1` saat `E0000001.CSV` sudah ada -> wajib mengembalikan `ESP_ERR_INVALID_STATE`, file lama tidak berubah, status tetap `READY`.
- [ ] **C3. Uji tulis dengan `format_if_mount_failed = true` (sanity safety):** `bb_storage_init()` langsung menolak dengan `ESP_ERR_INVALID_ARG`.
- [ ] **C4. Uji koeksistensi SPI *(khusus jika berbagi bus dengan ADXL345)*:** jalankan polling/sampling ADXL345 100 Hz sambil menulis `E0000003.CSV` (`N = 2000`), periksa tidak ada timeout bus SPI atau kerusakan data sensor/CSV.
- [ ] **C5. Uji baca HTTP konkuren *(saat integrasi `blackbox_interface`)*:** unduh `E0000002.CSV` lewat HTTP sambil menulis `E0000004.CSV`; pastikan `CONFIG_FATFS_FS_LOCK=16` aktif, HTTP memakai `fopen` + `fstat(fileno(fp), &st)` (bukan `opendir`/`stat`), total open files <= `cfg.max_open_files`, dan semua verifikasi CRC tetap `ESP_OK`.

---

## 7. Template Pengukuran Timing, Memori & Error (Isi Hasil Uji Fisik)

Gunakan `esp_timer_get_time()` di sekitar pemanggilan API dan `uxTaskGetStackHighWaterMark(NULL)` pada task penulis untuk mencatat angka pengukuran nyata:

| Pengukuran | Kondisi Uji (Isi) | Nilai Terukur (Isi) | Catatan / Kode Error |
|---|---|---|---|
| **Frekuensi SPI teruji** | `____ kHz` (`board = ____`) | — | — |
| **Durasi `bb_storage_init()`** | Kartu `____ GB`, format FAT32 | `____ ms` | `status = ____` |
| **Durasi `bb_storage_self_test()`** | `BBTEST00` write+verify+remove | `____ ms` | `err = ____` |
| **Durasi `bb_storage_write_event()` (`N=3`)** | `.TMP` -> `fsync` -> `rename` `.CSV` | `____ ms` (`____ B`) | `err = ____` |
| **Durasi `bb_storage_write_event()` (`N=500`)** | 500 sampel @ 100 Hz | `____ ms` (`____ B`) | `err = ____` |
| **Durasi `bb_storage_write_event()` (`N=2000`)** | Window penuh 10s @ 2 sensor × 100 Hz | `____ ms` (`____ B`) | `err = ____` |
| **Durasi `bb_storage_verify_event_file()` (`N=2000`)** | Baca-balik + hitung CRC-32 | `____ ms` | `err = ____` |
| **Durasi `bb_storage_get_stats()` (ke-1 vs ke-2)** | Panggilan awal vs setelah cache FAT | `____ ms` / `____ ms` | Cek jeda scan FAT |
| **Sisa Stack Minimum Task Penulis** | `uxTaskGetStackHighWaterMark` | `____ bytes` | Target >= 4096 B |
| **Dampak pada ADXL345 *(jika bus bersama)*** | Jitter/drop saat `write_event` `N=2000` | `____ ms max gap` | Terukur / N/A |
| **Error yang diamati (jika ada)** | Uji tanpa kartu / lepas / penuh | `0x____` | Ringkasan log UART |

---

> **Setelah seluruh checklist di atas terisi:** lampirkan salinan serial monitor log ke [`docs/03-PENGUJIAN-DAN-BUKTI.md`](03-PENGUJIAN-DAN-BUKTI.md) dan tandai status kasus uji fisik dari `UNVERIFIED ON HARDWARE` menjadi `PASS` atau `FAIL` sesuai bukti pengukuran nyata.
