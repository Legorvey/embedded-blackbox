# 00 — Peta konteks tugas, brief ketua, dan obrolan grup

Dokumen ini adalah **ringkasan provenance**, bukan instruksi dosen baru. Sumber primer yang dipakai:

1. `tugas sister.pptx` yang dikirim user (5 slide): diagram ESP32-S3 + display, I2C, SPI, SD card, SoftAP, scheduler; instruksi *pelajari library → save as → edit dan reduce → buat `.h` / `.c` / `.cpp`*; task sederhana di Arduino IDE menggunakan FreeRTOS; contoh header dan kode Wi-Fi; `xTaskCreate`, semaphore, dan task analog read. **Inilah acuan langsung dari bahan dosen yang tampak.**
2. `embedded_blackbox_agent_brief.md` dari ketua: rancangan sistem **Embedded Event Black Box** untuk ESP32-S3, lima pemilik komponen, ESP-IDF + FreeRTOS, target sekitar 3 minggu, budget Rp300 ribu, sensor MPU6050 (I2C) & ADXL345 (SPI), microSD via SPI, OLED/LED, SoftAP + HTTP. Ini **brief/rencana tim, belum keputusan resmi dosen**.
3. `Instructions TUBES Sister(1).pdf` (30 halaman): **kompilasi** demonstrasi ide Black Box, tabel lima komponen, percakapan dengan AI, salinan instruksi dosen, brief ketua, link framework dan screenshot chat grup. Tidak semua 30 halaman adalah perintah langsung dosen.
4. Screenshot chat grup di PDF halaman 27–30: anggota membahas pembagian library (termasuk Core dan Storage), referensi contoh/prototipe, diskusi bahwa **VS Code adalah editor, bukan framework**, dan pilihan **ESP-IDF**. Percakapan adalah konteks diskusi, bukan bukti semua pilihan/fitur sudah disahkan. Ringkasan ini tidak memuat nama, nomor telepon, atau salinan screenshot personal.

## Tujuan alat menurut rencana tim

Prototipe mendeteksi gerakan/benturan sederhana melalui sensor. Data bergerak melalui jalur:

`MPU6050 & ADXL345 → acquisition/core/event detector → pre/post-event record → blackbox_storage → SD card → interface (HP/laptop)`

FreeRTOS membantu penjadwalan/komunikasi task. Demo yang diusulkan: gerakkan/ketuk kotak, sistem merekam event dengan sampel sebelum dan sesudah, lalu file dapat dilihat melalui jaringan SoftAP. **Tidak ada klaim sebagai pendeteksi kecelakaan tersertifikasi.**

## Pembagian kepemilikan menurut brief (bukan jaminan pembagian orang final)

| Komponen | Tanggung jawab |
|---|---|
| `blackbox_imu_i2c` | MPU6050, konfigurasi/read sensor I2C, error |
| `blackbox_imu_spi` | ADXL345, driver SPI, data/interrupt |
| **`blackbox_storage` (fokus kamu)** | microSD SDSPI, FAT/VFS, penyimpanan record event, integritas & error |
| `blackbox_interface` | LED/OLED, SoftAP, status, daftar/unduh rekaman |
| `blackbox_core` | state machine, buffer, deteksi event, koordinasi FreeRTOS |

## Yang berasal dari dosen vs usulan ketua

| Materi | Dosen (PPT) | Brief/AI/kelompok |
|---|---|---|
| ESP32-S3, I2C sensor, SPI sensor, SD, display, SoftAP, scheduler | Ada pada diagram | Dipenuhi dalam desain Black Box |
| Pelajari, ubah/reduce library; file `.h` dan `.c/.cpp` | Tertulis | Satu komponen reusable per anggota |
| Task sederhana FreeRTOS menggunakan Arduino IDE | Tertulis | Framework utama yang diusulkan ESP-IDF |
| Alat Black Box & event detection | Tidak ditentukan eksplisit pada PPT yang tersedia | Usulan kelompok |
| 100 Hz, 5 detik pre + 5 detik post | Tidak ditentukan eksplisit di PPT | **Target tentatif**, verifikasi dahulu |
| 3 minggu dan Rp300.000 | Tidak terlihat sebagai mandat langsung di PPT | Batas dalam brief ketua |
| GitHub, branch, PR, Antigravity | Tidak disebut sebagai kewajiban PPT | Workflow yang bisa dipilih kelompok |

## Hal yang belum boleh diasumsikan

- Apakah dosen mewajibkan memodifikasi langsung library open-source vs membuat komponen baru berbasis driver resmi.
- Versi tepat ESP-IDF, board/pinout, pin SPI, jenis SD breakout dan wiring.
- Apakah SD dan ADXL345 berbagi bus SPI; ini memengaruhi urutan inisialisasi dan pemilik bus.
- Bentuk `event_record`, ukuran payload, lifetime buffer, sinkron/asinkron API, dan hak file akses dari HTTP server.
- Status persetujuan desain & siapa mengerjakan bagian mana; chat membahas, tidak otomatis mengesahkan semuanya.

## Strategi untuk anggota Storage

Fokus pada komponen yang bisa diuji terpisah terlebih dahulu: mounting SD, menulis dan membaca kembali data uji, laporan error. Lalu sepakati kontrak Core → Storage dan Interface → file selesai. Jangan menulis detektor benturan atau antarmuka web di dalam komponen Storage.
