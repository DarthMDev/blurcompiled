# Native movie slice and startup decoder repair — 2026-10-18

**October 19 update:** the standalone viewer now has synchronized selected-track
audio playback. See [the audio checkpoint](MOVIE_AUDIO_PROGRESS.md). The silent
viewer description below is the historical October 18 checkpoint; integrated
game startup remains a separate gate.

**Observed:** the real `BizarreSplash.bik` plays through a new ARM64 FFmpeg/SDL3
resource viewer using Metal. Ninety 1280×720 frames were decoded and presented;
84 have distinct packed-YUV CRCs. The last Metal framebuffer was saved and
visually inspected. The separate `atvi.bik` probe decoded 120 frames with 120
distinct CRCs. This viewer is silent and does not execute the game's startup
state machine. Integrated movie playback and the main menu remain unverified.

## Run the native resource slice

Uses the already-installed SDL3, Zlib and FFmpeg development libraries; no
dependency download is performed. FFmpeg is optional for the ordinary host.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew -DBLUR_VIDEO_SLICE=ON
cmake --build build -j 4
./build/blur-movie --assets '/path/to/Blur' \
  --movie BizarreSplash.bik --frames 180
./build/blur-movie --assets '/path/to/Blur' \
  --movie atvi.bik --frames 120 --decode-only
