# Komponen `blackbox_storage`

Komponen reusable ESP-IDF untuk menyimpan rekaman kejadian gerak (*motion event recordings*) dari `blackbox_core` ke kartu **microSD** berformat **FAT (FAT16/FAT32)** melalui antarmuka **SPI (SDSPI)**.

---

## 1. Tanggung Jawab & Batas Arsitektur

- **Yang dilakukan `blackbox_storage`:**
  - Menginisialisasi perangkat SDSPI dan me-*mount* partisi FAT melalui ESP-IDF Virtual Filesystem (`esp_vfs_fat_sdspi_mount`).
  - Mendukung dua mode kepemilikan bus SPI (`manage_spi_bus = true` bila Storage memiliki bus SPI sendiri, atau `manage_spi_bus = false` bila berbagi bus SPI dengan sensor ADXL345 yang diinisialisasi oleh pemilik bus eksternal).
  - Memvalidasi konfigurasi hardware dan data `bb_event_record_t` sebelum menulis ke media penyimpanan.
  - Menulis rekaman event dalam format **CSV berversi (`v1`)** lengkap dengan metadata event dan footer integritas **CRC-32 (`# END_EVENT`)**.
  - Menerapkan siklus penulisan dua tahap (**`.TMP` -> `fflush`/`fsync` -> `fclose` -> `rename` ke `.CSV`**) agar `blackbox_interface` (HTTP server) tidak pernah menyajikan atau mengunduh file yang baru tertulis sebagian.
  - Menyediakan fungsi verifikasi integritas file (`bb_storage_verify_event_file`) dan pencacahan file yang sudah selesai (`bb_storage_list_completed_events`).
- **Yang TIDAK dilakukan `blackbox_storage`:**
  - Tidak membaca sensor MPU6050/ADXL345 secara langsung.
  - Tidak membuat FreeRTOS task sendiri secara tersembunyi (operasi `bb_storage_write_event` bersifat sinkron dan dilindungi mutex internal; pemanggilan dilakukan dari task penyimpanan yang dikelola oleh `blackbox_core`).
  - Tidak pernah memformat kartu microSD secara otomatis (`format_if_mount_failed` wajib `false`).

---

## 2. Struktur File Komponen

```text
components/blackbox_storage/
├── CMakeLists.txt
├── include/
│   └── blackbox_storage.h
├── blackbox_storage.c
└── README.md
```

### Dependensi ESP-IDF (`CMakeLists.txt`)
- `REQUIRES`: `fatfs`, `sdmmc`, `esp_driver_sdspi`, `esp_driver_spi`, `esp_driver_gpio`, `vfs`
- `PRIV_REQUIRES`: `log`, `freertos`

---

## 3. Ringkasan API Publik (`include/blackbox_storage.h`)

| Fungsi | Kegunaan | Nilai Balik Utama |
|---|---|---|
| `bb_storage_init(cfg)` | Validasi konfigurasi, opsional init SPI bus, dan mount FATFS di `cfg->mount_point` | `ESP_OK`, `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_STATE`, `ESP_FAIL` |
| `bb_storage_deinit()` | Unmount FATFS (`esp_vfs_fat_sdcard_unmount`) dan bebas bus SPI jika `manage_spi_bus == true` | `ESP_OK`, `ESP_ERR_INVALID_STATE` |
| `bb_storage_get_status()` | Mengambil status komponen saat ini (`UNINITIALIZED`, `READY`, `MOUNT_FAILED`, `WRITE_ERROR`, `FULL`) | `bb_storage_status_t` |
| `bb_storage_get_stats(out)` | Mengambil statistik jumlah event tertulis, gagal, ukuran file terakhir, dan kapasitas total/bebas FAT | `ESP_OK`, `ESP_ERR_INVALID_ARG` |
| `bb_storage_validate_event(ev)` | Memeriksa validitas pointer, versi format, batas `event_id`, dan jumlah sampel (`1..2048`) | `ESP_OK`, `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_SIZE` |
| `bb_storage_format_event_csv(...)` | Serialisasi `bb_event_record_t` ke buffer memori dalam format CSV `v1` + CRC-32 | `ESP_OK`, `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_SIZE` |
| `bb_storage_write_event(ev, ...)` | Menulis event ke `<mount>/E<id>.TMP`, flush/sync, close, lalu rename ke `<mount>/E<id>.CSV` | `ESP_OK`, `ESP_ERR_INVALID_STATE`, `ESP_ERR_NO_MEM`, `ESP_FAIL` |
| `bb_storage_verify_event_file(...)` | Membaca ulang file CSV, memeriksa header, `# END_EVENT`, jumlah baris, dan menghitung ulang CRC-32 | `ESP_OK`, `ESP_ERR_NOT_FOUND`, `ESP_ERR_INVALID_CRC`, `ESP_FAIL` |
| `bb_storage_list_completed_events(...)` | Mengambil daftar nama file event yang sudah selesai (`E*.CSV`) dan mengabaikan file `.TMP` | `ESP_OK`, `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_STATE`, `ESP_FAIL` |
| `bb_storage_is_completed_event_filename(name)` | Predikat filter untuk memeriksa apakah nama file berformat `E<7-hex>.CSV` | `true` / `false` |
| `bb_storage_self_test()` | Uji tulis, verifikasi baca-balik CRC-32, dan hapus file uji sintetis pada microSD | `ESP_OK` atau kode error |

