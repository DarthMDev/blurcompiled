# MSVCR90 feasibility result and Astra checkpoint

2026-10-08. The first bounded experiment reached its strict-translation stop gate.
The candidate DLL was staged and analyzed, but no translated DLL host was built
and no DLL code was executed. Initialization and compatibility remain untested.

## Observed result

An isolated synthetic i386 executable compiled and linked without a CRT. It
imports six Kernel32 APIs and contains assertions for DLL export resolution,
DLL-owned allocation/reallocation/free and a data-value observation. All PE,
assembly, database, generated output and reports live under ignored directories.

The exact candidate was copied read-only from CrossOver, with source and staged
hashes checked before/after preparation:

- MSVCR90 SHA-256:
  `f58ddc0f08d75e2d750ad66d54be059cd90e428704b5b4351a4efc1550df13ba`.
- CrossOver version/build reported by its installed plist: `26.3` /
  `26.3.0.39832`.
- Synthetic executable SHA-256:
  `f8d0ce96575aa02d8c0afe90fb3cd92165305322d35c818e75139793f55e9562`.
- Unchanged kit:
  `eb32cc42623e0e9e868b9d27423a57d5a5cba56b`.
- Unchanged Blur executable copy:
  `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.

The preflight confirmed all 204 Blur MSVCR90 names among 1,452 exports. This is
name coverage, with code/data classification recorded, not ABI validation.
Conservative literal source scanning found 86 Kernel32 handler candidates,
seven signature-only entries, and 38 names not found; all 12 NTDLL names were
not found. The source scan is not an authoritative live registry/behavior audit.

Ghidra 12.1.4 produced 2,908 assembly listings for MSVCR90 and three for the
optimized synthetic executable. These are automatic function boundaries, not
recovered source. The actual DLL entry at RVA `0xd140` has a listing. Exporter
and listing hashes are in the export report. No decompiled pseudocode was needed.

The first whole-DLL translation ran strictly, exited 1 after 5.27 seconds, and
did not time out. It reported six failed functions caused by two FISTTP and four
UD2 instructions. Removing those failed bodies also produced eight unresolved
direct-call sites. The translator raised its dispatch invariant error before
writing the usual upstream statistics JSON, so complete discovery/recovery/table
statistics are **unavailable for this attempt**. The wrapper preserved the exact
command and diagnostics even without the upstream JSON.

No permissive translation flags were used. The synthetic executable's translation
and the host build were deliberately not started after the DLL gate failed.

## Instruction evidence

Every address below belongs only to the candidate DLL hash above. Evidence:
strict translation log, hash-pinned assembly listing, PE export table and an
independent Capstone decode of the candidate's bytes. Confidence is high for the
instruction encoding and exported labels; automatic function boundaries and
runtime path reachability remain unverified.

| Function RVA | Instruction RVA | Observed instruction | Export label at function start |
| --- | --- | --- | --- |
| `0xfb70` | `0xfbe3` | FISTTP, 64-bit integer memory operand | None; no source meaning assigned |
| `0x17d00` | `0x17df4` | UD2 | `_fcloseall` |
| `0x29180` | `0x2926c` | UD2 | `_rmtmp` |
| `0x4bdf0` | `0x4bf36` | UD2 | None; no source meaning assigned |
| `0x4c330` | `0x4c493` | UD2 | None; no source meaning assigned |
| `0x58900` | `0x58bb9` | FISTTP, 64-bit integer memory operand | `pow` |

The two FISTTP sites sit inside arithmetic sequences whose following instructions
consume the stored result; `pow` is also among Blur's static imports. The four
UD2 sites follow loads from small addresses in the invalid/null region. Their
placement on error paths is a static inference from surrounding branches and
error-state stores. This is evidence against treating these sites as harmless
listing padding. It does not establish whether the allocation smoke test would
reach them.

An independent listing count found four UD2, two FISTTP, one FNSTENV and one
FLDENV site. The pinned translator/runtime already contains environment
save/restore emitters and helpers. Consequently the older Wine sample's warning
about its x87 environment pair is not sufficient evidence that this candidate's
pair is unsupported. Strict translation has not yet progressed far enough to
provide a complete acceptance result or establish whether further table gaps
exist. Do not copy the sample's table repair addresses.

Detailed bytes and instruction contexts are retained only in ignored
`reports/recomp-crt-instruction-blockers.json` and the local assembly listings.

## Decision needed in Astra

The candidate has failed the **unmodified pinned kit's strict translation gate**.
This rejects immediate DLL execution on that path; it does not prove that the
single-DLL ownership architecture is infeasible.

The next planning decision is whether to authorize a small, separately recorded
kit patch for actual FISTTP and UD2 semantics with synthetic differential tests,
or to compare a compatible Microsoft VC90 candidate before changing the kit.
Returning to coherent native CRT adapters remains the fallback.

For Astra, assess:

1. Whether FISTTP's conversion, stack-pop, exceptional inputs and guest FP state
   can be implemented within the bounded instruction-test budget. A shortcut
   through the current FISTP helper is not automatically equivalent.
2. Whether UD2 should enter a modeled guest invalid-instruction path, and what
   exception/termination behavior that requires. A zero return, skipped body,
   padding annotation or generic unmodelled trap cannot establish its semantics.
   The preceding null-region accesses also require armed guest null checks.
3. Whether to spend the remaining strict feasibility budget on those two
   instruction families before investigating OS dependencies, or inspect another
   CRT's required instruction/service set first. A successful patch would still
   leave DLL initialization, heap lifetime, FILE/locale, OS imports and original
   VC90 ABI validation outstanding.
4. Whether this result changes the proposed single-owner boundary. No runtime
   evidence yet supports combining DLL CRT state with the existing adapters.

No listing-repair round or OS-service fix round was consumed. One DLL was staged,
one export pass and one strict translation attempt were performed. Any patched
experiment must use a separate kit checkout/build, preserve this pin's failure
report, record patch hashes, and repeat the strict gate. The current plan and
Blur adapter configuration remain available unchanged.

## Reproduction and artifacts

New handwritten source:

- `tools/recomp_crt_experiment.py`: bounded PE preflight, safe local staging,
  fixture compilation, input/baseline fingerprints, assembly export and strict
  translation wrapper. It has no permissive-mode or execution action.
- `tools/ghidra/ExportPinnedRecompListings.java`: exact-hash assembly export.
- `recomp/crt-probe/`: synthetic probe C source, six-import definition and
  configuration template. Rendered configuration stays local.
- `tests/test_recomp_crt.py`: synthetic PE bounds, export-kind, ordinal,
  directory, entry, static TLS and output-location tests.

From a fresh experiment output directory, the commands used were:

```sh
local/recomp-venv/bin/python tools/recomp_crt_experiment.py prepare \
  --out reports/recomp-crt-prepare.json
