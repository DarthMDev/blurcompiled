# Pinned SDK register frontend and observer candidate

October 27, 2026. **The CPU register mapper and an isolated Xbox observer
candidate are built. The user-approved candidate has now executed live.**
It keeps the reference Metal rendering path and reports register-state matches;
it does not submit Plume packets or establish in-game Plume rendering.

## New implementation and validation

`recomp/native/rex_plume_state.*` maps the current SDK register file into the
register-dependent portion of `UiState`. It compiles directly against the SDK's
typed register definitions. `recomp/rex-plume-state-pin.json` records hashes of
those definitions and the effective-state policies used from the SDK. CMake
rejects a changed reference definition/policy rather than silently reusing the
mapping. No GPU/platform API enters this mapper.

The mapper handles effective depth/stencil, alpha coverage, cull/winding, RT0
write masks and blend factors/operations. Unsupported EDRAM modes, extra color
target writes and polygon offset requests are rejected. Unsupported face/fill
or blend forms remain unknown and cannot pass the packet frontend's eligibility
checks. Attachment formats/counts, actual sampler/view properties, processed
primitive/index information, resource contents and zero-initialization proof
must come separately from the current draw; the mapper does not invent them.

One important normalized-state case is now verified: the run5 UI register value
`RB_DEPTHCONTROL=0x00700764` has the depth-write bit set but depth testing disabled.
The SDK disables effective writes in that case. The mapper follows that rule,
and also follows the redundant always-pass depth-test normalization. Treating
the raw write bit alone as effective state would misclassify this pass.

The mapped register-dependent fields match the actual run5 UI register snapshot
and the previously recorded effective pipeline. Synthetic tests cover the
depth normalization cases, enabled stencil/coverage, unsupported states, register
bounds and preservation of unresolved resource fields. ARM64 CMake, all ten
CTest checks and 103 Python tests pass (one existing Python skip).

## Isolated candidate

`recomp/native/rex_plume_observer.hpp` is attached after copy-mode handling in a
separate copy of the SDK draw path. It counts calls and register-core matches,
and explicitly logs `packets_submitted=0 resources_unresolved=1`. It does not
claim a core-register match is complete UI eligibility, change rendering state,
read private GPU pixels or arm any capture. Logging is bounded by first-event
and periodic counters.

`tools/build_plume_state_candidate.py` reads the reference Ninja compile/link
commands without invoking a build or modifying its outputs. It compiles a new
command-processor object and links a new plugin using the preserved reference
objects. The original SDK source, original command-processor object and original
plugin hashes are checked before and after. The new runtime copies the matching
host/runtime binaries; original assets and saves are not copied or changed.

The successful candidate is
`local/plume-rex-frontend-20261027/candidate-002`. Its `result.json` records every
link-input hash, new artifact hashes, source hash and protected-reference checks.
An initial candidate001 compile failed because the copied source lost a local
header search path. The builder now adds the original source directory as a
read-only include path; the failed attempt and log remain preserved.

The new plugin builds successfully. Existing SDK compiler warnings remain; no
graphics fix, performance result or live execution is inferred from that build.
`launch-proposal.json` specifies a new private run, explicit windowed 960x540
arguments and no capture/automatic shutdown. The user explicitly approved
“Launch the observer candidate”; run-001 was launched with those arguments and
private user/cache/tmp paths after verifying hashes and no existing Blur process.

The live observer reported 65,536 draws, 992 register-core matches and 2,988
unsupported states at its first periodic checkpoint. It reported zero Plume
packets and unresolved resources throughout. This confirms the injected CPU
observer executes and sees matching register tuples; it does not establish full
draw eligibility or visual/gameplay correctness. Apple M3 Metal initialization
and title 41560845 were logged. The previously observed surface-pitch warning
also appeared; no cause or fix is inferred. A bounded log prefix, SHA-256 and
counts are saved under candidate-002/run-001. No capture was armed or shutdown
requested; the window was left running for the user.

## Reproduce

