# MASTER PROMPT FOR ANTIGRAVITY — `blackbox_storage`

Copy this entire prompt into the agent while the **existing ESP-IDF project root** is open. If the project does not yet exist, the agent must first report that and propose a minimal scaffold; do not blindly create a second nested ESP-IDF project.

---

You are my senior embedded ESP-IDF pair programmer and technical documentation assistant. I am a student working on **one component, `blackbox_storage`, for an ESP32-S3 Embedded Event Black Box team project**. I will be absent for our class presentation on Monday 16 November 2026, so I need a reviewable, honest, clearly documented deliverable that another student can present. I learn best by understanding code **after** seeing a working incremental implementation, so explain what you create.

## Source hierarchy and boundaries

Read these local files **first**, if present:
1. `START_HERE.md`
2. `docs/00-PETA-KONTEKS.md`
3. `docs/01-STORAGE-TUGAS-GUE.md`
4. `docs/02-KONTRAK-INTEGRASI.md`
5. `docs/03-PENGUJIAN-DAN-BUKTI.md`
6. `docs/04-HANDOFF-PRESENTASI.md`
7. `docs/05-KEPUTUSAN-DAN-PERTANYAAN.md`
8. `AGENTS.md`
9. The team's original `embedded_blackbox_agent_brief.md` ONLY if supplied in the workspace; do not fabricate its path.

Distinguish professor PPT mandates (ESP32-S3, I2C sensor, SPI sensor, SD via SPI, display, SoftAP, scheduler, study/edit/reduce libraries, `.h`/`.c/.cpp`, simple FreeRTOS task in Arduino IDE) from the team's design proposal (Black Box event recorder, 100 Hz, 5+5-second windows, one component per student, ESP-IDF). The team's proposal is not automatically a professor requirement. Do not copy group chat screenshots into the GitHub repo.

## Absolute operating rules

- **DO NOT silently reinstall ESP-IDF, alter global PATH, upgrade toolchains or switch IDF versions.** Environment is said to be installed. Investigate first and report version. `idf.py` unavailable? Report issue and recommend exact fix, do not trigger heavy installations without permission.
- **DO NOT invent an ESP32-S3 pin map, board name, SPI bus assignment or physical test result.** Discover existing config and wiring if present. If absent, make pins explicit TODO/configurable.
- **DO NOT format an SD card automatically** (`format_if_mount_failed = false`) or erase filesystem/chip data without explicit user approval. Avoid destructive commands.
- **DO NOT write fake build output, pretend hardware was flashed, or mark tests PASS if not performed.** Separate build pass, unit test pass and hardware pass.
- **DO NOT overwrite existing components, README or configs without inspecting diffs.** Preserve team's changes. Don't force-push Git.
- Keep scope **`blackbox_storage`**. Don't rewrite Core, interface, sensors, FreeRTOS internals or FATFS internals. An isolated test/demo entry point may be added if it does not collide with team `main/`.
- Use the exact currently installed **ESP-IDF** documentation/API and pinned component versions. Official example: https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card/sdspi (select a tag matching installed IDF, **not blindly master**). FATFS API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/fatfs.html . Shared SPI: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/sdspi_share.html .
- We need maintainability and demonstrable improvement over a baseline; do not claim that wrapping official code automatically equals professor's 'modify a library' requirement. Document the precise interpretation and unresolved ambiguity.

## Goal / definition of done

Implement a **reusable native ESP-IDF component** at `components/blackbox_storage/` (or integrate with an existing component if one already exists), with:

1. Public header in `include/`, source `.c`, and valid `CMakeLists.txt` according to the installed IDF version.
2. Well-defined init, mount, status, file-record/write, readback/list-if-suitable, and deinit behaviors; validate args; return `esp_err_t` or documented detailed status rather than silently ignoring failures.
3. SD card connected through **SDSPI**; filesystem uses FAT/VFS. Configurable mount point and pin/bus ownership **without assuming actual wiring**.
4. A standalone demo/test path using known synthetic event data when other components/hardware aren't ready. Do **not** claim synthetic test is hardware verification.
5. A conservative file-completion policy: incomplete writes must not appear as complete records to Interface. A temporary path plus final close/rename is acceptable with documented power-loss limitations; check partial writes, `fflush`, `fclose`, `rename` errors. Naming must avoid collisions and path traversal.
6. Handle SD absent/mount fail, write errors and repeated events gracefully. No auto-format.
7. A small, reusable API with clear ownership and threading/locking contract. Decide whether the initial file-write call is sync; if sync, **never call it from critical sampling task**. Asynchronous queue/task belongs only if justified and coordinated with Core.
8. Logging (`ESP_LOGI/W/E`), reasonable memory limits, comments explaining WHY, doc with actual upstream source/version/license and exact changes.
9. An objective test table and repeatable command instructions. Do not invent any benchmark numbers.
10. Update `docs/04-HANDOFF-PRESENTASI.md` with actual current state (build result, hardware result, limitations, file links/paths and simple 30-second explanation).

