# Strict host diagnostics — October 20

Read `AGENTS.md` and `HANDOFF_CONTAINING_OWNER_2026-10-20.md` first.
The user uses DeepSeek manually in its free browser chat (DeepThink on, Search
off). Its supplied response is advisory evidence, not authority to bypass
verification. It is preserved under `local/diagnosis-20261020/deepseek-response.md`.

## Diagnostic design and provenance

The preceding probe's 390-second outer timeout can preempt the host watchdog:
its 360+30-second deadline starts at boot, after process loading. The missing
360-second cooperative close remains a separate question. Neither explains
black output without rendering evidence.

The pinned host remains untouched, SHA-256
`597b115f468a5cf18522c8d02e82f15deed82e7e8f55865337e0763da0987e18`.
The game executable remains pinned to
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
All 500 previous modules remain unchanged. Original installed assets remain
reference input only.

A separate diagnostic source snapshot is under `local/diagnosis-20261020/kit/`.
Only six native files were instrumented, plus one private CMake adaptation.
Counters use relaxed atomics; watchdog snapshots are approximate and do not
drive control flow. No scheduler policy, renderer support, guest instruction
semantics, game dispatch, authentication, or assets were changed.

The diagnostic executable SHA-256 is
`d65e2847e3c15468bb5aec92824ba2a22a2472a98059aca890a63d86671718e4`.
This is a new host implementation built from recorded sources, not the pinned
host. Exact source correspondence for the old pinned host remains unproven.
The diagnostic build imports the unchanged baseline compiled game and CRT
archives rather than regenerating or recompiling recovered instructions.
Source hashes, archive hashes, compiler commands/flags, CMake inputs, build logs,
and the instrumentation diff are recorded in `source-build-record.json`.

`prepare.py`, `instrument.py`, `build.py`, `record.py`, and `run.py` reproduce
the local work with fresh output paths. Use `local/recomp-msvc90-venv/bin/python`.
The source copy uses stat-based explicit directory traversal: this volume's
DirEntry directory-type hints fail to recurse in Python rglob/os.walk and rg.
Do not treat those tools' incomplete recursive listings as proof of absence.
The kit formatter was used on the six native diagnostic files. The kit build
API configured and built the isolated host and native tests.

Counters cover clock/message/tick service, normal tick exits, headless stages,
cooperative close requests, scheduler checkpoints, clock pin/guard state,
CPU/GPU presents, shader create/bind calls, raster early-return categories,
completed raster calls, and pixels written. Empty VS and PS are overlapping
counts: one rejected draw can increment both. A completed raster call is not
proof of visible output. Shader setter arguments are guest handles, not decoded
program identities. No shader archive decoder success is inferred from reads.

Static source finding: current `Dev_CreateVertexShader`/`Dev_CreatePixelShader`
allocate shader objects without copying bytecode; both shader setters return
success without populating pipeline programs. Raster draws reject empty VS/PS.
This is a new-implementation limitation, not recovered game behavior or proof
that the game deliberately chose fixed-function rendering.

## Validation

Root: 93 Python tests passed, one desktop test skipped; CMake build and all four
CTests passed. Logs: `reports/diagnosis-{unit,root-build,root-ctest}.log`.
Isolated native suites: SEH, CRT strictness, instruction helpers and headless
passed. DX ran 141205 checks with the same six cursor assertions failing, at
`dx_tests.cpp:2765,2770,2774,2781,2790,2797`; do not claim a green DX suite.
See `local/diagnosis-20261020/native-tests.log`.

## Integration

The single diagnostic strict run ended after 265.65 seconds, exit 6 / logged
host signal 6, at a newly reached unmodelled instruction; it did not time out.
All 500 modules, input hashes, recorded diagnostic sources and imported archives
remain unchanged. Bounds were 360 seconds / 180000 frames, with a 430-second
outer process-group timeout. The previous scheduling stall was not reproduced;
this run ended before either deadline, so it cannot establish correct cap or
watchdog termination.

The latest pre-trap heartbeat counted 86,871 raster attempts, all rejected with
both VS and PS empty; zero completed raster calls and zero raster pixel writes.
It also counted 277 successful VS object creations, 524 successful PS object
creations, and 260,104 / 346,975 setter calls respectively. These are approximate
snapshots before the trap, not final totals or proof of shader program decoding.
CPU presents: 43,350; GPU presents: zero. Sampled backbuffer remained black.
The direct shader creation/binding limitation is now both a source finding and
an observed empty-pipeline condition in this diagnostic run. Do not call the
shader-less draws evidence that the game intended fixed-function rendering.

At that sample, 98,396 boot/headless ticks had matching normal exits and cap
checks, guard was clear, pinning was off, and 900,759 scheduler checkpoints were
counted. That falsifies a persistent guard/pin/tick stall up to the last sample
in this run. It does not explain the previous pinned-host run or prove timing
parity between the two hosts.

## Exact next static task

Trap VA `00b54d09` / RVA `00754d09` is pinned bytes `0f5ac1`, CVTPS2PD XMM0,XMM1.
The native backtrace includes `body_00b57e60` calling into the baseline, so the
containing-owner module is observed on the reached call chain. Do not infer
coverage of every recovered block.

Use the existing inventory entry for owner VA `00b547d0` / RVA `007547d0`, rather
than repeating global trap discovery. Its baseline header has 1,511 instructions,
last VA `00b574b7` / RVA `007574b7`, and alternate entries `00b547e8`, `00b54c8b`,
`00b55f8b`, `00b574b1`. It has six inventoried CVTPS2PD traps and was explicitly
rejected by the earlier flat-table planner for exceeding 8 KiB.

Its repaired listing contains guarded CMP EAX,6 / JA at `00b54eda` /
`00b54edd`, then switch `00b54ee3` reading seven dwords at `00b574bc`
(RVAs are VAs minus `00400000`). Independent bounded CFG recovery is still
required; do not assume the baseline header/listing is complete. Preserve all
baseline fragment boundaries and dispatch entries; reject foreign ownership or
escaping targets; compare decoded tables; batch all verified fixes before the
next strict probe. The explicit 16 KiB tooling budget is available, but is not
permission to skip the independent owner/CFG audit.

Separately, D3D9 shader token capture and direct binding are a concrete native
adapter implementation gap. Diagnose/implement with bounded synthetic shader
fixtures and the existing decoder/pipeline types; do not patch around strict
instruction traps or invent recovered shader behavior.

Latest full module/input list:
`local/diagnosis-20261020/probe-diagnostic/result.json` (500 dylibs, no new module).
Keep the host choice explicit: this diagnostic executable differs from the
pinned host. The next probe must preserve all 500 modules and use fresh paths.
Confidence is high for reached trap/byte identity and diagnostic counter
observations; complete next-owner CFG and whole-function parity are unverified.
No menu or racing has been established.

## Subsequent batch checkpoint

See `HANDOFF_TABLE_BATCH_R2_2026-10-20.md`: four verified owner modules batch
13 trap fixes; the restored combined run preserves 504 modules and observes
a native invalid-field store at VA `00426ce8`. Its `probe-restored/result.json`
is the latest full input/module list. A manual DeepSeek prompt is prepared.