```sh
cmake -S . -B build-plume -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DBLUR_PLUME_RENDERER=ON -DBLUR_PLUME_GUEST_ADAPTER=ON \
  -DBLUR_REX_PLUME_FRONTEND=ON
cmake --build build-plume -j 4
python3 -m unittest discover -s tests -v
ctest --test-dir build-plume --output-on-failure
./build-plume/blur-rex-plume-state-tests \
  local/x360-feasibility/internal-workspaces-20261025/blur-image-ladder-runtime-20261025/run5/ui-registers.bin
python3 tools/build_plume_state_candidate.py local/plume-rex-frontend-20261027/candidate-new
```

The SDK state target is opt-in and uses C++23 for compatibility with the current
SDK headers. The renderer and native packet interfaces retain C++20. The builder
does not launch the game; it rejects existing output directories.

Next, connect verified current resources and shader/constant blocks to the native packet
builder and C bridge. Persistent/nonzero targets and the remaining pass chain
still need implementation before the complete backend can be replaced.
Preserve the older builds and useful diagnostics until that replacement is
verified. No cleanup was performed and no guest symbol was recovered or hooked.

## Current shader constants slice

`recomp/native/rex_plume_constants.*` now packs owned UI constant blocks from
current register words and current analyzed vertex/pixel float maps: system
(512 bytes), VS floats (64), PS floats (96), bool/loop (160) and fetch (768).
Register-derived blocks use explicit little endian stores and preserve bit
patterns. Dynamic float addressing, inconsistent counts, other float layouts and
incorrect register/system extents are rejected. System constants must come after
the SDK's current system-constant update; raw registers alone are insufficient.
The SDK system structure and analyzed-map definitions are added to the source pin.

The packet contract and observer share an exact translated shader-pair gate.
Synthetic tests cover all four bitmap banks, VS/PS separation, bit preservation,
ownership and rejection cases. An offline check accepts the run5 modules, rejects
a one-bit mutation, and reproduces all 160 bool/loop and 768 fetch bytes from the
actual register snapshot. That initial check did not load the captured float
bitmaps; the full offline check below now verifies both float blocks too.
The system block is supplied, not independently reconstructed.
ARM64 CMake, 11 CTest checks and the 103-test Python
suite pass (one Python skip).

`candidate-003` is built and unlaunched. Its separate SPIR-V draw-path hook runs
after the existing system-constant update and CPU uniform uploads. It gates the
current translated modules, reads their current analyzed maps, packs the blocks
independently and compares them to the runtime's CPU-visible uniform bytes. It
logs aggregate matches, mismatches and rejections. It never writes resource
payloads, reads GPU-private resources, submits Plume packets, changes reference
draws or arms capture. An overlay compile-time check verifies the system structure
is 512 bytes. Original SDK source, object and plugin hashes remain unchanged.
Its launch proposal specifies private run paths and a 960x540 window; fresh
approval is required. No existing Blur process was present at this checkpoint.

```sh
./build-plume/blur-rex-plume-constants-tests \
  local/plume-x360-20261027/packet/ui.bpg \
  local/x360-feasibility/internal-workspaces-20261025/blur-image-ladder-runtime-20261025/run5/ui-registers.bin
```

Next, validate float packing against actual live uploads, then integrate current
attachments, sampler/view state, processed indices, synchronized vertex/texture
contents, persistent targets and remaining passes. Neither observer candidate
is a Plume game renderer or a playable port.

## Composed SDK draw frontend (offline verified)

`recomp/native/rex_plume_draw.*` connects the SDK register mapper, current shader
pair gate and constant packer to the native packet builder. It accepts separate
resolved resource views, checks their physical vertex range against the current
typed SDK fetch descriptor, derives the selected shader's fetch-slot/stride ABI
only after exact module matching, and produces an owned, validated packet.
Replacement shader/constant spans in resource input, unknown resource state,
invalid fetch types, out-of-bounds addresses and mismatched buffer extents fail.
It does not read guest pointers or assume CPU RAM contains current GPU writes.
The caller must supply current analyzed maps and synchronized resources from the
same draw. The system block must come from the SDK's current update.

