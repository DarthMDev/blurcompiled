# CVTPS2PD continuation checkpoint — October 18

**Newer checkpoint:** the 4.9 KiB owner is now integrated. Read
[the comparison-instruction handoff](HANDOFF_STARTUP_COMPARE_2026-10-18.md)
for the new CMPNLESS blocker at RVA `000e06bc`.

Read `AGENTS.md` and the earlier `HANDOFF_STARTUP_NATIVE_2026-10-18.md` for pinned
inputs and the existing corrected host. This checkpoint supersedes its exact
CVTPS2PD blocker. Preserve all earlier generations, runs and patch artifacts.

## Verified change

The existing `recomp_cvtps2pd` helper captures both low source floats before
writing either double, so aliasing is safe. Its register behavior was retained;
a shared bits-taking helper adds the m64 form without reading 128 bits. The
translator now lowers legacy CVTPS2PD register and memory forms. No feature
advertisement, generated instruction patch or failure bypass was added.

Synthetic native-versus-Unicorn tests: alias `XMM0,XMM0`, distinct `XMM3,XMM0`,
and unaligned m64; 40 random fixtures per form plus 17 directed pairs per form.
Both lanes exercise signed zero, subnormals, minimum normal, maximum finite,
infinities, signed quiet/signalling NaNs and payload quieting. Stores expose
all destination lanes and the register source; scratch, stack, integer registers
and defined flags are compared. Default masked MXCSR only; guest FP exception
status and DAZ remain outside the model. This does not establish whole-game parity.

The bounded module tool now accepts audited alternate entries, validates their
instruction boundaries, emits all wrappers and replaces the same existing
runtime hook slots. Existing hooks are refused. Each module has a unique name.
No baseline listings or generated host instructions were edited.

Three closed functions were regenerated through the emitter and compiled through
kit `tools/lazy_static.py`, reusing the staged build configuration:

| Primary VA / RVA | Existing alternate VAs | Instructions | Final module directory under `local/startup-native-r2/` |
| --- | --- | --- | --- |
| `00797e20` / `00397e20` | `00797ea9`, `00798121` | 139 | `cvtps2pd-final-797e20/` |
| `005b0c70` / `001b0c70` | `005b0e00`, `005b1000`, `005b106e` | 466 | `cvtps2pd-final-5b0c70/` |
| `004e29b0` / `000e29b0` | none | 154 | `cvtps2pd-final-4e29b0/` |

All rows refer to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Evidence: existing corrected listings, baseline generated ownership/wrappers,
strict closed-CFG translation reports, and sequential strict startup probes.
Confidence: high instruction/entry identity and observed advancement; complete
function behavior remains unverified. Source snapshots and hashes are preserved.

## Observed advance and next integration site

Final probe `local/startup-native-r2/probe-cvtps2pd-final/` reaches beyond the
previous CVTPS2PD failures, including render-target texture requests and later
archive resources. It aborts after 3.93 seconds at:

- VA `004e067d`, RVA `000e067d`, bytes `0f5ac8`: **CVTPS2PD XMM1,XMM0**.
- Same executable SHA-256 above; high confidence from pinned PE Capstone decode
  and the final log. The existing host still carries the old emitted trap here.
- Complete owner: VA `004e0620`, RVA `000e0620`, 763 instructions, 4,904 bytes.
  Existing alternate VAs `004e0800`, `004e0ce9`, `004e1666`, `004e17a8`
  (RVAs `000e0800`, `000e0ce9`, `000e1666`, `000e17a8`). Ownership evidence is
  the corrected listing and original generated `body_004e0620` wrappers.
- Next small step: permit a deliberate bounded generation of this complete
  owner (current tool limit is 4,096 bytes), retaining all four alternate entries.
  Check closed CFG, SEH and jump-table requirements before compiling. Do not
  invent an entry at `004e067d`. There is no demonstrated need for whole-image
  regeneration or the expensive native host rebuild yet.

No stream observer errors, missing dispatch reports or unimplemented imports
were reported in the final run. Its one 1024×768 PPM is entirely black. Movies
and the main menu remain unverified; host video decoding is still disabled.
The earlier Bink framebuffer integration gaps remain. Texture counts vary with
startup scheduling and are diagnostic requests, not rendering acceptance.
One intermediate 15-second run timed out during resource loading; the fresh
final run reached the explicit instruction abort above.

## Reproduce the final checkpoint

```sh
python3 tools/recomp_blur_probe.py \
  --out local/startup-native-r2/NEW_FRESH_PROBE \
  --binary local/startup-native-r2/build/recomp/pop_headless \
  --assets '/path/to/Blur' \
  --extra-code local/startup-native-r2/cvtps2pd-final-797e20/code.dylib \
  --extra-code local/startup-native-r2/cvtps2pd-final-5b0c70/code.dylib \
  --extra-code local/startup-native-r2/cvtps2pd-final-4e29b0/code.dylib \
  --extra-code local/startup-native-r2/stream-watch/watch.dylib \
  --seconds 30 --frames 1800
```

Original ARM64 host hash is unchanged:
`f287e9bcc2fe19b2cfddead09e4683196fec0e5e2f842f60324009efa80a1eb1`.
No host rebuild or whole-image regeneration occurred. The installation stayed
read-only. Each module directory contains generation/input hashes and `code.dylib`.

Validation: 75 root unittest tests; root CMake build; all four root CTests;
81 synthetic instruction tests; 237 translator/config/import/SEH/table tests.
Unicorn's sandboxed ARM cache query crashes at memory mapping; the unrestricted
synthetic run passes. A broader initial command also selected 13 hook tests that
require another game's absent generated tree in `kit/build`; those failed on
missing files, and are not reported as passing. No build was created for them.
The format helper ran on the changed runtime header; the focused patch reverse
check passes against the final kit sources.

Artifacts: `recomp/patches/startup-cvtps2pd-reviewed-slice.patch` applies after the
earlier combined runtime patch, rather than replacing it. Root bounded-tool
changes are ordinary source files. `reports/startup-cvtps2pd-2026-10-18.json`
records hashes, test counts, final run, and the next exact site. Relevant source
snapshots are under `local/startup-native-r2/cvtps2pd-source-snapshot/`.
No goal, automation, subagent, commit or PR was created.
