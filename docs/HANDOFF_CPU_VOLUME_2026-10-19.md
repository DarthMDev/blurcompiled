# CPU volume startup slice — October 19

Continuation of `HANDOFF_RECOVERY_AUDIO_2026-10-19.md`. No main menu or racing
has been established. The adapter below is new host implementation, not recovered
game logic. Reference assets remain read-only.

## Confirmed null-object cause

The unchanged-host strict probe `local/volume-audit-20261019/probe/` loads the
same 467 prior modules plus the new read-only observer
`recomp/diagnostics/volume_texture_watch.c`. It aborts after 216.64 seconds with
unchanged hashed inputs. The creation wrapper is entered with dimensions
32×32×32, one level, game flags `0x40000`, format 21 and output wrapper
`07e1cef4`. `CreateVolumeTexture` reports its existing unimplemented stub.
The wrapper returns a true low byte while `wrapper+8` remains zero. The following
lock wrapper reads that zero resource and calls a zero virtual target, returning
to `005c13bf`. This confirms the resource creation/output gap for this run;
it does not establish any renderer or whole-function parity.

All symbol addresses below belong to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.

| VA | RVA | Evidence / confidence |
| --- | --- | --- |
| `005c2730` | `001c2730` | Creation wrapper calls device slot `+60`, stores output at wrapper `+8`; PE/listing boundaries and observed zero output. High confidence for accesses and observed reach. |
| `005c13a0` | `001c13a0` | Lock wrapper calls resource slot `+4c`; PE/listing boundaries and observed null resource. High confidence for accesses and observed reach. |
| `005c1460` | `001c1460` | Unlock wrapper calls resource slot `+50`; PE/listing boundaries. High confidence for instruction identity; runtime reach needs separate evidence. |
| `004b1960` | `000b1960` | Caller creates, locks, fills and unlocks the 32³ resource; PE/listing boundaries and observed creation/lock caller returns. Full behavior remains unverified. |

`local/volume-audit-20261019/byte-evidence.json` records listing hashes and every
instruction's pinned PE bytes. Both hooks preserve earlier hooks and results,
read bounded guest addresses, and leave guest memory unchanged.

## New bounded adapter

`recomp/patches/startup-cpu-volume-reviewed-slice.patch` applies to the current
`local/recomp-kit-static-binding` baseline. It adds the volume interface without
renumbering existing interface constants and implements one CPU-backed level
for formats 21/22/50, zero Direct3D usage, unshared resources and whole-level,
zero-flag locks. Dimensions are bounded to 256 on each axis before multiplication.
Other creation contracts fail explicitly and clear the output pointer.

The 22-slot volume vtable includes create/describe/type/level-count/lock/unlock
behavior. A 32³ four-byte resource has row pitch 128, slice pitch 4096 and 131072
bytes. Lock staging preserves bytes across unlock/relock and is released if the
object is destroyed while locked. Unsupported volume views and dirty-region
operations fail explicitly. GPU volume sampling, mip chains, partial boxes,
nonzero lock flags and additional formats are outside this slice.

Contracts: Microsoft's [LockBox reference](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvolumetexture9-lockbox),
[locked box layout](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dlocked-box),
and [volume description](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dvolume-desc).
This is API adaptation; it does not substitute a success return for missing data.

## Validation

The synthetic volume test uses actual COM trampolines and checks argument
cleanup, row/slice pitches, first/last byte retention, invalid levels, nested
locks, unmatched unlocks, unsupported mip counts, dimension limits, format 22
release while locked, and the observed 64×16×64 format-50 volume. All 58 checks pass:

```
cmake --build local/startup-native-r2/build/cmake/macos --target dx_tests -j 4
RECOMP_TEST_BLUR_VOLUME=1 local/startup-native-r2/build/recomp/dx_tests
```

The full DX suite's earlier run reported six cursor-surface assertions and
passed the added volume case. Those failures remain unresolved; the full suite
is not claimed green. The isolated volume test first needed a missing test CPU
reset corrected; its final standalone run passes.

Root Python tests: 88 run, one explicit desktop test skipped. Root CMake build
and all four CTests pass. Logs are `reports/volume-audit-*`.
Source/patch hashes are in `local/volume-audit-20261019/source-manifest.json`.

## First integration and the next initialization failure

`local/volume-audit-20261019/probe-fixed/` used the first adapter (formats 21/22),
strict execution and unchanged hashed inputs. After 217.67 seconds it stopped at
another null target, return VA `00858946` / RVA `00458946`, with observer EIP
`00422708` / RVA `00022708`. Before that it created and locked two 32³ format-21
volumes and retained 131072 bytes on each unlock. This establishes clearance of
the previous creation/lock gate, not main-menu reach.