Correction to the earlier checkpoint: `run5/ui-float-map-vs.bin` and
`ui-float-map-ps.bin` exist in the preserved capture. Each contains the four
64-bit float bitmaps. Loading those and the separate system block now verifies
all five blocks against the recorded uniform uploads. Four blocks are rebuilt
from registers/maps; the system block is checked against the separately saved
CPU system structure. Offline float counts are bitmap population counts; the
live observer still uses the actual analyzed SDK count/dynamic-addressing fields.

The composed frontend rebuilds the verified BPG1 packet byte-for-byte using these
captured shader/register inputs and the replay's resource payloads. Remaining
attachment/sampler/primitive/texture state is an explicitly fixed test fixture,
not a live resource mapper. This distinction is checked: replacing it with
unknown state fails. This check also rejects wrong vertex addresses/lengths and
modified shaders. Synthetic tests cover fetch types, bounds and stale replacement
inputs. ARM64 CMake, all 12 CTest checks and the Python suite pass (103 tests,
one skip).

The rebuilt packet was submitted twice through the dynamic Plume bridge offscreen
on Apple M3; outputs were identical and a stale submission was rejected. Its
327,680-pixel reference crop matches every RGBA byte in all four sample slots,
with maximum error zero. This remains the single UI pass, not a complete screen
or an in-game result. Records are in `local/plume-rex-draw-20261027`.

```sh
./build-plume/blur-rex-plume-draw-tests \
  local/plume-x360-20261027/packet/ui.bpg \
  local/x360-feasibility/internal-workspaces-20261025/blur-image-ladder-runtime-20261025/run5
```

The user declined candidate-003 launch. No game launch or capture was performed
for this checkpoint, and no new launch approval is requested. Further offline
work can replace the remaining resource fixture with current descriptor mapping
and establish resource synchronization/target persistence before live submission.

## Texture descriptor mapping

`recomp/native/rex_plume_texture.*` now checks the current typed SDK texture
fetch against separately supplied facts from the resolved native view. The
bounded implementation accepts unsigned, identity-swizzled 2D BC1 textures with
one array layer and a complete decoded mip chain. Dimensions, physical base/mip
pages, view format, first mip, layer count and every mip payload extent must
agree. Missing or unsupported descriptors fail; prefilled `UiState` texture
fields cannot bypass this check in `make_rex_ui_packet`.

The mapper follows the pinned SDK texture utility's 17-bit physical-page mask
and dimension-based mip clamping. Raw fetch pages never become host pointers.
Base-only 1x1 resources ignore unused mip pointers, matching that policy. The
source of this policy is added to `recomp/rex-plume-state-pin.json`.
Guest tiling/endian decoding and resource synchronization remain caller duties;
shape validation does not establish that supplied bytes are current GPU content.
No sampler settings, attachments or target-initialization facts are invented.

Synthetic tests cover invalid types and component interpretation, oversized
dimensions, unknown/mismatched native views, physical-page aliases, mip-range
clamping and malformed decoded payloads. The run5 atlas's resolved view facts
and all ten mips agree with its current register descriptor. Its texture format
and identity-view fields are now mapped rather than supplied by the old state
fixture. Other native-view facts still come from recorded capture evidence for
this offline test; a live shim must query the actual resolved view.

The reconstructed BPG1 packet remains byte-identical to the prior GPU-verified
packet. No new GPU replay was needed because neither packet bytes nor renderer
code changed. ARM64 CMake, all 13 CTest checks and the 103-test Python suite pass
(one skip). The checkpoint is `local/plume-rex-texture-20261027`; candidate-003
stays unlaunched, and no game launch, capture or cleanup occurred.

## Active Plume bridge and candidate-005