```

Escape closes the viewer. With no frame cap, it stops at EOF. `--snapshot PATH`
saves the final decoded frame from the renderer as BMP; the destination must be
outside the reference installation. The decoder accepts bounded YUV420P frames,
checks errors and drains at EOF. Audio, alpha formats, packaging and the game's
Bink ABI are separate work. A resource-viewer frame is not a game/menu frame.

The new implementation is `src/video/movie_main.cpp`. Synthetic FFV1 fixtures
check EOF, frame limits, packed-plane CRC parity with the FFmpeg CLI, malformed
input and refusal to overwrite reference data. The CLI uses the same decoder
library, so that comparison checks wrapper/plane handling, not an independent
Bink codec implementation.

## Recovered startup failure

All Blur symbols below are specific to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
The exact CRT remains SHA-256
`ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185`.

| Address / RVA | Evidence and conclusion | Confidence |
| --- | --- | --- |
| `00b8dbb0` / `0078dbb0` | Actual PE bytes, fresh Ghidra export, Capstone decode and corrected emitted code; caller passes its state through ESI | High for code/ABI observations; broader behavior unverified |
| `00b8dc4a`, `00b8dc5c` / `0078dc4a`, `0078dc5c` | Scalar SSE stores were recovered as operand-free string MOVSD | High; directly reproduced |
| `00ae2ba0` / `006e2ba0` | State argument and stack-top accesses in pinned assembly; observer caught caller `00b8dc67` passing `L+8` | High for reached bad argument |
| `00ae9b00` / `006e9b00` | Protected-call entry used to observe the state without repairing it | High for argument access |
| `00ae5e00` / `006e5e00` | Handwritten string-table override later frees the same state; native abort backtrace records the free chain | High for observed free; override is not parity-validated |
| `00af0dc0` / `006f0dc0` | Later constant-table writes overwrite the reused state allocation | High for reached writes; not the initiating defect |

The recovery decoder dropped operands whenever a mnemonic appeared in the
string-operation set. Both scalar SSE and implicit string-copy instructions are
called MOVSD. The resulting string copies advance ESI twice by four bytes. The
following numeric Lua push increments the global-state pointer instead of the
stack-top pointer. Previously added layout-repair/string/GC overrides concealed
this corruption and eventually freed the live state as a string-hash table.

The decoder now retains XMM operands. Five synthetic decoder cases cover scalar
load/store/register moves and plain/REP string moves. A decoder-to-native
regression compares scalar moves against x86 Unicorn, including ESI/EDI and
memory. The full 75-case instruction suite passes with the qualified oracle
outside the desktop sandbox; the initial sandbox oracle attempt trapped before
instruction execution and is not counted as passing.

The audit found 330 incorrect emitted sites at 217 distinct guest PCs across
162 function/header records. The isolated corrected caller passes the old Lua
corruption and reaches later startup work. The legacy Lua overrides are now
opt-in behind `BLUR_ENABLE_UNVERIFIED_LUA_OVERRIDES`; new builds use translated
bodies. An existing-binary diagnostic module provides the same routing without
hand-editing generated instructions. This is not independent Lua/CRT ABI parity.

## Full generation and current integrated boundary

The corrected translator ran against unchanged baseline listings plus the
previously verified supplemental entry at `00560000`. The new `--listing-overlay`
option preserves those baseline files and rejects duplicate entries.

The process returned zero: 48,409 emitted functions, 68,568 entries, no failed or
unmodelled functions, and no unresolved jump-table sites/entries. It also reports
67 withdrawn guesses and 50 recovery diagnostics in JSON (69 recovery events in
the console summary). Their contents differ from the old pass and still require
review. **This is a generation result, not renewed whole-image acceptance or a
compiled/booted replacement host.** Output is ignored under
`local/startup-movsd-full-r1/`.

The runtime now supports `RECOMP_ASSET_ROOT` as a read/list fallback. Mutations
continue to resolve against the scratch executable directory or installed
profile resolver; they never fall through to that asset root. A synthetic native
contract test checks case-insensitive lookup, file bytes, write/delete/rename
refusal and disabling the fallback.

The existing host with the bounded caller replacement and raw-Lua routing now
opens the real root, streaming, shader, gamedata and boot archives read-only. It
also finds both splash movie files. The bounded 45-second run times out while
processing resources; no movie open/decode or main-menu frame was established.
The older run without asset access reached a later worker crash instead. Those
are separate diagnostic conditions, not a passing startup result. Host profile
paths are fresh per probe; with mods disabled, Win32 writes still use the ignored
scratch EXE directory rather than a separate per-run file overlay.

## Evidence, validation and next gate

- `reports/startup-movsd-audit.json`, `startup-movie-inputs.json` and native logs.
- `local/startup-movsd-recovery-r1/`, `startup-movsd-module-r1/`,
  `startup-global-watch-r1/`, `startup-watch-free-stop-r1/` and
  `startup-with-assets-r{1,2}/` preserve the observations and experiments.
- `local/startup-bizarre-metal.png` is the inspected Metal framebuffer.
- `recomp/patches/startup-movie-runtime-combined.patch` preserves the complete
  experimental kit diff against its pin, including earlier unreviewed work.
  It is a reproducibility artifact, not acceptance of every included change.
- 69 project Python tests, the CMake build and four project CTests pass. The
  focused native SEH/strict-service CTests pass, including the new read-root case.
  The decoder driver has 169 passing tests; all 75 instruction comparisons pass.

Next: review the changed recovery diagnostics, configure/test the real Blur
setjmp/longjmp seam in the translator, then build the corrected whole-game
output with the speculative Lua overrides disabled. Trace the first resource
load that fails to complete before blaming the archive format or introducing a
decoder bypass. Connect verified movie decode to the game's Bink framebuffer
contract and resource lifetime before claiming its startup-movie milestone.
Main-menu acceptance requires a nonuniform, visually inspected game frame and
working navigation. No fabricated menu, authentication or success result is used.

## Continuation: configured nonlocal error returns

The translator now takes explicit `[translate].intrinsics` lists instead of
assuming another executable's addresses. The Blur configuration names the
`_setjmp3` import thunk at VA `00d449ce` / RVA `009449ce` and the pinned CRT's
`longjmp` export at VA `78594458` / RVA `00074458`. The executable and CRT hashes
above apply. PE bytes `ff25d434e500` identify the thunk's jump through IAT
`00e534d4`, whose import is `MSVCR90.dll!_setjmp3`; the DLL export table identifies
`longjmp`. Confidence is high for these addresses and argument positions.
`reports/startup-intrinsics-symbols.json` records the observations.

This is a new runtime implementation of the nonlocal-return seam, not recovered
CRT source. Generated code uses `setjmp` in a legal controlling expression. The
landing branch takes the value restored into guest EAX, preserving values such
as 7 and negative values; a requested zero returns one. Records also reject a
jump from a different CPU context. Synthetic native tests exercise five values,
nested buffers, restored ESP/EIP/EBX and the foreign-context abort. This does not
establish full MSVC structured-unwind/callback parity, or detect every stale
buffer after its generated host frame has returned. Indirect setjmp entries
continue to fail explicitly rather than save an already-returned helper frame.

A fresh experimental build is staged under ignored `local/startup-native-r2/`,
with the corrected scalar MOVSD decoder, supplemental observed entry, explicit
intrinsics and the opt-in speculative Lua overrides disabled. Its generation,
compilation and boot results are recorded separately below; they must not be
inferred from the synthetic tests or the standalone movie viewer.

The CRT generation exposed a second structural defect: an intrinsic replacement
omitted its internal entry definitions while retaining them in the dispatch
table. In the pinned CRT above, `785944a6` / RVA `000744a6` is an internal unwind
continuation: assembly pushes that address before the call at `785944a1`, and the
continuation begins with `POP ESI; POP EBX`. Confidence is high for the bytes and
internal-entry relationship; CRT unwind parity is still unverified. The
translator now substitutes only the audited primary entry and preserves original
translated bodies for internal entries. A synthetic regression covers this case.

`tools/verify_recomp_generation.py` independently checks the generator's raw
entry table against actual C function definitions, rejecting missing or duplicate
bodies and repeated entries. Its five synthetic tests include the omitted
continuation case. This is structural validation of the known generated format,
not a general C parser or behavioral acceptance.

The main image and CRT now pass that check at 68,568 and 3,290 entries. Main-image
recovery diagnostics and the 67 withdrawn guesses exactly match the previous
MOVSD generation. The new main-image generation used the translator revision
before the later intrinsic-continuation fix; its only intrinsic body is a
single-entry import thunk, so that adjustment has no effect on this image. The
revision is preserved in `local/startup-native-r2/translator-main-source-snapshot.py`;
the CRT was regenerated with the continuation fix. Neither generation used the
historical generated-code postprocessor.

The movie caller's static framebuffer observations are documented separately in
[BINK_FRAMEBUFFER_CONTRACT.md](BINK_FRAMEBUFFER_CONTRACT.md). They identify the
lock/register/decode/unlock lifetime and a record frame-rate mismatch, without
claiming that either explains the earlier loading stall.

## Completed native rebuild and positioned-read fix

The corrected whole-image host was compiled and linked as Mach-O ARM64 at
`local/startup-native-r2/build/recomp/pop_headless`. It uses the normal `-O2`
generated-code configuration; an unusually slow chunk finished before an
attempted cancellation, and no optimization override was applied. The fresh
host's three focused native CTests pass. There are 74 passing project Python
tests, four passing project CTests, and 201 passing translator/configuration tests.

The first new host still stalled. A new observer, `recomp/diagnostics/stream_decode_watch.c`,
captured an actual decoder error at VA `00add770` / RVA `006dd770`: return -3,
message `incorrect header check`, two input bytes consumed and no output. Caller
VA `00cb98a2` / RVA `008b98a2` follows the call in the previously documented zlib
reader. Both refer to the pinned Blur SHA above; confidence is high for reached
calls, field accesses and returned diagnostic text. The input prefix matched
`shaders-speed.pak` at file offset `0x60004`.

File tracing then showed the cause: a ReadFile request supplied an OVERLAPPED
offset of 534528 while the descriptor was at 393216. The shim ignored the supplied
offset and read unrelated bytes. This was an OS-service bug, not evidence that
the archive needed a decoder bypass. The implementation now honors 64-bit
positioned reads, retains the cursor/offset for asynchronous handles, publishes
immediate completion status/count and signals the optional event. Synchronous
reads continue to advance the cursor. The contract follows Microsoft's
[ReadFile documentation](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile).
Queued asynchronous I/O, completion ports and GetOverlappedResult are not thereby
implemented. Synthetic fixtures cover positioned bytes, cursor/offset behavior,
partial reads, EOF/high offsets, invalid metadata, completion events and unchanged
file contents. The strict native suite now has ten subprocess cases.

**Observed advance:** the next run passes the previous header-error loop, reads
many additional resource offsets and makes 173 texture-creation requests before
aborting after about four seconds. There were no stream-error observer messages
in this run. The next exact unsupported instruction is `CVTPS2PD XMM0,XMM0`, bytes
`0f5ac0`, at VA `00797e53` / RVA `00397e53`, verified directly from the pinned PE.
Confidence is high for this opcode and abort location. This is SSE2; the runtime's
message about an unadvertised CPU extension is misleading here. An existing
`recomp_cvtps2pd` helper is present in the experimental `runtime/x86.h`, but the
emitter still produces a trap. Audit and test that helper before wiring it into
the translator; do not reapply the historical generated-C postprocessor.

Evidence: `local/startup-native-r2/probe-r1/` (old stall), `probe-stream-r1/`
(decoder message), `probe-stream-offset-r1/` (wrong requested/current offset), and
`probe-positioned-r1/` (new unsupported-opcode abort). All input hashes remained
unchanged. The only captured startup framebuffer remains uniformly black;
integrated movie playback and the main menu are still unverified. Video decoding
is disabled in this diagnostic host, and the Bink framebuffer/audio adapters
still have the separate gaps described above.

The focused continuation patch is
`recomp/patches/startup-native-r2-reviewed-slice.patch`; the combined kit patch
also includes earlier unreviewed work and is not blanket acceptance. The next
conversation should read `docs/HANDOFF_STARTUP_NATIVE_2026-10-18.md` and reuse the
existing build, generations and logs.


## CVTPS2PD lowering and bounded startup advance — October 18

The helper audit and new register/alias/m64 oracle coverage pass: 81 synthetic
instruction tests, including directed IEEE edge values. The translator now emits
the helper; no generated instructions were edited. Three bounded, freshly emitted
complete functions retain all existing alternate entries and advance the unchanged
ARM64 host past the prior abort and two subsequent CVTPS2PD sites. Final execution
now aborts on `CVTPS2PD XMM1,XMM0`, bytes `0f5ac8`, VA `004e067d` / RVA `000e067d`
in the same pinned executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
Confidence is high from the final strict log and direct PE decode. Complete owner
VA `004e0620` / RVA `000e0620` is 4,904 bytes and has four existing alternate
entries, exceeding the current bounded generator's 4,096-byte limit. Its next
integration should preserve that owner and all entries, with strict CFG checks.

The final probe has no stream error or missing-dispatch report and captures only
a black frame; no movie/menu milestone is claimed. Default masked MXCSR only is
covered by the instruction tests. Root validation: 75 unittest tests, CMake build,
four CTests; 237 additional translator/config/import/SEH/table tests pass. See
[the checkpoint](HANDOFF_CVTPS2PD_2026-10-18.md) for exact evidence, module paths,
reproduction command, known test prerequisites and the focused patch. The host
was reused without whole-image regeneration or a native host rebuild.


The next bounded continuation integrated the complete 4,904-byte owner with all
four alternate entries using an explicit 8 KiB tool bound. Startup passes the
CVTPS2PD and now aborts on `CMPNLESS XMM3,XMM2`, bytes `f30fc2da06`, VA `004e06bc`
/ RVA `000e06bc` in the same pinned executable. Confidence is high from direct PE
decode and strict probe evidence. Four CMPNLESS and two CVTPD2PS runtime abort
boundaries remain in this function; only the first is observed dynamically.
The bounded report now lists intentional SSE traps, which the translator's
unmodelled diagnostic list omits. New synthetic regressions cover extent bounds
and trap reporting. This step passes 77 root tests, the CMake build and four
CTests; the unchanged host and previous modules were reused. See
[the comparison-instruction checkpoint](HANDOFF_STARTUP_COMPARE_2026-10-18.md).
No movie or menu frame is established.
