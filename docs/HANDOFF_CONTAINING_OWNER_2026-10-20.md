# Containing-owner and two-level switch audit — October 20

Read `AGENTS.md` and `HANDOFF_TABLE_RECOVERY_2026-10-20.md` first.
The reference executable and installed assets remain unchanged. Main menu and
racing remain unverified.

Executable SHA-256:
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Host SHA-256:
`597b115f468a5cf18522c8d02e82f15deed82e7e8f55865337e0763da0987e18`.

## Static recovery accepted

The candidate at VA `00b57e60` / RVA `00757e60` has independently recovered
bounded CFG identity through the RET at VA `00b5a764` / RVA `0075a764` (exclusive
end `00b5a767`). This is 10,503 bytes, not a new arbitrary baseline fragment.
Ghidra's original function owns the prologue and default exit; all other reached
instructions were unowned. No foreign function owner intersects the CFG.
Starting at the primary entry alone reaches all 2,117 unique baseline instruction
boundaries, including the previous `recovered_00b58522` fragment and trap
`00b59f0e` / RVA `00759f0e`. All 18 existing dispatch entries are preserved.

The outer switch at VA `00b57e8c` / RVA `00757e8c` is guarded by CMP ECX,31 / JA.
Its 32-byte index table at VA `00b5a788` / RVA `0075a788` indexes eight dword
slots at VA `00b5a768` / RVA `0075a768`. The nested switch at VA `00b58732` /
RVA `00758732` is guarded by CMP EAX,3 / JA and reads four dwords at VA
`00b5a7a8` / RVA `0075a7a8`. Both target sets stay inside the recovered owner.
Ghidra independently reads both tables. The builder separately checks pinned
instruction bytes, guard bytes, all baseline boundaries, all dispatch entries,
table target boundaries, and exact agreement with the translator's finite-table
decoder. Strict emission proves branch and fallthrough closure.

This batches six baseline deliberate trap sites in four recovered fragments:
`00b58e78`, `00b59f0e`, `00b59f23`, `00b5874d`, `00b58781`, `00b587a1` (RVAs
are VAs minus `00400000`). The complete replacement retains zero traps and
zero unresolved branches. Confidence is high for byte and bounded static CFG
identity; whole-function behavioral parity remains unverified.

Only after this independent recovery was the explicit 16 KiB tooling budget
added. Defaults remain 4 KiB for emission and 8 KiB for span validation. Larger
bounds do not relax ownership, baseline preservation, table, closure, or trap
gates. No instruction semantics or generated baseline C was patched.

## Local evidence and reproduction

`local/containing-owner-20261020/` contains:

- `audit.py`, `audit-plan.json`: pinned PE guard/table decode and union of every
  baseline fragment's instruction boundaries.
- `preserved-inputs.json`: complete prior strict probe input mapping, verified
  before tooling edits; 499 dylibs.
- `export.py`, `ghidra-command.json`, `ghidra-v2.log`, `export-v2/`: Ghidra
  read-only project recovery, ownership observations, listings and bytes.
  The first `ghidra.log` records the correctly rejected unaudited nested switch;
  it emitted no accepted listing. Ghidra process exit alone is not success.
- `build.py`, `build.log`, `export-v2/byte-check.json`, `module-00b57e60/`,
  `batch.json`: independent verification and compilation, including per-symbol
  executable SHA, VA/RVA, evidence and confidence.
- `validate.py`, `load/load.json`: all 499 previous modules retained; prior
  reserved entries and loaded module entries checked; zero dispatch conflicts.
  Intentional changes to the two analysis tooling files are recorded separately
  from the preserved runtime hashes.
- `run.py`, `console.log`, `probe-owner/`: one strict combined integration run,
  360 seconds / 180000 frames, with 390-second process-group timeout.

Use `local/recomp-msvc90-venv/bin/python` for these local scripts (pefile and
capstone are available there). `local/recomp-venv/bin/python` is a stale symlink.
Each export/build/probe requires fresh evidence output paths.

Validation: 93 Python tests passed, one desktop test skipped; root CMake build
and all four CTests passed. Logs are `reports/containing-owner-{unit,root-build,ctest}.log`.
The earlier DX cursor assertions remain a separate unresolved limitation.

## Integration

The single combined strict run ended by the runner's 390.04-second process-group
kill (exit -9), with all hashed inputs unchanged. All 499 earlier dylibs plus
the new owner module were preserved and loaded (500 total). Host logs confirm
all 18 new replacement registrations. No unmodelled trap, missing dispatch, or
unimplemented import was reported. Black sampled buffers and host frame reports
continued through frame 28,800; the final log shows guest scheduling/yields.
The guest 360-second bound did not finish the process before the external bound.
No normal guest exit, menu, racing, or whole-function behavioral parity was
established. The previous trap did not recur; there is no per-instruction trace
proving every replaced site executed. Timeout prevented normal final profile /
discovery output; these files are absent, not evidence of zero activity.

Next work is diagnosis of black buffers and guest scheduling/time-bound behavior,
not another known-trap discovery sweep. The user was prompted for DeepSeek before
this deeper diagnosis; no external reasoning service was invoked in this slice.
Do not alter runtime scheduling or rendering based on the timeout alone.

Preserve ALL inputs/modules from
`local/containing-owner-20261020/probe-owner/result.json` on continuation, then
append only independently accepted modules. Never replace that list with only
this owner module. Fresh evidence paths are required for subsequent probes.

## Subsequent diagnostics

See `HANDOFF_DIAGNOSTICS_2026-10-20.md` for the separate instrumented-host run,
confirmed empty shader pipelines, and the next trap at `00b54d09`. Its
`probe-diagnostic/result.json` is the latest full input/module list (500 dylibs).