October 29, 2026. **The active Plume bridge connector and isolated runtime
candidate-005 are built.** They connect verified Plume UI/composite draw submission,
4x MSAA UI tile resolve, and terminal HDR resolve routing into the candidate plugin.

### Implemented and verified slices:

1. **Composite Packet Format (BPG2)**:
   - Implemented `encode_composite` and `decode_composite` in `src/render/guest_packet.cpp`.
   - Strict 32 MiB bounds, versioned magic `0x32475042` ('BPG2'), 1x MSAA verification,
     SPIR-V header verification, and 2-texture layout enforcement (`1280x720` UI + `320x180` bloom/blur).
   - Validated via synthetic round-trip and corrupted-data rejection tests in `tests/guest_packet_tests.cpp`.

2. **Bridge ABI V2 with Composite Submission**:
   - Extended `src/render/plume_bridge.h` and `src/render/plume_bridge.cpp` with `BLUR_PLUME_ABI_V2 (2u)`
     and `BLUR_PLUME_CAP_COMPOSITE (4ull)`.
   - Added `BlurPlumeCompositeSubmission`, `BlurPlumeCompositeOutput`, and `BlurPlumeApiV2`.
   - Enforces C ABI stability, mono-threaded owner safety, monotonic host frame/draw identities,
     and fault isolation.
   - Preserves 100% backward compatibility with `BLUR_PLUME_ABI_V1`.
   - Validated via `tests/bridge_header_test.c` (C layout assertions) and `tests/plume_bridge_tests.cpp`.

3. **Active Plume Runtime Connector (`recomp/native/rex_plume_active.hpp`)**:
   - `ensure_rex_plume_bridge`: dynamically discovers and loads `libblur-plume-bridge.dylib`
     with local symbol scope, negotiating ABI V2.
   - `handle_rex_plume_ui_draw`: decodes BC1 mips, packs uniform blocks, maps primitive/scissor/target,
     and submits UI packets to Plume.
   - `handle_rex_plume_composite_draw`: feeds resolved UI and secondary texture into BPG2 composite
     packets and submits to Plume `Renderer::draw_composite`.
   - `handle_rex_plume_copy`: routes 4x MSAA UI tile resolve strips (`resolve_rex_rgba8_msaa4_strip`)
     and terminal RGBA16F-to-7e3 HDR resolve (`resolve_rex_rgba16f_to_7e3_rgba8`) into linear swap output
     and tiled guest physical memory.
   - `handle_rex_plume_swap`: tracks presentation lifecycle and frame statistics.
   - Validated via `tests/rex_plume_active_tests.cpp` (14/14 ctests passing).

4. **Candidate-005 Built in Local Workspace**:
   - `tools/build_plume_state_candidate.py` modified `MetalCommandProcessor` to hook active Plume draw
     dispatch, resolve copies, and swap tracking.
   - Built `local/plume-rex-frontend-20261027/candidate-005` with `blur_x360`, `librexgpu-xenos.dylib`,
     `libblur-plume-bridge.dylib`, and `librexruntime.dylib`.
   - Generated `launch-proposal.json` for private windowed execution (no capture armed, no automatic shutdown).
   - Reference SDK hashes re-verified; no game launched.


## Integration blockers corrected — October 29, 2026

Observed: candidate-006's existing `run-001/runtime.log` reports zero UI and
composite submissions through swap 2400. Source inspection shows its only
`dispatch_rex_plume_draw` call inside the MSC path; `UseSpirvCrossPath()` returns
through `IssueDrawMsl` before that call when enabled. The initial inference that
the live run selected SPIRV-Cross was wrong: candidate-007 later reports
`spirvcross=false`. Zero submissions alone do not identify which gate failed or
establish the cause of dark/red output or panel geometry artifacts.

New implementation: `tools/build_plume_state_candidate.py` now inserts the hook
in `IssueDrawMsl` after the current SPIR-V system-constant update. The original
SDK draw remains in place. The active UI adapter supplies raw guest texture data
only; previously it decoded mips and supplied both representations, which
`make_rex_ui_packet` explicitly rejects. Decode now occurs once in that builder.
Pre-submission exceptions are logged on the first and every 256th rejection.

