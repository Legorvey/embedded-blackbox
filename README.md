# Embedded Event Black Box (ESP32-S3 / ESP-IDF)

Proyek kelompok mata kuliah **Sistem Tertanam (Embedded Systems)** berbasis **ESP32-S3** dan **ESP-IDF / FreeRTOS**. Sistem ini dirancang untuk mendeteksi kejadian gerak/benturan signifikan dari dua sensor gerak pada bus berbeda (I2C dan SPI), menyimpan rekaman sampel *pre-event* dan *post-event* ke kartu **microSD via SPI (SDSPI)**, serta menampilkan status dan daftar rekaman melalui **OLED/LED** dan **Wi-Fi SoftAP HTTP Dashboard**.

> **Catatan Penting:** Proyek ini adalah **prototipe rekayasa firmware tertanam untuk pembelajaran akademis**, bukan perekam kecelakaan kendaraan tersertifikasi keselamatan (*safety-certified crash recorder*).

---

## 1. Arsitektur & Pembagian Komponen Tim (5 Mahasiswa)

Sesuai rancangan arsitektur tim (`embedded_blackbox_agent_brief.md`), sistem dibagi menjadi 5 komponen reusable ESP-IDF di bawah direktori `components/`:

| No | Komponen | Tanggung Jawab Utama | Status di Workspace Ini |
|---|---|---|---|
| 1 | `blackbox_imu_i2c` | Akuisisi sensor gerak MPU6050 (akselerometer + giroskop) via I2C | Milik Anggota 1 (Belum diintegrasikan) |
| 2 | `blackbox_imu_spi` | Akuisisi sensor akselerometer ADXL345 via SPI | Milik Anggota 2 (Belum diintegrasikan) |
| 3 | **`blackbox_storage`** | **Inisialisasi/mount microSD (SDSPI + FATFS VFS), serialisasi event CSV, siklus file `.TMP` -> `.CSV`, verifikasi CRC-32, dan penanganan error** | **Milik Anggota 3 — DIIMPLEMENTASIKAN** |
| 4 | `blackbox_interface` | Indikator lokal (OLED/LED), Wi-Fi SoftAP, dan HTTP server untuk melihat/mengunduh file event yang sudah selesai | Milik Anggota 4 (Belum diintegrasikan) |
| 5 | `blackbox_core` | Orkestrasi FreeRTOS task/queue, *rolling pre-event buffer*, deteksi event, dan *system state machine* | Milik Anggota 5 (Belum diintegrasikan) |

---

## 2. Struktur Direktori Proyek

```text
embedded-blackbox-storage-starter/
├── .github/
│   └── pull_request_template.md
├── .gitignore
├── AGENTS.md
├── START_HERE.md
├── README.md
├── CMakeLists.txt                       # Root ESP-IDF CMakeLists
├── sdkconfig.defaults                   # Default target esp32s3 & FATFS config
├── main/
│   ├── CMakeLists.txt
│   └── main.c                           # Entry point minimal & smoke test Storage
├── components/
│   └── blackbox_storage/
│       ├── CMakeLists.txt               # Registrasi komponen ESP-IDF & dependensi
│       ├── include/
│       │   └── blackbox_storage.h       # Kontrak API publik Storage
│       ├── blackbox_storage.c           # Implementasi SDSPI, FATFS VFS, CSV, CRC-32
│       └── README.md                    # Dokumentasi teknis komponen Storage
├── tests/
│   ├── test_blackbox_storage_host.c     # Unit, fault-injection & concurrency test suite (8 test suites)
│   └── run_tests.ps1                    # Script otomatis cross-compile ESP32-S3 + host tests (GCC & lcc64)
├── evidence/
│   ├── build-and-test-log.txt           # Log hasil host tests, cross-compile, & full ESP-IDF v5.4.4 build aktual
│   └── example-event-simulated.csv      # Contoh berkas CSV hasil serialisasi (SIMULASI, 441 bytes, CRC 0x10BF5ABE)
├── docs/
│   ├── 00-PETA-KONTEKS.md               # Peta sumber tugas dosen vs brief ketua
│   ├── 01-STORAGE-TUGAS-GUE.md          # Scope, milestone, dan status pengerjaan Storage
│   ├── 02-KONTRAK-INTEGRASI.md          # Kontrak API dengan Core & Interface + pertanyaan terbuka
│   ├── 03-PENGUJIAN-DAN-BUKTI.md        # Tabel rencana uji dan bukti terverifikasi
│   ├── 04-HANDOFF-PRESENTASI.md         # Materi handoff untuk presentasi Senin, 16 Nov 2026
│   ├── 05-KEPUTUSAN-DAN-PERTANYAAN.md   # Log keputusan teknis & daftar konfirmasi tim
│   └── 06-BELAJAR-STORAGE.md            # Modul belajar lengkap (SPI, SDSPI, FATFS, FreeRTOS)
├── prompts/
│   └── ANTIGRAVITY-STORAGE-MASTER.md
└── source_refs/
    └── README.md
```

