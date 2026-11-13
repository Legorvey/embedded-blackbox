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
│   ├── build-and-test-log.txt           # Log hasil cross-compile & unit test aktual
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
- **Framework Target:** ESP-IDF v5.4 (`release/v5.4`, diuji terhadap header SDK `v5.4.1-1-g2f7dcd862a` dan compiler `xtensa-esp32s3-elf-gcc` 14.2.0)
- **Kondisi Lingkungan Saat Ini:**
  - Cross-compiler `xtensa-esp32s3-elf-gcc` (GCC 14.2.0) dan SDK header/library ESP-IDF v5.4 untuk `esp32s3` tersedia secara lokal melalui paket toolchain Espressif (`Arduino15/packages/esp32`).
  - CLI `idf.py` standalone belum terpasang di `PATH` sistem global; untuk melakukan *full firmware link* dan *flashing* menggunakan `idf.py build flash monitor`, instalasi penuh ESP-IDF v5.4 Command Prompt / VS Code ESP-IDF Extension diperlukan.

---

## 4. Cara Menjalankan Pengujian & Build

### A. Verifikasi Cross-Compile ESP32-S3 & Unit Test Host (Dapat Dijalankan Sekarang)

Jalankan script pengujian otomatis dari PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_tests.ps1
```

Script ini menjalankan verifikasi otomatis dalam dua tahap:
1. **Cross-Compilation (`xtensa-esp32s3-elf-gcc -Wall -Wextra -Werror`)**: Mengompilasi `components/blackbox_storage/blackbox_storage.c` dan `main/main.c` terhadap header asli ESP-IDF v5.4 (`esp32s3`), serta memeriksa strict C17 compilation pada `tests/test_blackbox_storage_host.c`.
2. **Host Unit, Fault-Injection & Concurrency Tests**: Mengompilasi dan menjalankan 8 kelompok uji via **MinGW GCC (`-std=gnu11 -Wall -Wextra -Werror`)** dan **MATLAB `lcc64`** (validasi konfigurasi/GPIO ESP32-S3, lifecycle & pembersihan gagal unmount, validasi event & overflow guard, penamaan file 8.3 `.TMP`/`.CSV`, serialisasi CSV & CRC-32 `0x10BF5ABE`, verifikasi CSV ketat/korupsi/CRLF/2048 sampel, fault injection single-point `fopen`–`fsync`–`rename`/`ENOSPC` & pemulihan status, filter direktori pada listing, `self_test` dengan `BBTEST00`, serta stress test konkurensi 4 thread).

### B. Build Firmware Penuh dengan ESP-IDF Command Prompt (Saat `idf.py` Aktif)

```bash
idf.py build
```

> **Peringatan Hardware:** Pin GPIO SPI di `main/main.c` dan `BB_STORAGE_CONFIG_DEFAULT()` sengaja disetel ke `GPIO_NUM_NC` (`-1`) karena pinout board ESP32-S3 tim belum dikonfirmasi. Jangan melakukan `idf.py flash` sebelum pin MOSI, MISO, SCLK, dan CS dikonfirmasi sesuai skematik board dan modul microSD.

---

## 5. Status Implementasi & Verifikasi Saat Ini (6 Tingkat)

| Tingkat Verifikasi | Status Saat Ini | Detail Terverifikasi |
|---|---|---|
| 1. **Diimplementasikan di kode** | **SELESAI** | `components/blackbox_storage/` (`include/blackbox_storage.h`, `blackbox_storage.c`, `CMakeLists.txt`), `sdkconfig.defaults`, `main/main.c` |
| 2. **Diujikan secara unit test di host** | **PASS (8/8)** | Lulus **8/8 suites** pada **MinGW GCC 4.9.2 (`-std=gnu11 -Wall -Wextra -Werror`)** & **MATLAB `lcc64`** (termasuk konkurensi 4 thread) |
| 3. **Cross-compile objek target `esp32s3`** | **PASS** | `blackbox_storage_esp32s3.o` (`147.784` B), `main_esp32s3.o` (`15.004` B) via `xtensa-esp32s3-elf-gcc` 14.2.0 terhadap SDK ESP-IDF v5.4 (`0 warnings`) |
| 4. **Full link firmware ESP-IDF (`.elf`/`.bin`)** | **NOT RUN** | `idf.py` standalone / CMake / Ninja / VS Code ESP-IDF Extension tidak terpasang di mesin lokal |
| 5. **Perilaku hardware fisik microSD** | **UNVERIFIED ON HARDWARE** | Tidak ada board ESP32-S3 terhubung ke port serial dan pin GPIO SPI tim belum ditetapkan |
| 6. **Integrasi komponen tim lainnya** | **INTEGRATION PENDING** | Menunggu komponen Anggota 1 (`imu_i2c`), 2 (`imu_spi`), 4 (`interface`), dan 5 (`core`) |
