# PSRH-043 Integrated Resource Measurement

- Commit under review: `c2a0ff09d70775a9d582bb3e8a71e455cfb49529`
- Baseline for delta: `5044f9f854efdd4a9da899a357682c71605ec707`
- Build directory: `/tmp/psrh-043-phase5-integration-build`
- Date: `2026-08-09`

## Bounded diagnostic implementation

- Current free heap and lifetime minimum free heap are captured through the
  heap capability API.
- Reset reason is classified into normal, software, panic, watchdog,
  brownout, or unknown.
- Task stack high-water marks are captured through the existing FreeRTOS trace
  facility and converted from `StackType_t` words to bytes while the scheduler
  is suspended.
- The task snapshot has exactly 32 static entries. The true task count,
  captured count, capacity, and truncation flag are logged. A task set beyond
  the fixed capacity is reported as `truncated=yes`; no runtime allocation is
  used to enlarge the snapshot.
- Task names are copied into fixed module-owned buffers while the scheduler is
  suspended; no task-control-block name pointer is used after resumption.
- The 16 KiB heap value is log-only. It does not reset, enter safe mode, alter
  watchdog behavior, or change allocation policy.
- No application task stack constant was increased. The existing main-task
  stack constants remain 6144 (state machine), 3072 (button), 3072 (UI),
  8192 (network), 12288 (Matter adapter), and 4096 (config).

## Static target measurement

`idf.py -B /tmp/psrh-043-phase5-integration-build size` completed with exit
code 0. The integrated application reports:

| Measurement | Baseline `5044f9f` | Integrated | Delta |
|---|---:|---:|---:|
| Application BIN | 1,904,000 B (`0x1d0d80`) | 1,906,400 B (`0x1d16e0`) | +2,400 B |
| Flash Code | 1,751,112 B | 1,753,510 B | +2,398 B |
| DIRAM | 241,781 B | 243,701 B | +1,920 B |
| DIRAM `.bss` | 88,968 B | 90,888 B | +1,920 B |
| DIRAM `.data` | 18,077 B | 18,077 B | +0 B |
| LP SRAM | 24 B | 24 B | +0 B |
| Total image reported by size | 1,903,897 B | 1,906,295 B | +2,398 B |

The `size-files --format csv` result attributes `health_diag.c.obj` with
872 B of Flash Code and 1,920 B of DIRAM `.bss`. The ELF symbol table gives
the fixed storage directly:

| Symbol | Size |
|---|---:|
| `s_task_records` (32 × 24-byte records) | `0x300` / 768 B |
| `s_task_status` (32 × target `TaskStatus_t`) | `0x480` / 1,152 B |
| Fixed diagnostic storage total | 1,920 B |

The measured increase is bounded and matches the intended static snapshot;
there is no evidence of an unbounded allocation or stack-size increase.

## Runtime and hardware evidence

The integrated ELF is available, but no board, serial port, debugger, or
hardware lease was used in this integration worktree. Therefore the following
remain explicitly deferred rather than marked PASS:

- runtime current/minimum heap values and per-task stack high-water marks;
- verification that the runtime task count stays within the 32-entry bound;
- Wi-Fi disconnect/recovery and sensor failure/recovery capture;
- Matter/BLE commissioning, controller recovery, power-cycle, and watchdog
  observation.

The static Flash/RAM measurement above is complete for the integrated image;
runtime resource telemetry and hardware behavior require a separately
authorized Hardware Lab run.
