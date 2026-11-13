# Agent instructions (Antigravity / coding assistants)

Primary prompt: [`prompts/ANTIGRAVITY-STORAGE-MASTER.md`](prompts/ANTIGRAVITY-STORAGE-MASTER.md).

Rules:
- Preserve existing user and team files. Never delete or overwrite project code/config without inspecting first.
- Inspect actual ESP-IDF version, root project, board, SDK target, sources, Git state before modifications. Do not invent hardware pins.
- Prioritize the **Storage component only**; do not build sensor drivers, web dashboard, or full Core unless explicitly needed for an isolated test harness.
- Separate **facts from uploaded source**, **recommendations**, and **verified outputs**. Never claim successful flash/mount/benchmark without running tests.
- If hardware unavailable, create host/unit test or data simulation where feasible and clearly label `UNVERIFIED ON HARDWARE`.
- Never automatically format SD cards, erase partitions, or run destructive commands without explicit approval and backup.
- Respect original upstream licenses, document provenance and changes; do not copy large vendor source trees indiscriminately.
- Coordinate SPI sharing, shared event type, and concurrent filesystem access through documented open questions; avoid speculative public APIs that are hard to integrate.
- Keep generated output scoped and reviewable: small commits/PRs, build log, docs updated, no secrets or private chat screenshots in public repos.
- Report each phase: Files changed, commands executed, exact logs, PASS/FAIL/NOT RUN, blockers, and next required action.
