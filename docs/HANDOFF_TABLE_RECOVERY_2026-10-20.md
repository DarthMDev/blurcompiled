# Bounded table-owner recovery — October 20

## Resume here

Read `AGENTS.md`, then this file. Prior checkpoint:
`HANDOFF_CPU_VOLUME_2026-10-19.md`. Main menu and racing remain unverified.
The installed game remains reference input and was not modified.

Executable SHA-256 throughout:
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Host SHA-256 (unchanged; no full host rebuild in this slice):
`597b115f468a5cf18522c8d02e82f15deed82e7e8f55865337e0763da0987e18`.

## Reached gate and confirmed static cause

The preceding strict `local/volume-audit-20261019/probe-getters/` stopped at
CVTPS2PD VA `00b5ece0`, RVA `0075ece0`. Baseline owner VA `00b5dd30`,
RVA `0075dd30`, in `chunk_127.c`, had 675 instructions. Its prior regeneration
failed because the finite switch at VA `00b5ddd3`, RVA `0075ddd3`, targets
VA `00b5ddec`, RVA `0075ddec`, absent from the incomplete listing.

The pinned switch is guarded by `CMP EAX,0x27` / out-of-range JA and reads
40 dword targets at VA `00b5f2c8`, RVA `0075f2c8`. The target is inside the
bounded owner. The translator already implements CVTPS2PD; missing owner
blocks prevented the tested lowering from replacing the emitted startup trap.
These are observed trap reach and verified static instruction/table identities,
not whole-function behavioral parity.

## New recovery workflow

`tools/ghidra/ExportBoundedTableBatch.java` walks bounded instruction CFGs from
primary and previously declared alternate entries. It follows direct branches,
fallthroughs and only explicitly planned finite table targets, independently
reading those dwords from Ghidra's pinned program. Calls retain runtime dispatch;
the walker does not absorb callees. It rejects foreign function ownership,
escaping branches/targets, unknown indirect jumps, invalid instructions and
budget overflow. All work uses a copy of the disposable Ghidra project.

The local planner uses the existing translator's finite-table decoder over
baseline listings. Oversized owners, inferred/undecoded bounds and non-flat
(two-level) tables remain review items. It planned 15 flat-table owners.
Ghidra independently exported all 15. The reached owner has 1,407 recovered
instructions, including its previously missing switch arms.

`tools/recomp_table_recovery.py` supplies byte-span preservation gates, with
synthetic tests. The builder independently decodes every exported instruction
in the pinned PE and rejects overlaps, missing baseline boundaries, missing
alternate entries and table targets outside decoded boundaries. Gaps are not
permission to skip code: the existing strict emitter separately proves actual
fallthrough and branch closure. The builder preserves all existing baseline
dispatch entries lying on recovered instruction boundaries, including the four
previous suffix entries of the reached owner.

All 15 owners compile with the existing emitter and `--local-tables`, with no
retained traps or unresolved branches. Their tables must exactly match the
planner's finite tables after the complete recovered listing is re-decoded.
Validation reserves the previous 402-owner, 25-owner and 15-getter load manifests,
all earlier reserved entries, and both new observer families. It reports
15 loadable modules, zero conflicts, zero review items for the built subset.
The planner's separate rejected cases remain unmodified. No generated C,
original listing, instruction semantics or global strictness was patched.

## Evidence and reproduction

All local artifacts are under `local/table-recovery-20261020/`:

- `plan.json`, `plan.tsv`, `plan.py`: candidate bounds, tables, baseline hashes
  and excluded cases.
- `ghidra-command.json`, `ghidra.log`, `exports/results.tsv`: independent export.
- `exports/observed-*/{evidence.txt,byte-check.json,*.asm}`: executable identity,
  VA/RVA, pinned instruction bytes, evidence/confidence and table checks.
- `build.py`, `build-*.log`, `module-*/translation.json`, `batch.json`: complete
  owner emission, alternate entries and compiled module hashes.