A synthetic register/RAM regression in `rex_plume_active_tests.cpp` exercises the
active UI adapter through a CPU bridge sink and checks the resulting packet's
vertex address, converted indices, mip count and texture byte. This proves
submission reach without asserting GPU or game output. Existing embedded shader
contracts are reused; synthetic fixtures supply all register/resource values.

Candidate-007 compiles and links against the preserved reference objects; the
builder reverified protected source/object/plugin hashes. It has not been
launched. Python: 103 tests, one skipped. Plume CMake build and all 14 CTest
checks pass. The separate native validation build and its checks also pass.
The older `build/` cache still enables an unrelated startup adapter target whose
`runtime/imports.h` dependency is absent; that cached optional build fails.

Remaining live gaps include GPU-to-CPU resource synchronization, actual UI
resolve source/strip dimensions, HDR target preload/persistence, and descriptor
mapping in place of historical composite addresses. The copy hook currently
passes an empty MSAA source and fixed dimensions; it does not establish a working
three-strip Plume UI resolve. No visual correction, complete renderer replacement,
interactive menu, or race is claimed by these changes.

Cleanup: superseded candidate-002/003/005 runtime binaries and command-processor
objects were moved into a verified recovery archive before loose-file removal.
`local/cleanup-x360-20261029/result.json` records each size and SHA-256, archive
identity and a net payload reduction of 235,692,434 bytes (about 225 MiB).
All source, logs, saves, captures, candidate-006, reference SDK/build inputs and
original game assets remain. The new candidate is retained separately.

### Candidate-007 title-screen feedback and dual-backend follow-up

The user supplied a title-screen screenshot showing bright logo/blue icon/text
and a very dark car/ground with red highlights. This is title-screen evidence,
not evidence that main-menu panel artifacts are fixed. Its run log reports
`spirvcross=false` and zero UI/composite submissions through swap 1080. The
candidate-007 hook was incorrectly moved exclusively to the unused branch.

The builder now instruments both MSC and SPIRV-Cross, after an explicit current
SPIR-V system-constant update in each branch. Dispatch logs identify the calling
backend, examined draw count, exact UI/composite microcode match counts, and
current shader word counts. This separates hook reach from shader eligibility
and submission failures without relaxing the shader gate. The original SDK draw
is retained. Candidate-008 is the isolated follow-up; no change to the running
candidate-007 or its user data is needed to build it.

### Settled candidate-008: shader identity byte order

User reported unchanged graphics and confirmed the menu had settled. The existing
candidate-008 log records MSC dispatch through 458,752 examined draws with zero
UI/composite matches and no bridge load or submissions. No capture was armed.

Observed capture writer at preserved run5 Vulkan command-processor source saves
`Shader::ucode_dwords()` directly, with `count * 4` bytes. The embedded identity
arrays had interpreted those bytes as big-endian words; the SDK API on this ARM64
host exposes the little-endian native words. For example, captured UI VS bytes
`03 30 15 70` represent native word `0x70153003`, while the prior array contained
`0x03301570`. All four embedded microcode identity arrays had this mismatch.

New implementation corrects all four arrays to the native word values without
altering the SPIR-V byte arrays or relaxing exact identity matching. An independent
offline check compares every corrected word with each preserved capture and
records SHA-256/word counts in
`local/cleanup-x360-20261029/shader-identity-verification.json`. The active test
checks the independently interpreted first words and rejects a byte-swapped UI
identity. Candidate-009 contains the correction; this is a shader eligibility
fix, not proof of a complete live Plume frame or a visual root-cause fix.

### Settled candidate-009: omitted SPIR-V vertex limits — October 30

Observed: candidate-009 now matches both exact shader pairs and loads the ABI V2
bridge. It still reports zero UI/composite submissions; sampled rejection logs
include `UI index clamp differs`, unsupported viewport/blend state, and incomplete
texture mip chains. Thus the byte-order fix repaired eligibility, not the visible
rendering. The user reports unchanged graphics after settling the menu.