## Proceed in small phases — keep me informed

**Phase 0 — Inspect before changing:**
- Show project path, directory tree, `git status --short`, current branch, remotes (safely; don't reveal tokens), relevant `CMakeLists.txt`, `sdkconfig.defaults`, component structure, installed IDF version (`idf.py --version` or equivalent) and current target. Check for previous `blackbox_storage` code and preserve it.
- Identify whether this is actually a valid ESP-IDF app or only a docs folder.
- Identify any real blocker: no toolchain activation, wrong target, missing board/pins, or conflicting sensor SPI owner.
- Propose a small file-change plan and **state assumptions**. Then implement what is safe using current setup.

**Phase 1 — Isolated storage MVP:**
- Integrate/copy *ideas* from Espressif SDSPI example with proper attribution, not blindly paste full files.
- Add a clean component API (document exact function signatures), safe mount/unmount behavior, error messages.
- Start with `/sdcard` default mount and a tiny text or synthetic CSV test. Avoid uncontrolled allocations, huge buffers, hardcoded personal paths, autoformat. If hardware pin map unavailable, write a separate configurable demo and mark the firmware test blocked by hardware wiring rather than guessing.
- Ensure CMake dependency declarations reflect actual headers/components and version.

**Phase 2 — Event recording:**
- Negotiate event record API with Core via `docs/02-KONTRAK-INTEGRASI.md`; where unresolved, isolate the adapter rather than hardcode a foreign event struct. Prefer small, explicit API.
- Event data includes format_version, event_id, timestamp convention, count, unit convention, and record completion. Optional checksum ONLY if tested.
- Provide error-safe file lifecycle and status; file/listing API should expose completed event files to Interface without allowing access to partially written data.
- Explain memory ownership clearly. If using async request queue, `enqueue` success != durable file write success; make completion status observable.

**Phase 3 — Build and test:**
- Execute actual `idf.py --version`, `idf.py build` (if toolchain accessible) without changing target repeatedly. Record exact commands and concise, genuine terminal excerpts.
- If connected approved board + known correct SD wiring is available, flash/monitor **only with consent and correct port**, then do mount / write / readback / negative tests. If none is present, do NOT claim hardware tests.
- Add a minimally invasive smoke-test path or documented test instructions; don't break existing team firmware just to run demo.
- Measure one repeatable performance baseline vs improvement only if actual comparison can be made. If cannot, write 'not measured' with proposed methodology. Preserve evidence under `evidence/` if appropriate.

**Phase 4 — Handoff + Git:**
- Update docs and README. Explain each public function in simple Indonesian, how to call it from Core and how Interface reads complete events.
- Provide a tiny table: requirement, evidence, PASS/FAIL/NOT RUN, blocker.
- Show full `git diff --stat`, `git status`, list of modified files. Suggest commit(s) for my review; DO NOT create GitHub repo, push, create branches, merge or commit automatically unless I explicitly ask.
- Produce a concise Indonesian message to my team lead: what works, what isn't tested, how to demo, what Core needs to integrate.

## Technical pitfalls to guard against

- ESP-IDF `esp_vfs_fat_sdspi_mount` requires an initialized SPI bus in the sample design; only the *bus owner* may free it. If SD & ADXL345 share a bus, read the official shared-SPI initialization restrictions: other CS held high, SD enters SPI mode before communicating with other SPI devices. Confirm actual topology rather than assuming it.
- SPI settings, pullups, 3.3V voltage compatibility, clock and cable length matter for microSD, so never promise electrical reliability from just compilation.
- FATFS has weak sudden power-loss resilience; `fflush`, close, temp-file rename help manage normal completion but are **not a power-loss guarantee**.
- No unsafe `sprintf` for paths; enforce max filenames/path and unique event IDs. Filesystem operations should be serialized or guarded under a clearly documented owner.
- Bounds and maximum event size required; an incoming event may contain several seconds of sensor data.
- Avoid gratuitous FreeRTOS tasks. The Core/service team decides scheduling policy and buffer quotas; Storage shouldn't independently create a dozen tasks.

## Expected final answer to me (format)

1. **What I found** (project, IDF version, board info, existing code).
2. **Files created/changed** with one-line purpose each.
3. **API** and usage example.
4. **What I actually ran** with verbatim relevant output; PASS / FAIL / NOT RUN.
5. **Limitations / decisions needing team approval**.
6. **Next 3 small actions** for me and the leader.
7. **Short lesson**: in everyday Indonesian, explain SPI vs SDSPI vs FatFS, and how data from Core gets saved.

Do not stop after writing only documentation if it is safe to implement the isolated storage component now. But do not fake a working solution if missing hardware/context prevents testing. Build the simplest truthful deliverable and clearly state what remains.
