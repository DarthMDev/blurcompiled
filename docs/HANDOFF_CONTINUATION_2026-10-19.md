# October 19 continuation checkpoint

Read `HANDOFF_BOOT_COMPLETE_2026-10-19.md` first. Main menu and racing remain
unverified. The reference installation was used read-only.

The prior agent also left `module-00573ff0` and `module-0057fc60` after the
written checkpoint. The explicit directory enumeration loads both.

## Observed advance

Fresh strict diagnostic `local/startup-native-r2/compare-step/probe-continuation-20261019-baseline/`
used the preserved `pop_headless-f287e9bc` and diagnostic atomic override. It
completed the boot archive, reached `BinkDoFrame`, then stopped at unknown call
VA `00570a30`, RVA `00170a30`, observer EIP `00560772`.

Recovered bounded entry `00570a30..00570afe`: 207 bytes, 85 instructions.
Disposable Ghidra export and independent PE Capstone instruction boundaries agree.
The module reports no unmodelled instructions, unresolved branches, alternate
entries, tables or tail targets. Evidence is in `compare-step/observed-00570a30/`
and `compare-step/module-00570a30/`. Confidence is high for byte boundaries and
observed reach; behavioral parity is unverified.

Fresh strict diagnostic `probe-continuation-20261019-entry/` loads that module,
passes the unknown call and reaches the movie routine again, then stops at
VA `0057ff2d`, RVA `0017ff2d`, `CVTPS2PD XMM1,XMM1`.
Both probes report unchanged hashed inputs. Placeholder Bink calls do not
establish video playback; headless rendering does not establish a visible menu.

## Prepared next module

The trap belongs to listed owner VA `0057fea0`, RVA `0017fea0`, 225 bytes.
`add_module.py` generated the complete owner from its existing corrected listing,
with baseline ownership evidence in generated `chunk_036.c`, using the previously
tested CVTPS2PD lowering. It compiled through kit `tools/lazy_static.py` into
`compare-step/module-0057fea0/code.dylib`. No generated instruction was edited.
Fresh strict probe `probe-continuation-20261019-cvt/` passed that site and
reached the next conversion trap at VA `0057f238`, RVA `0017f238`, in listed
owner `0057f1b0` (RVA `0017f1b0`). That complete owner was also regenerated
with the tested lowering, producing 85 instructions with no unsupported
instructions or unresolved branches. Its evidence and manifest are in
`compare-step/{observed,module}-0057f1b0/`.

Module SHA-256 values:

- `00570a30`: `372d2b28f88d86b998699b5666d2580d6d3322c6314d43fb6ea7d82309d670c5`
- `0057fea0`: `9e49597ece39d8e0d019f5a87daa41a9e9d883889ba6693d37ad988555dac364`

The next diagnostic, `probe-continuation-reviewed-1/`, emitted no new instruction
trap before ending, but its outer helper exceeded 340 seconds before writing
`result.json`. It is incomplete evidence: do not claim hashed-input validation,
successful startup, menu state or parity for this run. Its host log and helper
traceback are preserved; the bounded continuation helper stopped immediately.
Review the log and repeat with adequate manifest-finalization time after the
heavy rebuild finishes. Avoid concurrent long probes and the full rebuild.

All recovered entries above refer to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Module manifests contain the source hashes and structural translation evidence.

## Build and checks

Root Python tests: 80 passed. Root CMake build and all four CTests passed.
Logs: `reports/continuation-2026-10-19-{unit,build,ctest}.log`.

The interrupted host rebuild completed through the existing `build_host.py`
helper. Host SHA-256:
`9746b35544f3cae463e6d2410a1a10d6e428e8fe23eb83cc275e45231adf1e01`.
Native suites passed: SEH 318 checks / zero failures; strict CRT 11 subprocess
cases; intrinsics five nested return-value cases and foreign-context guard /
zero failures. Logs are under `reports/continuation-2026-10-19-*_tests.log`.
Future incremental builds should call the kit build helper directly without
reconfiguration, as the earlier handoff recommends.

Fresh strict `probe-continuation-20261019-rebuilt/` used this host and all bounded
modules, with **no atomic override**. All hashed inputs were unchanged; no
unimplemented import was reported. At 176.17 seconds it aborted on unknown call
VA `005706d0`, RVA `001706d0`, observer EIP `00560772`, ESP `03046ee0`.
This run did not establish movie reach or menu state.

Independent PE decode suggests a 205-byte body `005706d0..0057079c`, with
internal branches and calls to existing `00560750`; it ends at `RET 8` before
INT3 padding. This is only a candidate extent. The disposable Ghidra export
exceeded its 90-second timeout, so **no module was generated or accepted** for
this entry. Preserve `ghidra-observed-005706d0.log` and the recovery log before
retrying; inspect the disposable database state/locks first. Do not reuse a
different function's implementation merely because the bytes look similar.

Next: complete the independent boundary/CFG check for this reached entry, build
its bounded module, then repeat the fresh strict rebuilt-host probe. Use fresh
probe names and the explicit enumeration in `run_direct.py`. The menu and racing
remain future milestones; no DeepSeek consultation was required for these steps.