The pinned Metal `UpdateSpirvSystemConstantValues` zeroes its 512-byte buffer and
omits `vertex_index_min`/`vertex_index_max`. The pinned SDK Vulkan updater fills
both from current `VGT_MIN_VTX_INDX`/`VGT_MAX_VTX_INDX` registers. Their struct
offsets are 496/500. A zero maximum fails the bounded UI packet contract for any
converted quad containing indices greater than zero.

New implementation: `complete_rex_spirv_index_limits` copies these current raw
register values into the pinned constant layout. Candidate source generation
calls it in the Metal SPIR-V updater, with compile-time assertions for both field
offsets. The preserved SDK/reference binaries remain untouched. Synthetic tests
check byte order, preservation of unrelated constants, changing live limits,
bounds rejection, and a complete active UI packet rejecting zero limits then
reaching the bridge sink after completion. This does not relax any pipeline,
resource, or packet eligibility guard. Candidate-010 is the isolated follow-up.

The run's selected log lines and SHA-256 hashes of the compared SDK source/header
files are recorded in ignored
`local/cleanup-x360-20261029/candidate-009-index-limit-evidence.json`.
The remaining graphics issues are unverified; the current full frame still comes
from the SDK backend, and the live resource/resolve/preload integration gaps
listed above remain. No capture was armed and no game process was terminated.


### Shared dependency repair during the next candidate build

The first follow-up compile exposed a cleanup error: the current Metal SDK's
`thirdparty` symlink still targeted the retired older renderer's source tree.
Cached CMake checks and link-input existence did not detect that broken include
path. Candidate-011's build failed before producing a new runtime.

Restored the 1,279 dependency files recorded by the reference Ninja graph from
verified recovery archives, including seven libmspack symlinks and their targets,
into `local/x360-feasibility/shared-dependencies-20261030`. Repointed the SDK's
`thirdparty` symlink to that dedicated dependency cache. Recovered payload is
49,550,724 bytes; no old game build was restored. The archive/file hashes and
symlink repair are recorded in
`reports/plume-research-20261030/shared-dependency-repair.json`.
Reference source contents, object files, original game inputs and existing
candidate binaries remain unchanged. Candidate-012 is the fresh retry.

### Candidate-015: submission identities and SPIR-V exponent trial

New host implementation: UI and composite submissions now share a monotonically
increasing context-wide sequence. The recycled SDK draw slot remains diagnostic
metadata. Empty HDR preloads defer composite submissions before touching the
bridge, preventing a preserve request without synchronized scene contents.
Synthetic active-bridge tests cover recycled SDK slots, interleaved identities,
frame changes, overflow, and missing HDR preloads.

Candidate-012 encountered recovered-header timestamp differences with the old
PCH; candidate-013 exposed an omitted empty include-path component. The builder
now compiles the original textual prefix header rather than consuming the old
timestamped PCH. Restored empty `xxHash/cmake_unofficial` and `renderdoc`
directories in the dedicated dependency cache; the former is needed to traverse
the recorded `xxHash/cmake_unofficial/..` include path. No PCH validation bypass
or reference-object rewrite is used.

The optional `--spirv-exponent-fix` candidate build replaces the isolated SPIR-V
fetch object with the word-3 correction described in
`PLUME_XERENGE_RESEARCH_2026-10-30.md`. The runtime switch
`--blur_spirv_fetch_exponent_fix=false` retains word-4 behavior for a fresh-cache
control run. Candidate-014 built but exited before rendering because this SDK
explicitly rejects SPIRV-Cross during setup. Diagnostic builds using this option
now enable that existing experimental backend with a warning; normal candidate
builds and the reference SDK retain the rejection guard.

Observed in candidate-015/run-001 on 2026-10-30:

