# Batched finite-table recovery, second pass — October 20

Read `AGENTS.md` and `HANDOFF_DIAGNOSTICS_2026-10-20.md` first.
Installed game and assets remain reference inputs only. Menu/racing unverified.

Executable SHA-256:
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Unchanged diagnostic host SHA-256:
`d65e2847e3c15468bb5aec92824ba2a22a2472a98059aca890a63d86671718e4`.
Original pinned host, also unchanged:
`597b115f468a5cf18522c8d02e82f15deed82e7e8f55865337e0763da0987e18`.
No host rebuild, instruction-semantics change, shader fix, or scheduler change
was needed for this batch.

## Accepted batch

| Owner VA | RVA | Bounded bytes | Instructions | Preserved dispatch entries | Inventoried trap sites |
| --- | --- | ---: | ---: | ---: | ---: |
| 0047c470 | 0007c470 | 3076 | 921 | 15 | 1 |
| 00b547d0 | 007547d0 | 11498 | 2385 | 12 | 6 |
| 00bba4a0 | 007ba4a0 | 4634 | 1048 | 7 | 3 |
| 00c58130 | 00858130 | 6508 | 1415 | 14 | 3 |

Total: 5,769 instructions, 48 preserved entries, 13 baseline deliberate trap
sites. All four complete owner modules compile with strict branch/fallthrough
closure, exact finite-table decoder agreement, zero retained traps and zero
unresolved branches. Confidence is high for static byte and bounded CFG identity;
whole-function behavioral parity remains unverified.

The reached CVTPS2PD trap at VA `00b54d09` / RVA `00754d09` has pinned bytes
`0f5ac1`. All five other inventoried traps in its owner are batched with it.
The owner's seven-case flat table is guarded by CMP EAX,6 / JA and read at
VA `00b54ee3` from VA `00b574bc` (RVAs `00754ee3`, `007574bc`). Its exclusive
end is VA `00b574ba`, including the complete final RET. Independent Ghidra
recovery preserves every baseline fragment boundary and all 12 entries; the
incomplete 1,511-instruction primary listing was not treated as a complete owner.

The three other owners use five guarded byte-remapped tables. Exact index and
pointer bases, case counts, targets, guards, instruction bytes and executable
identity are in `plan.json` and per-owner `byte-check.json`. Their acceptance
does not assume byte-remapped targets are a flat dword list. Larger, inferred,
unbounded, or insufficiently proven owners remain unmodified review cases.

## Tooling and independent gates

`tools/recomp_finite_tables.py` reads explicitly bounded flat or byte-remapped
tables. Synthetic tests check case order, duplicates, sparse indices, unreadable
indices/storage, invalid counts and escaping/exclusive-end targets. It infers
neither bounds nor CFG from bytes.

`tools/ghidra/ExportAuditedTableBatch.java` takes explicit per-owner 8/16 KiB
budgets and finite table shapes, independently reads the tables in the pinned
program, and walks the bounded CFG in a read-only disposable project. It follows
primary/declared alternate entries, direct branches and planned table targets;
calls retain runtime dispatch. Foreign function owners, invalid instructions,
escaping CFGs, unaudited indirect jumps, unreached table sites and budget overflow
are rejected. Output must be fresh. The earlier exporter was not changed.

The builder independently checks PE instruction bytes/sizes, overlaps, the union
of all baseline fragment boundaries, every dispatch entry, and every target
boundary. Strict emission separately proves fallthrough/branch closure and
finite-table agreement. No original listing or generated baseline C was patched.
All recovered symbols have executable SHA, VA/RVA, evidence and confidence in
`load/load.json` and the per-owner checks. Runtime strictness remains enabled.

## Evidence and reproduction

`local/table-batch-20261020-r2/` contains:

- `plan.py`, `plan.json`, `plan.tsv`, `plan-summary.json`: candidate bounds,
  tables, baseline unions and excluded review cases.
- `preserved-inputs.json`: full prior diagnostic-probe mapping, all 500 dylibs.
- `export.py`, `ghidra-command.json`, `ghidra.log`, `exports/`: independent
  listings, raw instruction bytes, byte checks, evidence and confidence.
- `build.py`, `build-console.log`, `build-*.log`, `module-*/`, `batch.json`:
  four compiled complete owner modules; no retained traps/unresolved branches.
- `validate.py`, `load/load.json`: all 500 prior modules preserved, prior reserved
  entries and loaded entries checked, zero dispatch conflicts.
- `verify-restored.py`, `restored-verification.json`: post-reconnect integrity.
- `run-restored.py`, `console-restored.log`, `probe-restored/`: fresh integration.

Use `local/recomp-msvc90-venv/bin/python`. Require fresh evidence paths for
repeated builds/exports/probes. This uses the same diagnostic host as the last
reached-trap run; it does not establish parity with the older pinned host.

