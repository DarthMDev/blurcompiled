# Further startup slices — October 19

Continuation of `HANDOFF_CONTINUATION_2026-10-19.md`. No menu or gameplay
acceptance is claimed. Read-only reference installation; no baseline generated
instruction edits or failure bypasses. Rebuilt host SHA-256 remains
`9746b35544f3cae463e6d2410a1a10d6e428e8fe23eb83cc275e45231adf1e01`.

All entries below refer to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.

| VA / RVA | Bytes | Instructions | Module SHA-256 |
| --- | --- | --- | --- |
| `005706d0` / `001706d0` | 205 | 83 | `d9da4a13620b4e9d670602792964703cc2bba7bcc09c8875b9faf59337a2e80f` |
| `0057ad50` / `0017ad50` | 724 | 249 | `021b15e7bdde0e6bb0dec83cb1fb3fc71666761e763dbb9f9f2fca29b77929af` |
| `00561980` / `00161980` | 1249 | 324 | `44426621bb4288d4097ee7ab36c0955ae9a6d222c9bee5f0fe0afd0a49398f1c` |
| `00560f60` / `00160f60` | 189 | 50 | `f7d1734311db6dd57733feca5272d7eeec9c1b631eb4da786595290ca48d3200` |

The two missing indirect entries were recovered through disposable Ghidra
exports and independent PE Capstone boundary checks. Confidence is high for
entry/byte identity and observed reach; full behavioral parity remains unverified.
Evidence and modules are in ignored `local/startup-native-r2/compare-step/`
under `observed-<VA>` and `module-<VA>`. Each translation reports no unsupported
instructions, unresolved branches, tables or SEH.

`005706d0` has unreachable padding at `0057075d`: bytes `8d4900`, LEA ECX,[ECX].
The preceding unconditional jump at `0057075b` goes to `00570760`, and no
internal branch targets that padding. Ghidra's 83-instruction reachable body
matches the independent byte decode when this explicitly recorded padding is
excluded. See its `byte-check.json`.

Recovery timeout resolution: this volume misreports some scandir directory
entries as files. A normal shutil.copytree therefore failed on actual directory
entries. Explicit os.listdir plus stat recursion copied the 316 MB disposable
project to `/private/tmp/blur-observed-20261019-retry`, without copying project
locks. Ghidra successfully exported both entries there. The original disposable
project, its logs, and its lock files were preserved. Retry helper:
`compare-step/recover_observed_retry.py` (240-second export bound).

The two listed owners were regenerated with the already-tested CVTPS2PD
lowering through `add_module.py` and compiled through kit `tools/lazy_static.py`.
Owner `00561980` preserves alternate entry `00561e06` (RVA `00161e06`).
Ownership evidence is in each observed directory. These are structural
translations; no whole-function behavioral parity is asserted.

## Strict probe chain

All runs use the rebuilt host, explicit module enumeration, and no diagnostic
atomic override. Fresh output directories are under `compare-step/`.

- `probe-next-005706d0`: passes the previously missing entry, reaches
  `BinkDoFrame` at caller `006846a5`, then stops at unknown target `0057ad50`
  (RVA `0017ad50`), observer EIP `00563185`. Inputs unchanged; 154.45 seconds.
- `probe-next-0057ad50`: passes that target, reaches the movie routine, then
  stops at CVTPS2PD at `00561af4` (RVA `00161af4`), owner `00561980`.
  Inputs unchanged; 124.44 seconds.
- `probe-next-00561980`: passes that site, reaches the movie routine, then
  stops at CVTPS2PD at `00560fd1` (RVA `00160fd1`), owner `00560f60`.
- `probe-next-00560f60`: reached the 36,000-frame cap in 143.87 seconds,
  with unchanged hashed inputs, no strict instruction traps, missing dispatch
  targets or unimplemented imports. Movie routine reached once. Uniform black
  frames; host exit 4, guest exit 0 after cap-driven WM_CLOSE. Still reading
  root.pak at the cap, so no menu state inferred.
- `probe-next-extended`: pending. A fresh six-minute diagnostic permits up to
  180,000 frames because the 36,000-frame cap demonstrably cut off ongoing
  archive work. `run_extended.py` changes only that host diagnostic frame limit;
  strict execution, hash manifests, read-only assets and module loading remain.

Movie-routine reach is now reproduced on the rebuilt runtime without the
atomic override. It still uses the no-decoder Bink placeholder; it does not
establish visible playback. CPU rendering still cannot establish a visible SM3
menu. The interlocked imports are built into this host; no new runtime shim was
needed in this continuation.

## Validation

80 root Python tests passed, root CMake build passed, all four CTests passed.
Logs: `reports/continuation-next-{unit,build,ctest}.log`. No new instruction
lowering or runtime service implementation was introduced. Per-module
translation reports and strict probe manifests are the bounded integration checks.
