# START HERE — Embedded Black Box / Storage

> **Untuk anggota yang menangani Storage.** Paket ini adalah **panduan dan bahan awal**, **belum firmware yang sudah berjalan**. Dibuat dari PPT dosen, brief ketua, dan PDF kompilasi percakapan/instruksi. Jangan menyebut build/hardware sudah lulus sebelum diuji.

## Urutan praktis untuk kamu

1. **Buka project ESP-IDF yang sudah ada** di VS Code/Antigravity. Pastikan ada `CMakeLists.txt` root. **Jangan buat project ESP-IDF kedua di dalam project pertama.** Jika project ESP-IDF belum ada, minta kode project dari ketua atau biarkan agent membuat scaffold minimal dengan persetujuan.
2. Tempatkan **isi** paket ini pada **root project/repository** yang sama. Jika nama file sama, **bandingkan dulu** dan jangan overwrite versi ketua.
3. Baca `docs/00-PETA-KONTEKS.md` dan `docs/01-STORAGE-TUGAS-GUE.md` (10–15 menit).
4. Periksa `docs/05-KEPUTUSAN-DAN-PERTANYAAN.md`, terutama versi ESP-IDF, pin SPI, serta pembagian bus dengan ADXL345. Yang belum diputuskan harus berstatus *TODO*, bukan dikarang.
5. Buka Antigravity di **root project**. Tempel seluruh isi `prompts/ANTIGRAVITY-STORAGE-MASTER.md`. Minta agent menjalankan **Fase 0 → Fase 1** dulu. Untuk perubahan file/firmware, agent harus bisa menjelaskan apa yang dibuat dan mana yang sudah diuji.
6. Setelah ada hasil nyata, isi `docs/03-PENGUJIAN-DAN-BUKTI.md` dan `docs/04-HANDOFF-PRESENTASI.md` dengan **bukti sebenarnya** (build log, foto hardware, hasil baca file). Sebelum Senin, kirim link GitHub dan rangkuman singkat ke ketua.

## Bikin GitHub baru (satu repo untuk satu kelompok)

1. GitHub → **New repository** → `embedded-blackbox` → **Private**. Jangan tambah README/.gitignore/license dari web jika project lokal sudah berisi file supaya tidak memicu konflik awal.
2. Pastikan kamu berada di **root ESP-IDF project**, bukan folder acak berisi dokumen saja.
3. Jika BELUM ada `.git/`:

   ```bash
   git init -b main
   git add .
   git commit -m "docs: initialize embedded blackbox context"
   git remote add origin https://github.com/USERNAME/embedded-blackbox.git
   git push -u origin main
   git switch -c feat/storage
   ```

   Ganti `USERNAME`; jalankan `git status` dan periksa file staged sebelum commit. Jika sudah ada Git, **jangan ulang `git init`**: periksa `git remote -v`, branch, lalu sambungkan ke remote yang tepat.
4. Undang anggota lewat Settings → Collaborators; **jangan kirim password/token**. Untuk kerja berikutnya: branch fitur → commit → push → PR ke `main`; jangan semua orang push langsung ke `main`.
5. Jangan unggah screenshot chat grup atau dokumen yang mengandung data pribadi tanpa persetujuan. Rangkuman teknis disediakan di `docs/00-PETA-KONTEKS.md`.

## Yang harus diserahkan paling tidak

- Folder `components/blackbox_storage/` berisi **kode nyata** (`include/*.h`, `.c`, `CMakeLists.txt`) setelah agent mengimplementasikannya; saat ini hanya README placeholder.
- README penggunaan API dan deskripsi referensi/upstream, versi, serta perubahan dari baseline.
- Build log **jika benar telah dijalankan**, atau label jelas `BELUM DIUJI`.
- Bukti tes SD card (mount/tulis/baca/error), atau hambatan hardware yang benar-benar ada.
- `docs/04-HANDOFF-PRESENTASI.md` agar ketua bisa menjelaskan bagianmu saat kamu tidak hadir.

## Jangan sampai lupa

- **VS Code = editor**; **ESP-IDF = framework + build tools + driver**; **FreeRTOS = task scheduler**; **GitHub = kolaborasi/riwayat kode**.
- Jangan `format_if_mount_failed = true` tanpa izin, karena dapat menghapus data SD.
- Jangan menebak GPIO atau model ESP32-S3. Jangan menjanjikan anti-korupsi SD saat power loss.
- Lihat `source_refs/README.md` untuk referensi resmi.
