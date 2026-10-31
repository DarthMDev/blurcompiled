# Missing-listing batch recovery and movie audio — October 19

Read `STATIC_TRAP_BATCH.md` and `MOVIE_AUDIO_PROGRESS.md`. The latter documents
the completed standalone movie-with-sound feature and its launch commands. This
checkpoint does not claim integrated game movies, menu or gameplay.

## Independent listing recovery

A single disposable Ghidra pass processed 42 baseline missing-listing candidates
with already-supported instruction traps. Plan and results are preserved in
`local/trap-recovery-20261019/{plan.tsv,exports/results.tsv,ghidra.log}`. Shared
trap bytes and oversized extents were excluded from this pass's plan. The source
script `tools/ghidra/ExportTrapBatch.java` invokes the existing strict per-entry
exporter and records failures without inventing entries.

Ghidra exported 41 bodies and rejected one suffix entry owned by another
function. `tools/recomp_recover_batch.py` independently checks every instruction
boundary and the entire extent against pinned PE bytes before invoking the
bounded emitter. It compiled 25 entries; 16 exported bodies failed the exact
boundary/extent check and remain review items. No padding or generated
instruction edits were used to force them through.

All recovered symbols refer to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
The `build-r2` byte-check manifests contain VA/RVA, bytes, baseline ownership
headers, independent export identities, evidence and confidence. Behavioral
parity remains unverified. Compiles use kit `tools/lazy_static.py`.

## The reached suffix belongs to a complete recovered owner

Ghidra rejected `0073dde7` / RVA `0033dde7` because it belongs to `FUN_0073ddac`
/ RVA `0033ddac`. The latter exported as 31 instructions / 113 bytes, spanning
`0073ddac..0073de1c`. Capstone and Ghidra boundaries agree exactly. The baseline
had split the suffix into a separate dispatch entry, while its parent listing
does not appear in the original functions.tsv export.

The initial compile preserved header-declared alternates only. Revision 2 now
preserves **every existing baseline dispatch entry** lying on an independently
verified instruction boundary in the complete recovered body. Thus the module
for `0073ddac` also replaces the known suffix entry `0073dde7`. It does not
create a new entry at the trap instruction `0073ddea`.

Use `local/trap-recovery-20261019/load-r2/load.json`, not the initial `load/`
manifest. Its 25 modules have no conflicts with the 402 earlier batch modules,
the original active replacements, or the six reserved observer hooks. The
validation tool now accepts `--reserve-load` to verify and reserve a previous
load manifest. Per-symbol executable identity, VA/RVA, evidence and confidence
are recorded in the new load manifest.

## Probe evidence

The initial `probe-trap-recovery-20261019` loaded the 402+25 modules, reported
unchanged hashed inputs, reached BinkDoFrame and stopped at a null indirect call:

```
crt.guard.unknown_call target=00000000 EIP=004b19e7 ESP=029e3cf4
return=005c13bf
```

This was not a missing executable entry suitable for automatic translation.
It took 177.86 seconds and aborted by signal 6. No new SSE trap or unimplemented
import was reported. No parity is inferred, and the zero target must not be
replaced with a success stub. Its origin needs a separate runtime/data audit.

The revised strict probe `probe-trap-recovery-20261019-r2`, using the original
402-module load manifest and `load-r2` only, reproduced the null call at the same
observer EIP and caller return. Inputs were unchanged, BinkDoFrame was reached,
and no SSE trap or unimplemented import was reported. It aborted by signal 6
after 191.14 seconds. This is the current reproducible startup gate.
Both probes use the same rebuilt host SHA-256
`9746b35544f3cae463e6d2410a1a10d6e428e8fe23eb83cc275e45231adf1e01`,
read-only assets, and no diagnostic atomic override. Logs and results remain
under `local/startup-native-r2/compare-step/`.

Pinned-byte/listing inspection of caller VA `005c13bd` / RVA `001c13bd` shows
`CALL EAX` after reading a virtual slot at `+0x4c`; its return is `005c13bf` /
RVA `001c13bf`. Observer EIP `004b19e7` / RVA `000b19e7` is the instruction after
the earlier call at `004b19e2` / RVA `000b19e2`, not the null call instruction.
Confidence is high for these instruction identities; object provenance is
unverified. The earlier path requests a 32×32×32 resource, and the kit still
has a CreateVolumeTexture stub. A texture/vtable-adapter gap is a hypothesis,
not an established cause. Audit allocation, output pointers and the reached
virtual slot before implementing any fix.

## Viewer validation

88 root Python tests ran successfully (one explicit desktop test skipped), root
CMake build and all four CTests passed. The six-test movie suite also passed
separately with `BLUR_MOVIE_PLAYBACK_TEST=1` and desktop access. Its audio device
is dummy; the real Activision playback check separately used Metal and the Mac's
actual 48 kHz stereo playback device. Logs: `reports/movie-recovery-*` and
`reports/movie-audio-*`. See the audio progress document for sample counts,
checksums, timing estimates and selected-track limitations.

No installed game files were modified. Decompiler databases, extracted data,
compiled modules and logs remain in ignored local/report directories.
