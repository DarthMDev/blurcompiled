# Xbox UI adapter through Plume

October 27, 2026. **One captured guest UI pass is verified through Plume/Metal.
Live Xbox integration and in-game play remain incomplete.** The user selected
finishing the Plume adapter before pursuing a race on the previous Metal build.
The older Xbox builds remain reference inputs; cleanup is deferred until a
replacement is verified.

Update: [the loadable native host bridge and C++ packet builder are now verified](PLUME_HOST_BRIDGE.md).
Live Xbox integration is still pending; this is the handoff point for Sol Medium.

## Measured result

The adapter replayed the actual run5 UI pass at sequence 252302, capture
`d30f75635fd6e31859375b9bd49b7191`, using its 162 native indices, 108 guest
vertices, five packed constant blocks, translated VS/PS modules and ten BC1
texture mips. Recorded state includes a 1280x512 RGBA8 target, 1280x720 viewport,
1280x256 scissor, four samples, back-face culling and source-alpha blending for
both RGB and alpha, with depth test/write disabled.

**All 327,680 pixels in the captured 1280x256 crop, all four sample slots and all
RGBA channels match the reference bytes exactly: maximum error zero.** Both
have 20,849 pixels containing nonzero RGB. The complete 1280x512 output is saved,
but the uncaptured lower rows are not comparison evidence. Byte equality of
these sample slots does not establish a general cross-API sample-position map.
This reproduces the reference pass, including defects present in its inputs;
it does not establish a fix for the previous UI diagonals or dark/red title.

Observed translated shader SHA-256:

| Stage | SHA-256 |
| --- | --- |
| VS | `e77b9b0228285837f7b86f0fd8aad7cc5c82c574d20ac43d948212d592ef898f` |
| PS | `a67b801f0d3b45683d796b599781fa0174e55149458cd36f9b982c08e3005a13` |

No guest function/symbol was recovered or hooked in this change. These module
identities are evidence for a bounded adapter contract, not guest addresses.
Original executable/asset data, old renderer binaries and prior captures were
not modified. No game process was launched, no live capture armed and no old
Xbox build deleted.

## New implementation

- `src/render/guest_draw.*`: native adapter using Plume for resource allocation,
  shader compilation, indexed rendering, barriers, per-sample compute readback
  and fence completion. SPIRV-Cross converts the supplied, previously captured
  translated modules to MSL. It does not translate Xbox microcode itself.
- `src/render/guest_packet.cpp`: platform-independent, bounded BPG1 host packet
  decoder. It checks lengths, dimensions, mip extents, index bounds and physical
  address spans. The first schema explicitly represents one fixed UI contract;
  unsupported shader modules, signed sampling and different constant layouts
  are rejected by the GPU adapter.
- `tools/plume_ui_capture.py`: normalizes existing evidence into a packet only
  after checking draw/probe identity, resource transaction identity, observed
  zero input crop, pipeline/sampler eligibility and actual padded index bytes.
  Its comparison checks packet identity and output dimensions before comparing.
- `blur-plume-guest`: standalone offscreen replay CLI and synthetic GPU test.
  No game CPU, kernel or system services are executed.

The original guest vertex physical offset is an explicit offset in a bounded
GPU byte buffer. No guest/native pointer substitution or copied PC RVA is used.
Only the supplied range contains vertex data; the rest of that buffer is
initialized to zero. This conservative first replay uses about 508 MiB for the
captured high physical offset. It is not a performance-optimized live memory
system. The texture input is already linear native BC1 blocks from the capture;
guest tiling/endian conversion and live resource lifetime management still need
to be connected to a verified frontend.

Plume's build-local extension now also supports explicit one-layer 2D arrays.
The translated shaders use stable argument buffer set numbers and explicit
padding for VS/PS constant-block holes. Buffer padding uses SPIRV-Cross's scalar
buffer base types; [its implementation](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/spirv_msl.cpp)
documents the mapping. The tested installed SPIRV-Cross is 1.4.350.1; build/evidence
records include the MSL interface header and linked static-library hashes.

An adapter bug initially allowed the compute output to be copied before its
write dependency was recorded, yielding all-zero readback even for a red-clear
diagnostic. It is corrected by marking the output buffer as a compute writer
before dispatch and transitioning it to copy-read afterward. A separate, fully
synthetic 7x5 test now checks every RGBA sample for both 1x and 4x targets through
the same readback helper. The earlier zero replays and diagnostic reports remain
historical failed observations, not graphics conclusions.

## Reproduce

```sh
cmake -S . -B build-plume -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DBLUR_PLUME_RENDERER=ON -DBLUR_PLUME_GUEST_ADAPTER=ON
cmake --build build-plume -j 4
python3 -m unittest discover -s tests -v
ctest --test-dir build-plume --output-on-failure
./build-plume/blur-plume-guest --self-test
```

Prepare and replay using **new** ignored output directories:

```sh
python3 tools/plume_ui_capture.py prepare \
  local/x360-feasibility/internal-workspaces-20261025/blur-image-ladder-runtime-20261025/run5 \
  local/plume-x360-20261027/packet-new
./build-plume/blur-plume-guest local/plume-x360-20261027/packet-new/ui.bpg \
  local/plume-x360-20261027/replay-new
python3 tools/plume_ui_capture.py compare local/plume-x360-20261027/packet-new \
  local/plume-x360-20261027/replay-new
```

GPU commands require a normal macOS GPU-access context. Existing output
directories are rejected. Generated packets, shaders and pixel data remain
ignored. `--diagnostic-no-cull` and `--diagnostic-clear-red` are isolated replay
controls, always recorded in output metadata; diagnostic runs cannot pass the
normal exact-match comparison gate.

Validation: 103 Python tests passed with one existing skip; ARM64 CMake build
passed; all six standard CTest checks passed; real Apple M3 synthetic 1x/4x
readback passed; final replay006 comparison passed exactly. The original native
texture rendering GPU checks also remain available separately.

## Remaining work and cleanup gate

The actual Xbox host still selects `xenos/metal`. This library has not replaced
its graphics system, command processor or presenter. The next integration must
provide current draw state, actual resource contents and lifetimes, correctly
packed constant blocks, supported shader variants and synchronization. The
fixed UI contract requires a frontend eligibility check on every relevant state
field; matching a shader hash alone does not establish eligibility. Unsupported
work must be visible in coverage/failure reporting, never fabricated success.

Expand verified coverage to a complete menu pass chain and then connect it in a
separate Xbox candidate. The required user milestone is an interactive menu,
followed by loading an offline event and a controllable race. A successful UI
replay or title frame is not that milestone. Keep the existing Xbox builds and
diagnostics until the new path meets those gates.

After the replacement is verified, inventory dependency use, preserve a runnable
baseline and reproducible source/build pins, retain useful capture/diagnostic
evidence, and remove only obsolete rebuildable products and verified redundant
copies. Preserve original game inputs, saves, unique evidence and shared live
dependencies. Record every removed path and reclaimed bytes. This cleanup is
authorized for that later stage; no deletion was performed in this checkpoint.
