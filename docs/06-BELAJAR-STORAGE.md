# 06 — Panduan Belajar Komponen `blackbox_storage` (Dari Nol sampai Paham Kode)

Dokumen ini ditulis khusus untuk kamu sebagai pemilik komponen **`blackbox_storage`**, dengan bahasa Indonesia yang santai tetapi tetap akurat secara teknis. Setiap konsep langsung dikaitkan dengan **kode asli yang ada di repositori ini** supaya kamu bisa memahami dan menjelaskan setiap barisnya ke ketua tim maupun dosen.

---

## 1. Apa itu SPI (Serial Peripheral Interface)?

Bayangkan mikrokontroler **ESP32-S3** adalah seorang "bos" (*Master*), sedangkan modul **microSD** dan sensor **ADXL345** adalah "anak buah" (*Slave / Device*) yang dihubungkan lewat jalur kabel komunikasi berkecepatan tinggi. Jalur itu bernama **SPI**.

SPI memakai **4 jalur kabel utama**:

| Nama Pin di Kode (`bb_storage_config_t`) | Singkatan | Fungsi Sederhana |
|---|---|---|
| `gpio_sclk` | **SCLK** (*Serial Clock*) | Detak jantung/irama dari ESP32-S3 supaya pengirim dan penerima sinkron. |
| `gpio_mosi` | **MOSI** (*Master Out Slave In*) | Jalur data dari ESP32-S3 menuju kartu microSD (saat kita **menulis** file). |
| `gpio_miso` | **MISO** (*Master In Slave Out*) | Jalur data dari kartu microSD menuju ESP32-S3 (saat kita **membaca** file). |
| `gpio_cs` | **CS** (*Chip Select*) | Kabel "panggilan nama". Kalau pin CS microSD ditarik ke tegangan rendah (*LOW* / `0`), artinya ESP32-S3 sedang bicara dengan microSD. Kalau *HIGH* (`1`), microSD diam dan tidak ikut campur. |