The same run recorded one rejected creation: 64×16×64, one level, format 50
(L8), requested by VA `0041c9e0` / RVA `0001c9e0`. Its creation failure returns
before the later initialization call. The next null target is a game object
rather than a Direct3D interface: VA `00858930` / RVA `00458930` reads its vtable
from ECX, while the update at VA `00422610` / RVA `00022610` supplies object field
`+5c`. Creation of that field occurs in VA `00419e70` / RVA `00019e70`, reached
later in startup coordinator VA `00422c40` / RVA `00022c40` if the volume stage
succeeds. These addresses use the executable SHA above. Evidence is the pinned
PE decode, repaired listings and probe logs; confidence is high for individual
accesses and observed null reach, with full behavior unverified.

The read-only `recomp/diagnostics/startup_object_watch.c` observes construction,
initialization, teardown and update phases together. The first lifecycle run
`probe-object/` used the first adapter and reproduced the new null call after
343.08 seconds with unchanged hashed inputs. It recorded construction zeroing
`+5c`, then update with `+5c`/`+60` still zero and `+a8`/`+3b4`/`+3b8` nonzero.
The initializer hook was never entered. This supports the failed volume stage
preventing later initialization; no fake game object or skip was introduced.
The first observer's post-return `caller` field was the current stack top,
not a preserved caller; use its pre-call records for callers. The final observer
preserves the original caller across both records. The first source/module are
preserved as `object-watch-r1.c`/`object-watch-r1.dylib` in the audit directory.

`object-byte-evidence.json` records all eight audited owners and flags complete
boundary matches separately. The teardown and large luminance-volume body have
nonmatching complete extents; their entire bodies are not accepted as recovered
functions. Their relevant decoded instructions remain evidence, and neither is
regenerated or replaced by this work.

The final adapter adds format 50 using one byte per voxel: row pitch 64, slice
pitch 1024 and size 65536 for the observed resource. Synthetic tests verify the
last byte survives unlock/relock. `source-manifest-r1.json` preserves the first
adapter provenance; `source-manifest-l8.json` records the extended source.

## Luminance integration and a batch of missing constant getters

`probe-l8/` completed after 134.04 seconds, strict guards enabled, unchanged
hashed inputs, host SHA-256
`597b115f468a5cf18522c8d02e82f15deed82e7e8f55865337e0763da0987e18`.
The observed luminance upload retained 65536 bytes. The initializer at
`00419e70` was entered and returned with both `+5c` and `+60` nonzero. This
establishes clearance of the preceding skipped-initialization gate for this run.
It then requested showroom assets and stopped at missing executable target
VA `008c24a0` / RVA `004c24a0`. Observer EIP `008559cf` / RVA `004559cf` is context,
not established as the faulting instruction. Showroom requests alone do not
prove visible main-menu reach.

Pinned bytes at the target are `b8 06 80 84 64 c3`: `MOV EAX,0x64848006; RET`.
A bounded static inventory of executable addresses in `00850000..008fffff`
found 15 absent baseline entries with the same six-byte instruction shape,
16-byte alignment and preceding INT3 padding. Only `008c24a0` had observed reach.
The other 14 remain static candidates; their constants were never invented.

One Ghidra pass in a copy of the disposable project independently exported all
15 complete two-instruction bodies. Every listing boundary and byte extent
matches the pinned executable. All 15 compiled with the existing bounded
emitter, without instruction edits, retained traps, open branches or tables.
Load validation found 15 loadable modules, zero conflicts and zero review items,
reserving prior load manifests, all previous reserved entries and both new
observer families. This is structural acceptance, not behavioral parity.

The complete plan, independent exports, byte checks, generated sources, modules,
per-symbol SHA/VA/RVA/evidence/confidence, validation and results remain under
`local/volume-audit-20261019/`:

- `getters-inventory.json`, `getters-plan.tsv`, `getters-command.json` and
  `getters-ghidra.log`;
- `getters-export/observed-*/{evidence.txt,byte-check.json,*.asm}`;
- `getters-batch.json`, `getter-module-*/translation.json` and
  `getters-load/load.json`.

Use the final volume patch plus the two prior load manifests and
`getters-load/load.json`; do not replace the full module list with the 15 getters.
The original first-adapter source files and patch were reconstructed and
preserved under `source-r1/` and `cpu-volume-r1.patch`, with exact hash matches
to the previously captured first-adapter manifest.

## Getter-batch integration checkpoint

The strict `probe-getters/` loaded all 15 new modules with unchanged hashed inputs.
It passed the previously missing `008c24a0` call and stopped at an unmodelled SSE
instruction at VA `00b5ece0` / RVA `0075ece0` (same executable SHA above).
Evidence: strict host log; confidence high for observed trap address, semantics
and containing-owner completeness still require audit. No main menu or racing
is established. The run took 260.65 seconds.
