# Builder Handoff: `<task-id>`

## Scope and baseline

- Task contract: `agent/tasks/<task-id>.yml`
- Baseline commit: `<full commit>`
- Final commit(s): `<full commit(s)>`
- Worktree/branch: `<local path>` / `<branch>`
- Changed files: `<list>`
- Ownership check: `<all changes inside owned_paths / exceptions approved by whom>`

## Implementation summary

Describe behavior changes, concurrency/timeout handling, storage lifecycle, and relevant failure behavior. State explicitly whether public interfaces, hardware assumptions, Matter model, partitions, or OTA behavior changed.

## Machine verification

| Check | Exact command | Environment/tool versions | Result/exit code | Evidence location |
|---|---|---|---|---|
| Format/static analysis | `<command>` | `<versions>` | `<PASS/FAIL>` | `<path or CI artifact>` |
| Host tests | `<command>` | `<versions>` | `<PASS/FAIL>` | `<path or CI artifact>` |
| ESP32-C6 build | `<command>` | `<IDF/ESP-Matter versions>` | `<PASS/FAIL>` | `<path or CI artifact>` |
| Hardware/HIL | `<command or N/A>` | `<board/PCB/probe>` | `<PASS/FAIL/BLOCKED>` | `<sanitized summary/raw log location>` |

## Resource and resilience evidence

- Flash/RAM delta: `<measurement or gap>`
- Task stack high-water marks: `<measurement or gap>`
- Power/temperature impact: `<measurement or gap>`
- Wi-Fi disconnect/recovery: `<result or reason deferred>`
- Sensor failure/recovery: `<result or reason deferred>`
- Matter/BLE behavior: `<result or reason deferred>`

## Risks and decisions needed

List unresolved defects, unverified acceptance criteria, assumptions, reproducibility limits, and any issue that requires human approval. Never call an unperformed check PASS.