---

## 3. Prasyarat Lingkungan (Environment Prerequisites)

- **Target Mikrokontroler:** `esp32s3` (ESP32-S3)
- **Framework Target:** **ESP-IDF `v5.4.4`** (CMake `3.30.2`, Ninja `1.12.1`, Python `3.13.2`, `xtensa-esp32s3-elf-gcc` `14.2.0`; juga tetap teruji terhadap header SDK Arduino15 `release/v5.4` `v5.4.1-1-g2f7dcd862a`)
- **Kondisi Lingkungan Saat Ini:**
  - Lingkungan penuh **ESP-IDF `v5.4.4`** (`C:\Espressif\v5.4.4\esp-idf`) beserta `idf.py`, CMake `3.30.2`, Ninja `1.12.1`, dan toolchain `xtensa-esp32s3-elf-gcc` sudah terpasang dan dikonfigurasi (`.vscode/settings.json` menunjuk ke setup `v5.4.4`, tetap lokal / tidak di-commit).
  - Pada terminal PowerShell biasa di luar ESP-IDF PowerShell / VS Code extension, aktifkan environment dengan `Microsoft.v5.4.4.PowerShell_profile.ps1` (dan pastikan direktori `cmake\3.30.2\bin` masuk `PATH`).
  - Paket toolchain `Arduino15/packages/esp32` juga tetap dipakai sebagai pemeriksaan cepat lintas-header mandiri oleh `tests/run_tests.ps1` Step 1.

---

## 4. Cara Menjalankan Pengujian & Build

### A. Verifikasi Cross-Compile Cepat & Unit Test Host

Jalankan script pengujian otomatis dari PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_tests.ps1
```

Script ini menjalankan verifikasi otomatis dalam dua tahap:
1. **Cross-Compilation Cepat (`xtensa-esp32s3-elf-gcc -Wall -Wextra -Werror`)**: Mengompilasi `components/blackbox_storage/blackbox_storage.c` dan `main/main.c` terhadap header SDK ESP-IDF v5.4 (`esp32s3`), serta memeriksa strict C17 compilation pada `tests/test_blackbox_storage_host.c`.
2. **Host Unit, Fault-Injection & Concurrency Tests**: Mengompilasi dan menjalankan 8 kelompok uji via **MinGW GCC (`-std=gnu11 -Wall -Wextra -Werror`)** dan **MATLAB `lcc64`** (validasi konfigurasi/GPIO ESP32-S3, lifecycle & pembersihan gagal unmount, validasi event & overflow guard, penamaan file 8.3 `.TMP`/`.CSV`, serialisasi CSV & CRC-32 `0x10BF5ABE`, verifikasi CSV ketat/korupsi/CRLF/2048 sampel, fault injection single-point `fopen`–`fsync`–`rename`/`ENOSPC` & pemulihan status, filter direktori pada listing, `self_test` dengan `BBTEST00`, serta stress test konkurensi 4 thread).

### B. Full Build Firmware ESP-IDF v5.4.4 (`esp32s3` — Terverifikasi Berhasil)

Di terminal PowerShell/CMD ESP-IDF `v5.4.4`:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py size
```