local/recomp-venv/bin/python tools/recomp_crt_experiment.py export \
  --out reports/recomp-crt-export.json
local/recomp-venv/bin/python tools/recomp_crt_experiment.py translate \
  --out reports/recomp-crt-strict-round0.json
```

Preparation defaults to the installed Homebrew LLVM clang/dlltool and the Rust
toolchain's LLD linker, all discovered locally; exact commands are recorded in
the preparation report. Tool paths can be supplied explicitly. Existing output
directories/reports are refused to preserve prior evidence. No downloads were
needed. These commands do not run the Windows fixture or the DLL.

Ignored evidence:

- `local/recomp-crt-probe/manifest.json`: baseline and source/input hashes.
- `reports/recomp-crt-prepare.json` and compiler/linker logs.
- `reports/recomp-crt-export.json` and Ghidra logs.
- `reports/recomp-crt-strict-round0.json` and `-msvcr90.log`.
- `reports/recomp-crt-instruction-blockers.json`.

Validation completed after the code additions: 51 Python tests passed (12 new
synthetic CRT tests), normal CMake build passed, opt-in CMake build passed, and
startup-adapter/native-assets/bootstrap-help CTest checks passed. These checks
verify the tooling and preserve existing adapters; none validates DLL execution.
The kit checkout is still clean. Baseline fingerprints confirm the existing
Blur configuration/adapters, executable copy and latest build/boot evidence were
unchanged. The installed game was never written or used as a runtime root.