- Runtime confirms `spirvcross=true` and texture exponent fetch word 3.
- The user reports the settled title/menu remains **dark/red**.
- At swap 420, the periodic status reports 286 accepted UI draws, zero composite
  draws, zero UI MSAA resolves, and zero terminal resolves.
- No monotonically increasing identity errors or UI bridge-submit rejections.
- 590 SDK resolve errors report a 1280-pixel region against surface pitch 320.
  This is an observed mismatch, not proof of its cause or the dark/red cause.
- Composite correctly remains deferred without synchronized RGBA16F scene data.
- The verified test PID was terminated with SIGTERM and confirmed closed.

Conclusion: the ordering defect is fixed; the exponent correction is exercised
but does not resolve the reported scene appearance. The full frame still comes
from the SDK backend. Resource synchronization, resolve interpretation and HDR
composite handoff remain unresolved. No rendered-frame capture was armed.
The separate CPU vector-pack correction remains deferred because the affected
emitted operations were not found in the current Blur generated sources.

Validation: 103 Python tests run (one skipped), CMake build successful, all 14
CTest checks passed. Ignored per-run evidence is in
`local/plume-rex-frontend-20261027/candidate-015/run-001/diagnostic-summary.json`;
build/test records are under `reports/plume-research-20261030`.

### Candidate-016: bounded fullscreen pitch trial (negative visual result)

Inspection of the preserved capture-v2 frame identifies resolve records 20
(depth) and 21 (color): both use a 1280x720 destination, matching window scissor,
and vertex bounds -0.5..1279.5 by -0.5..719.5, but surface pitch 320 and no MSAA.
Other 320-wide resolves have matching 320-wide vertices and remain unaffected.
The original resolver clamps to the source pitch, consistent with the
[upstream Xenia implementation](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/draw_util.cc).
The warnings were also present in older Vulkan runs; they are not established
as Metal-specific failures. Input hashes and decoded fields are in
`reports/plume-research-20261030/resolve-pitch-trial-evidence.json`.

New diagnostic implementation: `recomp/native/rex_resolve_pitch_trial.hpp`
recognizes only this bounded full-screen mismatch when destination, scissor,
finite vertex bounds, sample count and copy control agree. The candidate builder
option `--resolve-pitch-trial` adds a default-false runtime control
`blur_resolve_pitch_trial`. When explicitly enabled, it temporarily substitutes
source pitch 1280 during the SDK resolve, preserving all other surface bits and
restoring the complete register word with a scope guard immediately afterward.
It does not change guest memory, original input binaries, reference SDK source,
or ordinary candidate defaults. This tests a stale-pitch hypothesis; it is not
a recovered hardware rule or a verified correction.

Observed in candidate-016/run-001 on 2026-10-30:

- The normal MSC backend is confirmed (`spirvcross=false`).
- The trial activates repeatedly (the log reaches application 1024).
- Zero outside-surface-pitch warnings, versus repeated warnings in prior runs.
- The user reports the settled title/menu is **still dark/red**.
- At swap 720: 561 UI draws, zero composite draws and zero Plume resolves;
  zero monotonic-identity errors.
- One MSC geometry pipeline materialization failure, also present in
  candidate-010. Candidate-015's SPIRV-Cross run had no such failure but the same
  user-reported dark/red appearance; this does not establish that failure as the
  sole cause.
- Test PID received SIGTERM and was confirmed closed; no capture was armed.

Changing the source-pitch interpretation alone did not correct the visible
scene. Keep the trial opt-in; do not treat removal of warnings as proof of
correct rendering. The remaining HDR format, intermediate scene color and
composite/resolve handoff need independent inspection.

Validation: synthetic fixtures cover the eligible fullscreen state and preserve
unrelated surface bits, reject downsample rectangles, MSAA, clear requests,
different destinations, offsets and absent registers. CMake build succeeded;
103 Python tests ran (one skipped) and all 14 CTest checks passed. Evidence and
the closed-process receipt are under
`local/plume-rex-frontend-20261027/candidate-016/run-001`.