Output link & konfigurasi yang terverifikasi pada **ESP-IDF `v5.4.4`** (target `esp32s3`):
- Komponen `blackbox_storage` ditemukan dan dikompilasi menjadi `libblackbox_storage.a` (`187.364` B; `0` warnings / `0` errors).
- `build/bootloader/bootloader.bin` (`20.928` B / `0x51c0` B, `36%` ruang sisa).
- `build/partition_table/partition-table.bin` (`3.072` B; `nvs` `24K` @ `0x9000`, `phy_init` `4K` @ `0xf000`, `factory` `1M` @ `0x10000`).
- `build/embedded_blackbox.elf` (`3.804.372` B) dan `build/embedded_blackbox.bin` (`205.056` B / `0x32100` B; `idf.py size` = `204.936` B, tersisa `0xcdf00` B / `80%` di partisi `factory`).
- *Catatan footprint starter:* karena `main/main.c` menahan `BB_STORAGE_PIN_CS = GPIO_NUM_NC` (`-1`) dan keluar dini sebelum memanggil `bb_storage_init()`, linker *gc-sections* hanya menyertakan `1.186` B dari `libblackbox_storage.a` di ELF starter (ukuran objek terkompilasi penuh `blackbox_storage.c.obj` di ESP-IDF `v5.4.4` adalah `11.335` B `.text`, `8` B `.data`, `208` B `.bss`).

> **Peringatan Hardware & Konfigurasi Flash Sebelum Flashing:**  
> 1. Pin GPIO SPI di `main/main.c` dan `BB_STORAGE_CONFIG_DEFAULT()` sengaja disetel ke `GPIO_NUM_NC` (`-1`) karena pinout board ESP32-S3 tim belum dikonfirmasi.  
> 2. `sdkconfig` masih memakai default flash ESP-IDF **2 MB / DIO / 80 MHz**, tabel partisi **Single factory app (`1M`, tanpa OTA)**, dan **PSRAM nonaktif**. Konfirmasikan ukuran flash, mode SPI flash (`QIO`/`DIO`), keberadaan PSRAM, serta pin `MOSI`/`MISO`/`SCLK`/`CS` fisik sesuai board tim sebelum menjalankan `idf.py flash`.

---

## 5. Status Implementasi & Verifikasi Saat Ini (6 Tingkat)

| Tingkat Verifikasi | Status Saat Ini | Detail Terverifikasi |
|---|---|---|
| 1. **Diimplementasikan di kode** | **SELESAI** | `components/blackbox_storage/` (`include/blackbox_storage.h`, `blackbox_storage.c`, `CMakeLists.txt`), `sdkconfig.defaults`, `main/main.c` |
| 2. **Diujikan secara unit test di host** | **PASS (8/8)** | Lulus **8/8 suites** pada **MinGW GCC 4.9.2 (`-std=gnu11 -Wall -Wextra -Werror`)** & **MATLAB `lcc64`** (termasuk konkurensi 4 thread; data simulasi host) |
| 3. **Cross-compile objek target `esp32s3`** | **PASS** | `blackbox_storage_esp32s3.o` (`147.728` B), `main_esp32s3.o` (`15.004` B) via `xtensa-esp32s3-elf-gcc` 14.2.0 terhadap SDK ESP-IDF v5.4 (`0 warnings`) |
| 4. **Full link firmware ESP-IDF (`.elf`/`.bin`)** | **PASS** | `idf.py build` pada **ESP-IDF `v5.4.4`** (`esp32s3`, CMake `3.30.2`, Ninja `1.12.1`) sukses `0 warnings`: `embedded_blackbox.elf` (`3.804.372` B), `embedded_blackbox.bin` (`205.056` B), `bootloader.bin` (`20.928` B), `partition-table.bin` (`3.072` B) |
| 5. **Perilaku hardware fisik microSD** | **UNVERIFIED ON HARDWARE** | Tidak ada board ESP32-S3 terhubung ke port serial, pin GPIO SPI tim masih `GPIO_NUM_NC`, dan spesifikasi flash/board belum dikonfirmasi |
| 6. **Integrasi komponen tim lainnya** | **INTEGRATION PENDING** | Menunggu komponen Anggota 1 (`imu_i2c`), 2 (`imu_spi`), 4 (`interface`), dan 5 (`core`) |