---

## 4. Format Berkas CSV (`v1`) & Konvensi Nama Berkas 8.3

Setiap event disimpan dengan nama berkas **8.3 FAT-compliant**:
- Saat sedang ditulis: `E<7 digit hex uppercase>.TMP` (contoh: `/sdcard/E0000001.TMP`)
- Setelah selesai dan terverifikasi: `E<7 digit hex uppercase>.CSV` (contoh: `/sdcard/E0000001.CSV`)

Alasan pemilihan format nama 8.3: kompatibel penuh meskipun opsi Long File Names (`CONFIG_FATFS_LFN_NONE`) dinonaktifkan pada konfigurasi FATFS ESP-IDF.

Contoh isi berkas CSV (`v1`):

```csv
# BLACKBOX_EVENT_FORMAT,v1
# event_id,1
# trigger_timestamp_us,5020000
# sample_rate_hz,100
# flags,0x00000087
# pre_event_samples,2
# post_event_samples,1
# sample_count,4
sample_idx,timestamp_us,offset_ms,sensor,ax_mg,ay_mg,az_mg,gx_mdps,gy_mdps,gz_mdps
0,5000000,-20,1,10,-15,998,100,-30,10
1,5010000,-10,2,18,-22,1004,0,0,0
2,5020000,0,1,2940,-510,1250,1450,320,-90
3,5030000,10,2,410,-95,1015,0,0,0
# END_EVENT,count=4,crc32=0x90C76B22
```

---

## 5. Provenance, Referensi Resmi & Lisensi

Implementasi komponen ini ditulis dari awal sebagai modul C khusus untuk proyek **Embedded Event Black Box** dengan mengacu pada dokumentasi dan pola API resmi Espressif **ESP-IDF v5.4 (`esp32s3`)**:

1. **ESP-IDF FATFS VFS API (`esp_vfs_fat.h`)** — Lisensi upstream: Apache-2.0  
   https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/storage/fatfs.html
2. **ESP-IDF SDSPI Host Driver (`driver/sdspi_host.h`)** — Lisensi upstream: Apache-2.0  
   https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/sdspi_host.html
3. **ESP-IDF Shared SPI Bus Restrictions (`sdspi_share.html`)**  
   https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/sdspi_share.html
4. **Espressif SDSPI Example (`examples/storage/sd_card/sdspi`)** — Lisensi upstream: Public Domain / CC0-1.0  
   https://github.com/espressif/esp-idf/tree/v5.4/examples/storage/sd_card/sdspi

**Rekayasa Orisinal yang Ditambahkan di Atas Contoh Dasar Espressif:**
- Pemisahan kepemilikan bus SPI (`manage_spi_bus`) dengan *rollback* `spi_bus_free()` otomatis bila `esp_vfs_fat_sdspi_mount()` gagal.
- Penolakan keras terhadap `format_if_mount_failed = true` dan pin `GPIO_NUM_NC` yang belum dikonfigurasi.
- Serialisasi event terstruktur (`bb_storage_format_event_csv`) dengan metadata lengkap dan checksum **CRC-32 IEEE 802.3**.
- Siklus hidup berkas dua tahap (`.TMP` -> `.CSV`) dan pembersihan berkas `.TMP` otomatis saat terjadi kegagalan tulis (`ENOSPC` / `EIO`).
- Fungsi verifikasi baca-balik (`bb_storage_verify_event_file`) dan filter daftar file selesai (`bb_storage_list_completed_events`) untuk mencegah `blackbox_interface` membaca file parsial.
- Dukungan kompilasi ganda (target `esp32s3` ESP-IDF v5.4 dan *host unit testing* dengan *fault injection*).

---

## 6. Batasan Teknis (Limitations)

1. **Ketahanan Listrik Mati Mendadak (FAT Power-Loss Limitation):**  
   Mekanisme `.TMP` -> `fflush`/`fsync` -> `fclose` -> `rename` ke `.CSV` menjamin bahwa pada operasi normal, `blackbox_interface` tidak akan membaca file yang sedang ditulis. Namun, sistem berkas **FAT16/FAT32 pada microSD bukan journaling filesystem yang tahan mati listrik mendadak**. Jika daya terputus tepat ketika kartu SD sedang menulis sektor FAT/direktori internal, struktur FAT tetap berisiko mengalami korupsi.
2. **Operasi Sinkron (Blocking I/O):**  
   `bb_storage_write_event()` melakukan operasi I/O SPI yang dapat memakan waktu puluhan hingga ratusan milidetik tergantung latensi kartu microSD. **Jangan pernah memanggil fungsi ini dari ISR atau task akuisisi sensor 100 Hz.** Panggil dari task penyimpanan berprioritas lebih rendah yang menerima snapshot event melalui FreeRTOS Queue.
3. **Berbagi Bus SPI dengan ADXL345:**  
   Jika microSD dan ADXL345 berbagi satu bus SPI (`SPI2_HOST`), seluruh pin CS perangkat lain pada bus tersebut wajib ditahan *HIGH* (inaktif) sebelum kartu microSD diinisialisasi ke mode SPI oleh `esp_vfs_fat_sdspi_mount()`.