**Kenapa pin CS penting banget di proyek kita?**  
Karena di diagram dosen ada **dua perangkat SPI**: sensor **ADXL345** dan **microSD**. Keduanya boleh memakai kabel `MOSI`, `MISO`, dan `SCLK` yang sama (*shared SPI bus*), **asalkan kabel `CS`-nya terpisah**. Di kode kita ([`blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h)), struktur `bb_storage_config_t` memiliki pengaturan `manage_spi_bus`:
- Kalau `manage_spi_bus = true`: komponen Storage yang menyalakan bus SPI lewat `spi_bus_initialize()`.
- Kalau `manage_spi_bus = false`: bus SPI sudah dinyalakan duluan oleh `blackbox_core` (untuk dipakai bareng ADXL345), sehingga Storage cukup mendaftarkan pin `gpio_cs` miliknya saja.

---

## 2. Apa itu SDSPI?

Kartu microSD sebenarnya punya dua "bahasa" komunikasi:
1. **Mode SDMMC (4-bit/1-bit native):** Lebih cepat, tapi butuh jalur khusus.
2. **Mode SDSPI (SD over SPI):** Kartu microSD diajak bicara menggunakan protokol SPI 4-kabel standar (`SCLK`, `MOSI`, `MISO`, `CS`).

Di ESP-IDF v5.4, **SDSPI** adalah driver resmi (`driver/sdspi_host.h`) yang mengubah perintah-perintah kartu SD (seperti `CMD0` untuk reset kartu, `CMD17` untuk baca sektor 512 byte, `CMD24` untuk tulis sektor) menjadi sinyal SPI.

Di kode kita ([`blackbox_storage.c`](../components/blackbox_storage/blackbox_storage.c)), konfigurasi SDSPI disiapkan seperti ini di dalam `bb_storage_init()`:

```c
sdmmc_host_t host = SDSPI_HOST_DEFAULT();
host.slot = cfg->host_id;
host.max_freq_khz = cfg->max_freq_khz;

sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
slot_config.host_id = cfg->host_id;
slot_config.gpio_cs = cfg->gpio_cs;
```

---

## 3. Apa itu FAT Filesystem (FATFS) dan VFS?

Kartu microSD pada dasarnya hanyalah jutaan kotak memori mentah berukuran 512 byte (disebut *sector*). Kartu SD tidak kenal apa itu "nama file", "folder", atau `E0000001.CSV`.

Agar microSD bisa menyimpan file yang nantinya bisa dibuka di laptop Windows atau diunduh lewat HP, kita butuh:
1. **FAT Filesystem (FAT16/FAT32):** Sistem pencatatan (seperti daftar isi buku) yang mengatur di nomor sektor mana saja sebuah file disimpan. ESP-IDF memakai pustaka **FatFs** untuk mengelola struktur FAT ini.
2. **VFS (Virtual Filesystem):** Jembatan di ESP-IDF yang membuat partisi FAT terlihat seperti folder biasa (misalnya `/sdcard`), sehingga kode C kita bisa memakai fungsi standar `fopen()`, `fprintf()`, `fflush()`, `fclose()`, dan `rename()`!

Di [`blackbox_storage.c`](../components/blackbox_storage/blackbox_storage.c), ketiganya (SDSPI + FATFS + VFS) disatukan oleh satu fungsi resmi ESP-IDF:

```c
err = esp_vfs_fat_sdspi_mount(cfg->mount_point, &host, &slot_config, &mount_config, &card);
```

Perhatikan juga bahwa di `mount_config`, kita menyetel `.format_if_mount_failed = false` dan bahkan di `validate_config()` kita menolak jika ada yang mengirim `format_if_mount_failed = true`:

```c
if (cfg->format_if_mount_failed) {
    BB_LOGE("format_if_mount_failed=true is forbidden by safety policy");
    return ESP_ERR_INVALID_ARG;
}
```
Kenapa? Supaya kalau kabel SPI longgar saat uji coba, ESP32-S3 **tidak tiba-tiba memformat dan menghapus seluruh isi kartu microSD kamu!**

---

## 4. Bagaimana Cara Kerja Komponen ESP-IDF?

Di ESP-IDF, proyek besar dipecah menjadi modul-modul mandiri di dalam folder `components/`. Proyek kita memiliki 5 komponen (1 per mahasiswa), dan milikmu berada di:

`components/blackbox_storage/`

Keuntungan membuat komponen ESP-IDF:
- Kode kamu terisolasi rapi dan tidak bercampur aduk dengan kode sensor I2C, sensor SPI, atau web server.
- Anggota tim lain (`blackbox_core` atau `blackbox_interface`) cukup menambahkan `REQUIRES blackbox_storage` di `CMakeLists.txt` mereka dan `#include "blackbox_storage.h"`.

---

## 5. Apa Fungsi `.h`, `.c`, dan `CMakeLists.txt`?

Sesuai instruksi dosen (*"buat library sendiri dengan menyiapkan file header `.h` dan file kodingan `.c`"*), komponen kamu terdiri dari tiga file inti:

1. **[`components/blackbox_storage/include/blackbox_storage.h`](../components/blackbox_storage/include/blackbox_storage.h) (File Header / "Buku Menu"):**
   - Berisi deklarasi struct (`bb_storage_config_t`, `bb_storage_sample_t`, `bb_event_record_t`), enum status (`bb_storage_status_t`), dan daftar fungsi publik yang boleh dipanggil oleh komponen lain.
   - Tidak berisi logika detail cara kerja internal.
2. **[`components/blackbox_storage/blackbox_storage.c`](../components/blackbox_storage/blackbox_storage.c) (File Implementasi / "Dapur"):**
   - Berisi kode nyata bagaimana fungsi-fungsi di `.h` bekerja: mulai dari validasi pin, memanggil `spi_bus_initialize()` dan `esp_vfs_fat_sdspi_mount()`, menghitung checksum CRC-32, sampai menulis file CSV.
   - Variabel internal seperti `static bb_storage_ctx_t s_ctx;` disembunyikan dengan kata kunci `static` supaya komponen lain tidak bisa mengacak-acak state internal Storage.
3. **[`components/blackbox_storage/CMakeLists.txt`](../components/blackbox_storage/CMakeLists.txt) (Resep Build Sistem):**
   ```cmake
   idf_component_register(
       SRCS "blackbox_storage.c"
       INCLUDE_DIRS "include"
       REQUIRES fatfs sdmmc esp_driver_sdspi esp_driver_spi esp_driver_gpio vfs
       PRIV_REQUIRES log freertos
   )
   ```
   - `SRCS`: memberi tahu compiler file `.c` mana yang harus dikompilasi.
   - `INCLUDE_DIRS`: memberi tahu bahwa folder `include/` berisi header publik (`blackbox_storage.h`).
   - `REQUIRES` & `PRIV_REQUIRES`: memberi tahu ESP-IDF driver bawaan apa saja yang dibutuhkan oleh komponenmu (seperti `fatfs`, `sdmmc`, `esp_driver_sdspi`, `freertos`, dan `log`).

---

## 6. Fungsi-Fungsi Utama di `blackbox_storage`

Berikut fungsi utama yang wajib kamu kenal:

1. **`bb_storage_init(const bb_storage_config_t *cfg)`**:
   Memeriksa apakah konfigurasi pin dan path valid, membuat mutex FreeRTOS, menginisialisasi bus SPI (jika `manage_spi_bus == true`), lalu me-*mount* microSD ke `/sdcard`. Jika mount gagal, fungsi ini otomatis memanggil `spi_bus_free()` supaya tidak ada kebocoran resource.
2. **`bb_storage_validate_event(const bb_event_record_t *event)`**:
   Penjaga gerbang (*boundary validation*). Memastikan pointer tidak `NULL`, `format_version == 1`, `event_id` valid (`1 .. 0x0FFFFFFF`), dan `sample_count` tidak nol serta tidak melebihi `BB_STORAGE_MAX_SAMPLES` (`2048`).
3. **`bb_storage_format_event_csv(...)`**:
   Mengubah struct `bb_event_record_t` di RAM menjadi teks CSV lengkap dengan metadata dan nilai checksum CRC-32 di bagian bawah (`# END_EVENT`).
4. **`bb_storage_write_event(const bb_event_record_t *event, ...)`**:
   Fungsi inti untuk menyimpan rekaman event ke microSD menggunakan metode dua tahap (`.TMP` -> `.CSV`).
5. **`bb_storage_verify_event_file(...)`**:
   Membaca kembali file `.CSV` dari kartu SD, mengecek apakah baris `# END_EVENT` ada, apakah jumlah sampel cocok, dan menghitung ulang CRC-32 dari seluruh angka sampel untuk memastikan tidak ada 1 angka pun yang korup/berubah!
6. **`bb_storage_list_completed_events(...)`**:
   Membaca isi folder `/sdcard` dan hanya mengembalikan daftar file yang sudah selesai (`E0000001.CSV`), sambil menyembunyikan file `.TMP` yang belum selesai ditulis.
7. **`bb_storage_deinit()`**:
   Melepas mount kartu SD (`esp_vfs_fat_sdcard_unmount`) dan membebaskan bus SPI dengan aman.

---

## 7. Bagaimana Aliran Data Event Masuk ke Dalam File? (Two-Phase Write)

Ketika `blackbox_core` mendeteksi guncangan/benturan, Core memanggil `bb_storage_write_event(&event, ...)`. Berikut langkah demi langkah yang terjadi di dalam [`blackbox_storage.c`](../components/blackbox_storage/blackbox_storage.c):

```text
[Struct bb_event_record_t dari Core]
         │
         ▼
1. Validasi input (bb_storage_validate_event) & Kunci Mutex (bb_lock)
         │
         ▼
2. Buat 2 nama file 8.3 (bb_storage_build_event_paths):
   - Sementara : /sdcard/E0000001.TMP
   - Final     : /sdcard/E0000001.CSV
         │
         ▼
3. Cek apakah /sdcard/E0000001.CSV sudah ada?
   - Jika sudah ada -> Tolak (ESP_ERR_INVALID_STATE) agar tidak menimpa rekaman lama!
         │
         ▼
4. Buka /sdcard/E0000001.TMP -> Tulis Header Metadata + Baris Sampel + Footer CRC-32:
   # BLACKBOX_EVENT_FORMAT,v1
   ...
   0,5000000,-20,1,10,-15,998,100,-30,10
   ...
   # END_EVENT,count=4,crc32=0x90C76B22
         │
         ├──► Jika gagal di tengah jalan (misal SD penuh / error I/O):
         │    Tutup file -> Hapus /sdcard/E0000001.TMP -> Set status FULL/WRITE_ERROR
         ▼
5. Jika semua baris berhasil ditulis:
   fflush(fp) -> fsync(fileno(fp)) -> fclose(fp)
         │
         ▼
6. Ubah nama file (rename):
   /sdcard/E0000001.TMP  ──►  /sdcard/E0000001.CSV
```

**Kenapa harus lewat `.TMP` dulu baru di-`rename` ke `.CSV`?**  
Bayangkan pengguna sedang membuka halaman web dashboard (`blackbox_interface`) lewat Wi-Fi tepat ketika alat sedang menulis 1000 baris sampel ke microSD. Kalau kita langsung menulis ke `E0000001.CSV`, web server bisa mengunduh file yang baru terisi separuh! Dengan menulis ke `.TMP` dulu dan baru mengubah namanya menjadi `.CSV` di langkah paling akhir, `bb_storage_list_completed_events()` menjamin web server **hanya pernah melihat file `.CSV` yang sudah 100% utuh**.

---

## 8. Apa Saja yang Bisa Salah pada Penyimpanan microSD?

Di sistem tertanam nyata, penyimpanan microSD sering mengalami gangguan. Berikut masalah yang bisa terjadi dan bagaimana kode kamu menanganinya:

1. **Kartu SD Lupa Dipasang atau Kabel Longgar Saat Boot:**  
   `esp_vfs_fat_sdspi_mount()` akan mengembalikan kode error. Fungsi `bb_storage_init()` menangkapnya, membatalkan inisialisasi bus SPI (`spi_bus_free`), menyetel status ke `BB_STORAGE_STATUS_MOUNT_FAILED`, dan mengembalikan kode error ke Core sehingga ESP32-S3 **tidak crash/reboot berulang-ulang**.
2. **Kartu SD Penuh Saat Menulis (`ENOSPC`):**  
   `fprintf()` atau `fflush()` akan gagal. Kode kita melompat ke label `cleanup_error:`, menutup file, menghapus file `.TMP` yang belum selesai supaya tidak menyampah di kartu SD, dan mengubah status ke `BB_STORAGE_STATUS_FULL` (`ESP_ERR_NO_MEM`).
3. **Listrik Mati Mendadak Saat Menulis (Power Loss):**  
   Jika baterai/kabel USB dicabut paksa tepat saat kartu SD sedang menulis:
   - Jika putus sebelum `rename()`, file masih bernama `.TMP` sehingga tidak akan dianggap sebagai rekaman sah oleh `blackbox_interface`.
   - Jika file terpotong, baris `# END_EVENT,count=...,crc32=...` di bagian paling bawah tidak akan terbentuk atau nilai CRC-32-nya tidak cocok saat diperiksa oleh `bb_storage_verify_event_file()`.
   - **Batasan Jujur:** Sistem berkas FAT32 sendiri tidak tahan mati listrik mendadak (*not power-loss atomic*) pada level tabel alokasi sektornya. Jadi selalu dokumentasikan batasan ini bila ditanya dosen.

---

## 9. Bagaimana `blackbox_storage` Berintegrasi dengan FreeRTOS?

Prinsip penting di arsitektur proyek kita:
> **Sebuah komponen library (`blackbox_storage`) BUKAN berarti harus membuat task FreeRTOS sendiri.**

Mengapa?
- Menulis ke microSD lewat SPI adalah operasi yang **lambat dan waktu tunggunya tidak pasti** (bisa 5 ms, bisa juga 150 ms jika kartu microSD sedang merapikan memori flash internalnya).
- Sementara itu, pembacaan sensor gerak (MPU6050 dan ADXL345) harus jalan konsisten setiap 10 ms (100 Hz).
- Oleh karena itu:
  1. **Task Sensor / Core (Prioritas Tinggi):** Membaca sensor, mendeteksi benturan, lalu menaruh paket data event ke dalam **FreeRTOS Queue**. Task ini **dilarang keras** memanggil `bb_storage_write_event()` secara langsung.
  2. **Task Penyimpanan / Writer Task di Core (Prioritas Rendah):** Menunggu kiriman data dari Queue, lalu memanggil `bb_storage_write_event()` yang sifatnya sinkron (*blocking*). Sementara task penyimpanan menunggu kartu SD selesai menulis, FreeRTOS tetap bisa menjalankan task sensor 100 Hz tanpa terganggu!
  3. **Mutex Internal di `blackbox_storage` (`s_ctx.mutex`):** Di dalam `blackbox_storage.c`, kita memakai `xSemaphoreCreateMutex()`, `xSemaphoreTake()`, dan `xSemaphoreGive()` di dalam `bb_lock()` dan `bb_unlock()`. Gunanya: kalau task HTTP (`blackbox_interface`) memanggil `bb_storage_list_completed_events()` bersamaan saat task penyimpanan sedang memanggil `bb_storage_write_event()`, keduanya akan mengantre dengan tertib dan tidak bertabrakan.

---

## 10. Cara Menjalankan Ulang Pengujian di Laptop Kamu

Kamu bisa menjalankan kembali seluruh pengujian kapan saja lewat terminal PowerShell di VS Code dengan satu perintah:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_tests.ps1
```

Apa yang dilakukan perintah itu?
1. **Step 1 (Cross-Compile ESP32-S3):** Memanggil compiler resmi `xtensa-esp32s3-elf-gcc` (GCC 14.2.0) untuk mengompilasi `blackbox_storage.c` dan `main.c` menggunakan header asli **ESP-IDF v5.4 (`esp32s3`)** dengan aturan super ketat `-Wall -Wextra -Werror` (semua peringatan dianggap error). Hasilnya: **PASS (0 warnings, 0 errors)**.
2. **Step 2 (Host Unit & Fault-Injection Tests):** Mengompilasi `blackbox_storage.c` bersama [`tests/test_blackbox_storage_host.c`](../tests/test_blackbox_storage_host.c) menjadi program `.exe` di Windows dan menjalankan 7 skenario pengujian otomatis (mulai dari uji salah pin, gagal mount, pembuatan CSV + CRC-32, deteksi file korup, sampai simulasi SD card penuh). Hasilnya: **Summary: 7/7 passed, 0 failed**.