- `load/load.json`, `validate.log`: conflict-free load manifest with per-entry
  executable SHA, VA/RVA, evidence and confidence.
- `run.py`, `console.log`, `probe-tables/`: one strict combined integration run.

Use the latest probe's full `inputs` module list when continuing. Do not replace
it with only the 15 table modules. `run.py` verifies all preceding probe inputs,
then appends the accepted table modules and records new hashes and bounds.
Runtime remains strict; assets are read-only. Probe bounds: 360 seconds / 180000
frames, with a 390-second process-group timeout. Use a fresh output directory
for every run. Existing scripts intentionally refuse reusing evidence paths.

Root validation after changes: 92 Python tests, one explicit desktop test
skipped; root CMake build and all four CTests passed. Logs:
`reports/table-recovery-{unit,root-build,ctest}.log`. The four new synthetic tests
cover overlaps, oversized instructions, invalid extents, lost baseline/dispatch
boundaries and invalid table targets. The prior full DX suite's six cursor
assertions remain an earlier unresolved limitation, not a green-suite claim.

## Integration result

The strict `probe-tables/` finished after 186.36 seconds with unchanged hashed
inputs, all 15 new modules loaded and the same host executable. It cleared the
previous CVTPS2PD gate at VA `00b5ece0` / RVA `0075ece0` and stopped at the next
CVTPS2PD (`XMM0,XMM5`) at VA `00b59f0e` / RVA `00759f0e`. Both volume formats
and the previously skipped initializer still completed. No main menu or racing
was established. Evidence: strict host log and pinned PE instruction decode;
confidence high for observed gate identity, not whole-function behavior.

## Exact next task

Audit the new reached trap's ownership before emitting anything. Baseline
`chunk_127.c` names recovered owner VA `00b58522` / RVA `00758522`, through
VA `00b5a753` / RVA `0075a753`, with suffix entries `00b586c6`, `00b587ea`,
`00b58d04`, `00b58e0f`, `00b5968d` (RVAs are VAs minus `00400000`). There is no
matching repaired listing or functions.tsv owner. The old trap inventory already
classified it as review. Its baseline span exceeds the current 8 KiB bound, and
its unaligned recovered start may be a containing owner's omitted block.
Do not pretend this baseline header establishes a complete original function,
raise bounds blindly, emit just the trap, or split away existing suffix entries.
Use pinned bytes, neighboring independent Ghidra ownership, control-flow/table
references and the baseline header to establish a bounded recovery plan first.
A strong containing-owner candidate is VA `00b57e60` / RVA `00757e60`:
its repaired listing has a normal prologue, a guarded two-level switch and an
exit at VA `00b5a764` / RVA `0075a764`, spanning the recovered fragments above.
The switch uses a byte-index table at VA `00b5a788` / RVA `0075a788`, followed
by dword targets at VA `00b5a768` / RVA `0075a768`. Baseline `chunk_127.c`
places recovered fragments immediately after that switch, including the reached
trap's fragment. Evidence: the pinned repaired listing and baseline ownership
headers. Confidence is high for these static accesses, but containing-owner
completeness remains a hypothesis requiring independent CFG recovery. This
case needs an audited two-level-table plan and exceeds the current 8 KiB slice;
it is deliberately excluded from the flat-table batch. Do not discard the
other baseline recovered fragment/alternate entries when recovering its parent.

This new gate is distinct from the 15 complete owners recovered above.

The next probe must preserve ALL modules from `probe-tables/result.json`, then
append any independently accepted new modules. Avoid repeating discovery of
already known trap sites. Do not infer menu reach from frame counts or showroom
requests. The code changes in this slice were analysis/export tooling and
synthetic checks; runtime instruction semantics and host binary were unchanged.

## Subsequent checkpoint

Containing-owner recovery and the next combined strict probe are recorded in
`HANDOFF_CONTAINING_OWNER_2026-10-20.md`. Its `probe-owner/result.json` supersedes
`probe-tables/result.json` as the full latest module/input list (500 dylibs).