Validation: 96 Python tests passed, one desktop test skipped; root CMake build
and all four CTests passed. Logs: `reports/table-batch-r2-{unit,build,ctest}.log`.
No native runtime sources changed. The prior native DX suite's six cursor
assertions remain an unresolved limitation, not a green-suite claim.

## Volume interruption

The initial combined probe lost `/Volumes/T7-Touch`. Its runner session ended
with exit 120; `probe-batch/result.json` is absent and the log contains NUL/binary
regions. Preserve those outputs as interrupted evidence; they are invalid for
integration acceptance. The internal-disk interruption note is also copied into
this local batch directory. Do not infer a game fault from the interrupted run.

After reconnection, 1,239 explicit integrity checks passed with zero mismatches:
all 521 previous probe inputs (including 500 modules and both hosts), 662 recorded
diagnostic source files, imported game/CRT archives, four new compiled modules,
generated sources, module inputs, plans and baseline evidence. No rediscovery or
recompilation was required. `restored-verification.json` records these checks.

## Integration

The restored strict run finished in 126.7 seconds, exit 5 with logged host signal
11. All runtime input hashes and diagnostic source hashes remain unchanged;
all 504 modules were preserved. There was no unsupported-instruction trap,
missing dispatch or unimplemented import reported. This does not prove each
replaced site executed. Both previous and current runs have timing-dependent
thread progress; do not claim the four new modules caused or resolved this fault.

The run used the unchanged diagnostic host, 360 seconds / 180000 frames, and a
430-second outer timeout. Frame files were sampled every 600 presents instead
of the default 10 to reduce disk writes (27 GiB available at restart). Strict
execution/counters stayed enabled; changed I/O overhead prevents deterministic
parity claims. The run ended before the cooperative cap or watchdog could be
tested. The earlier scheduling stall remains unreproduced here.

## New observed blocker: invalid object field store

Saved guest EIP `00b20fc2` is a return from indirect CALL at `00b20fc0`, not the
faulting instruction. Native symbolication localizes host PC `102c1cf78` to
`fn_00426c80`, generated `chunk_004.c:4254`. Pinned PE bytes at VA `00426ce8` /
RVA `00026ce8` are `f30f1182e0020000`, MOVSS [EDX+0x2e0],XMM0. The preceding
instruction at VA `00426ce4` / RVA `00026ce4` loads EDX from [EBX+0x1c].

The synchronized fault registers give EBX=ECX=`0898af70`, EDX=`fdcdedbd`.
The effective guest address `fdcdf09d`, plus inferred arena base `7000000000`,
exactly equals the host fault address `70fdcdf09d`. The native slide was
`02610000`, independently derived from nm and the signal-handler frame.
Confidence is high for the instruction/effective-address identity, not for the
origin of the bad field. Do not call it a known poison pattern without evidence.

Evidence is in `fault-audit/fault.json` (executable SHA, VA/RVA, pinned bytes,
confidence, symbolication commands and address arithmetic), `fault-excerpt.txt`,
and the strict host log. The baseline function is a recovered 37-instruction
fragment `00426c80..00426cf1`, not a repaired Ghidra function listing; ownership
and behavioral parity should not be assumed from that header alone.

Next work is static ownership/vtable/call preservation audit plus a bounded
batch of read-only observers for the field's initialization/lifetime, if needed.
No field was zeroed, pointer clamped, call skipped or strictness weakened.
The field may already be invalid on entry or change across callees; no snapshot
yet distinguishes these cases. Native backtrace includes caller `00b20f80`,
`00b21010`, `00422340`, `00422610`, the existing object watcher, and startup
callers. Avoid interpreting heuristic stack words as a full call chain.

The user uses DeepSeek manually in its free browser chat (DeepThink on, Search
off). A focused next prompt is saved at
`local/table-batch-20261020-r2/fault-audit/deepseek-prompt.md`. Await its response
before a speculative deep diagnosis; independent static fact checking is fine.

Rendering remains separately unimplemented at direct shader capture/binding:
latest pre-fault snapshot has 44,393 raster attempts rejected with both shaders
empty, zero raster completion/pixel writes. Host ticks remained balanced, guard
clear and clock unpinned through that sample. No menu/racing established.

Latest full input/module list for continuation:
`local/table-batch-20261020-r2/probe-restored/result.json` (504 dylibs).
Preserve all of it, then append only verified new modules. Do not use the
interrupted `probe-batch/` or revert to only the four new modules.

## Subsequent field-observer checkpoint

See `HANDOFF_FIELD_AUDIT_2026-10-20.md` for construction/registration before
observed initialization, host-service starvation, and a successful watchdog
report. Latest full input list: `local/field-audit-20261020/probe/result.json`
(505 libraries). Requested old frame-capture cleanup is recorded there.
