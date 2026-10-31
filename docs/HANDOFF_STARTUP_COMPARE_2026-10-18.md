# Startup comparison-instruction checkpoint — October 18

This supersedes the next integration site in `HANDOFF_CVTPS2PD_2026-10-18.md`.
Keep its first three bounded modules and the unchanged ARM64 host. Read AGENTS.md
and both earlier handoffs for pinned inputs and constraints. No whole-image
regeneration, native host rebuild, generated instruction edit or failure bypass
occurred. Reference installation remains read-only.

## New implementation and observed advance

The bounded generator keeps its default 4,096-byte limit and now accepts an
explicit `--max-bytes 8192`. Synthetic tests retain the extent, entry-boundary,
positive-size and upper-limit checks. The complete 4,904-byte function at VA
`004e0620` / RVA `000e0620` was emitted with its 763 instructions and all four
existing alternate VAs `004e0800`, `004e0ce9`, `004e1666`, `004e17a8` (RVAs
`000e0800`, `000e0ce9`, `000e1666`, `000e17a8`). Closed CFG checks pass; no SEH or
jump tables were found. The module compiles with kit `tools/lazy_static.py`.

Final module: `local/startup-native-r2/cvtps2pd-final-4e0620-r2/code.dylib`.
It passes the old CVTPS2PD at RVA `000e067d`, then aborts on the next genuine
unsupported instruction. Final probe: `local/startup-native-r2/probe-cvtps2pd-4e0620-final/`.

All recovered addresses here refer to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Evidence: unchanged corrected listing, original generated ownership/wrappers,
pinned PE Capstone decode, and strict probe log. Confidence is high for opcode,
entry identity and observed advancement; full function parity is unverified.

## Exact next blocker

- VA `004e06bc`, RVA `000e06bc`, bytes `f30fc2da06`:
  **CMPNLESS XMM3,XMM2**, immediate predicate 6 of scalar CMPSS.
- Same containing function above. Existing experimental helper
  `recomp_cmpnless` in kit `runtime/x86.h` has no lowering rule. Audit and test
  its mask, NaN/unordered behavior, preserved upper lanes and unchanged EFLAGS
  against synthetic Unicorn before adding the emitter path.
- Static later boundaries in this same function: CMPNLESS at RVAs `000e0724`,
  `000e0795`, `000e081a`, and CVTPD2PS at RVAs `000e086a`, `000e0888`.
  These later sites are listing/emitter evidence, not observed execution.
  `recomp_cvtpd2ps` also already exists and needs its own audit/tests.

The bounded report previously relied on `Translator.unmodelled`, which omits
intentional SSE runtime traps. That diagnostic gap is fixed: it now inventories
actual emitted `recomp_unmodelled` calls and their listing instructions, and
states that runtime abort boundaries remain. A synthetic regression covers this.
The final report correctly lists six retained traps; no trap was removed or
bypassed. The earlier `cvtps2pd-final-4e0620/translation.json` is preserved as
historical evidence and its empty unmodelled list is superseded by the r2 report.

## Reproduce

Use the prior checkpoint's probe command with a fresh output and add:

```sh
--extra-code local/startup-native-r2/cvtps2pd-final-4e0620-r2/code.dylib
```

Keep all three previous final modules and the read-only stream observer. The
host hash remains `f287e9bcc2fe19b2cfddead09e4683196fec0e5e2f842f60324009efa80a1eb1`.
Final probes preserve input hashes. No missing dispatch, unimplemented import or
negative stream observer event was reported. The captured framebuffer remains
black; integrated movies and main menu are unverified. Video is still disabled.

Validation this step: 77 root unittest tests, root CMake build, all four root
CTests, and compilation of the bounded native module. No translator/helper code
changed, so the prior 81 instruction-oracle tests were not rerun or relabelled as
new validation. Hashes, logs and source snapshots are recorded in
`reports/startup-cvtps2pd-4e0620-2026-10-18.json`.
