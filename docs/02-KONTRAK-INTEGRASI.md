# 02 — Kontrak Integrasi `blackbox_storage` (Provisional v1 — Perlu Konfirmasi Tim)

> Dokumen ini mendefinisikan **kontrak API publik yang telah diimplementasikan** pada [`blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h) sekaligus memisahkan mana bagian yang sudah siap dipakai dan mana asumsi yang masih menunggu kesepakatan anggota **Core**, **ADXL345 (SPI)**, dan **Interface**.

---

## 1. Batas Kepemilikan Data & Tanggung Jawab Antar-Komponen

1. **`blackbox_core` -> `blackbox_storage`:**
   - `blackbox_core` mengumpulkan sampel *pre-event* dan *post-event* di RAM, mengisi struct `bb_event_record_t`, lalu memanggil `bb_storage_write_event(&event, final_path, sizeof(final_path))` dari **task penyimpanan berprioritas rendah** (bukan dari ISR atau task sampling 100 Hz).
   - **Kontrak Lifetime Memori:** `bb_storage_write_event()` bersifat **sinkron (blocking)** dan selesai membaca `event` serta array `event->samples` sebelum fungsi kembali (*return*). Pemanggil (`blackbox_core`) tetap memiliki buffer tersebut dan bebas menggunakan ulang atau membebaskan memorinya segera setelah `bb_storage_write_event()` selesai.
2. **`blackbox_storage` -> `blackbox_interface`:**
   - Saat proses penulisan berlangsung, berkas ditulis ke nama sementara `E<7-digit-hex>.TMP` (contoh: `/sdcard/E0000001.TMP`).
   - Setelah seluruh sampel dan footer `# END_EVENT,count=...,crc32=0x...` berhasil di-*flush* (`fflush` + `fsync`) dan ditutup, berkas di-*rename* menjadi `E<7-digit-hex>.CSV` (contoh: `/sdcard/E0000001.CSV`).
   - `blackbox_interface` wajib memanggil `bb_storage_list_completed_events()` (atau memfilter menggunakan `bb_storage_is_completed_event_filename()`) sehingga klien HTTP **hanya pernah melihat dan mengunduh berkas `.CSV` yang sudah selesai**.
   - Untuk polling LED/OLED/HTTP status tanpa risiko tertunda latensi SD card, Interface dapat memanggil `bb_storage_get_status()` yang bersifat *lock-free/atomic*.
3. **Koordinasi Bus SPI (`blackbox_imu_spi` / ADXL345 & `blackbox_storage`):**
   - Jika microSD menggunakan controller SPI terpisah dari ADXL345 (`SPI2_HOST` vs `SPI3_HOST`), set `cfg.manage_spi_bus = true` (default). `blackbox_storage` akan memanggil `spi_bus_initialize()` dan `spi_bus_free()`.
   - Jika microSD dan ADXL345 **berbagi satu bus SPI** (misal `SPI2_HOST`), pemilik bus utama (`blackbox_core`) wajib menginisialisasi bus SPI satu kali dengan **DMA aktif (`SPI_DMA_CH_AUTO`)** dan **`max_transfer_sz >= 4000`**, menahan **seluruh pin CS (SD & ADXL345) tetap HIGH (inaktif)**, lalu memanggil `bb_storage_init()` dengan `cfg.manage_spi_bus = false`.
   - Pada target ESP-IDF `v5.4.4`, `cfg.mount_point` harus diawali `'/'` dan panjangnya tidak melebihi `ESP_VFS_PATH_MAX - 2` (`13` karakter, misalnya `"/sdcard"`), karena `vfs_fat.c` menyalin `base_path` dengan `strlcpy(..., sizeof(base_path) - 1)` (`sizeof == 15`).

---

## 2. Definisi Tipe Data & API Publik (`blackbox_storage.h`)

### A. Struktur Sampel (`bb_storage_sample_t`) — *PROVISIONAL*

```c
typedef struct {
    uint64_t timestamp_us; /* Timestamp monotonik dalam mikrodetik (mis. esp_timer_get_time()) */
    int32_t  offset_ms;    /* Offset relatif terhadap titik trigger (<0 pre, 0 trigger, >0 post) */
    uint8_t  sensor_id;    /* 1 = BB_SENSOR_MPU6050_I2C, 2 = BB_SENSOR_ADXL345_SPI */
    int32_t  accel_x_mg;   /* Akselerasi X dalam milli-g (1000 mg = 1.0 g) */
    int32_t  accel_y_mg;   /* Akselerasi Y dalam milli-g */
    int32_t  accel_z_mg;   /* Akselerasi Z dalam milli-g */
    int32_t  gyro_x_mdps;  /* Kecepatan sudut X dalam milli-deg/s (0 untuk ADXL345) */
    int32_t  gyro_y_mdps;  /* Kecepatan sudut Y dalam milli-deg/s (0 untuk ADXL345) */
    int32_t  gyro_z_mdps;  /* Kecepatan sudut Z dalam milli-deg/s (0 untuk ADXL345) */
} bb_storage_sample_t;
```

### B. Struktur Rekaman Event (`bb_event_record_t`) — *PROVISIONAL*

```c
typedef struct {
    uint16_t                   format_version;       /* Wajib BB_STORAGE_FORMAT_VERSION (1) */
    uint16_t                   sample_rate_hz;       /* Frekuensi sampling nominal (mis. 100 Hz) */
    uint32_t                   event_id;             /* ID unik event (1 .. 0x0FFFFFFF) */
    uint64_t                   trigger_timestamp_us; /* Waktu terjadinya trigger (us) */
    uint32_t                   flags;                /* Bitmask bb_event_flags_t */
    size_t                     pre_event_samples;    /* Jumlah sampel sebelum trigger */
    size_t                     post_event_samples;   /* Jumlah sampel sesudah trigger */
    size_t                     sample_count;         /* Total sampel (1 .. BB_STORAGE_MAX_SAMPLES=2048) */
    const bb_storage_sample_t *samples;              /* Pointer ke array sampel milik Core */
} bb_event_record_t;
```

### C. Fungsi Utama API Publik

```c
esp_err_t bb_storage_init(const bb_storage_config_t *cfg);
esp_err_t bb_storage_deinit(void);
bb_storage_status_t bb_storage_get_status(void);
esp_err_t bb_storage_get_stats(bb_storage_stats_t *out_stats);

esp_err_t bb_storage_validate_event(const bb_event_record_t *event);
esp_err_t bb_storage_write_event(const bb_event_record_t *event,
                                 char *out_final_path,
                                 size_t final_path_size);
esp_err_t bb_storage_verify_event_file(const char *file_path,
                                       uint32_t expected_event_id,
                                       size_t expected_sample_count);
esp_err_t bb_storage_list_completed_events(char out_filenames[][BB_STORAGE_MAX_FILENAME_LEN],
                                           size_t max_files,
                                           size_t *out_count);
```

### D. Contoh Pemakaian Singkat untuk `blackbox_core` & `blackbox_interface`

```c
/* 1. Inisialisasi & Penulisan oleh Storage Writer Task di blackbox_core */
bb_storage_config_t st_cfg = BB_STORAGE_CONFIG_DEFAULT();
st_cfg.gpio_mosi = BOARD_SPI_MOSI;
st_cfg.gpio_miso = BOARD_SPI_MISO;
st_cfg.gpio_sclk = BOARD_SPI_SCLK;
st_cfg.gpio_cs   = BOARD_SD_CS;
/* Jika berbagi bus SPI dengan ADXL345 yang sudah init duluan: st_cfg.manage_spi_bus = false; */
esp_err_t err = bb_storage_init(&st_cfg);

/* Dari FreeRTOS queue task prioritas rendah: */
char saved_path[BB_STORAGE_MAX_PATH_LEN];
err = bb_storage_write_event(&event_record, saved_path, sizeof(saved_path));

/* 2. Daftar file selesai & status cepat di blackbox_interface */
bb_storage_status_t status = bb_storage_get_status(); /* non-blocking / atomic */
size_t total_events = 0;
if (bb_storage_list_completed_events(NULL, 0, &total_events) == ESP_OK && total_events > 0) {
    /* Catatan: urutan mengikuti slot direktori FAT (belum tersortir) dan berhenti di max_files */
    char files[32][BB_STORAGE_MAX_FILENAME_LEN];
    size_t count = 0;
    if (bb_storage_list_completed_events(files, 32, &count) == ESP_OK) {
        /* Wajib validasi filename (cegah traversal / .TMP): */
        if (bb_storage_is_completed_event_filename(files[i])) {
            /* Buka "/sdcard/" + files[i] via fopen("rb"), cek ukuran pakai fstat(fileno(fp), &st) */
        }
    }
}
```

> **Catatan Konkurensi FATFS, VFS (`vfs_fat.c` ESP-IDF v5.4.4) & `sdkconfig.defaults`:** `sdkconfig.defaults` menyetel `CONFIG_FATFS_FS_LOCK=16` (`FF_FS_REENTRANT=1`) — setelan ini **wajib disalin** ke `sdkconfig.defaults` proyek utama tim. Audit kode sumber ESP-IDF `v5.4.4` (`vfs_fat.c` & `sdspi_host.c`) menunjukkan bahwa cache `dir_path` dan `cached_fileinfo` pada `vfs_fat_opendir()`/`vfs_fat_readdir()`/`vfs_fat_stat()` tidak dilindungi `fat_ctx->lock`, `vfs_fat_read()` juga tidak memegang `fat_ctx->lock`, `f_mount(0, ...)` saat `deinit` tidak reentrant, serta `deinit_slot()` mengembalikan pin `gpio_cs` ke mode input. Oleh karena itu: (1) `blackbox_interface` **wajib memakai `bb_storage_list_completed_events()`** + `bb_storage_is_completed_event_filename()` dan dilarang memanggil `opendir()`/`stat()` langsung ke `/sdcard` saat writer aktif (untuk `Content-Length`, buka dulu `fopen()` lalu gunakan `fstat(fileno(fp), &st)` atau *chunked transfer*); (2) semua stream unduhan HTTP wajib ditutup sebelum `bb_storage_deinit()`; (3) file descriptor HTTP ikut berbagi slot `cfg.max_open_files` (default `4` — sisakan minimal 1 slot untuk writer, maks `<= cfg.max_open_files - 1` stream simultan); (4) `bb_storage_deinit()` selalu membersihkan state & bus yang dimiliki bahkan jika unmount error (tidak ada *retry deinit*); dan (5) siapkan pull-up eksternal `3.3V` pada `SD_CS` bila bus SPI dibagi dengan ADXL345.

---

## 3. Daftar Kesepakatan yang Masih Perlu Dikonfirmasi Tim (`INTEGRATION PENDING`)

| No | Pertanyaan Integrasi | Asumsi Provisional di `blackbox_storage` Saat Ini | Pihak yang Perlu Menyetujui |
|---|---|---|---|
| 1 | Satuan sampel sensor (`mg`/`mdps` integer vs `float` `m/s²` & `deg/s`) | Menggunakan `int32_t` fixed-point (`mg` dan `mdps`) agar deterministik, cepat, dan bebas masalah format float | Anggota 1 (MPU6050), Anggota 2 (ADXL345), Anggota 5 (Core) |
| 2 | Sumber waktu `timestamp_us` | Monotonik `uint64_t` mikrodetik sejak boot (`esp_timer_get_time()`) + `offset_ms` relatif terhadap trigger | Anggota 5 (Core) |
| 3 | Batas maksimum sampel per event (`BB_STORAGE_MAX_SAMPLES`) | Dibatasi hingga `2048` sampel per event (cukup untuk 10 detik @ 100 Hz × 2 sensor = 2000 sampel) | Anggota 5 (Core) |
| 4 | Kebijakan penomoran `event_id` saat reboot | `event_id` dalam rentang `1 .. 0x0FFFFFFF`; file dengan `event_id` yang sudah ada di SD tidak boleh ditimpa (`ESP_ERR_INVALID_STATE`) | Anggota 5 (Core) |
| 5 | Pembagian bus SPI antara microSD dan ADXL345 | Didukung keduanya melalui flag `cfg.manage_spi_bus` (`true` jika bus terpisah/milik Storage, `false` jika bus dibagi dan di-init oleh Core); perhatikan bahwa transaksi SDSPI menahan lock bus selama polling sibuk kartu | Anggota 2 (ADXL345), Anggota 5 (Core) |
| 6 | Cara `blackbox_interface` menyajikan daftar, sortir, & isi file ke HTTP | Memanggil `bb_storage_list_completed_events()` (urutan FAT belum tersortir; hitung dulu via `NULL, 0, &total`), tutup stream sebelum `deinit`, dan hindari `opendir()`/`stat()` langsung ke VFS | Anggota 4 (Interface) |
| 7 | Pin GPIO fisik (`MOSI`, `MISO`, `SCLK`, `CS`) pada board ESP32-S3 | Default disetel ke `GPIO_NUM_NC` (`-1`); wajib diisi setelah board fisik dan skematik wiring disepakati | Seluruh tim |
| 8 | Spesifikasi Flash, PSRAM, & tabel partisi board ESP32-S3 tim | Starter memakai default ESP-IDF `2MB` flash (`dio`, `80m`), PSRAM off, dan `Single factory app` (`1M`); wajib disesuaikan sebelum flashing & integrasi Wi-Fi/HTTP | Ketua & Anggota 5 (Core) |
