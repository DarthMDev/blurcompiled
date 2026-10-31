# Plume native renderer checkpoint

October 27, 2026. **New implementation: verified native rendering foundation.
The Xbox game has not been switched to Plume.** No gameplay or graphics fix is
claimed by this checkpoint. The PC asset viewer and existing Xbox Vulkan/Metal
builds remain the comparison paths.

## Implemented

`blur-render` is a C++20 renderer using Plume's Metal backend directly. It owns
textures, samplers, argument-buffer descriptor sets, indexed vertex/index uploads,
copy/alpha pipelines, targets, command lists, fences and window presentation.
It accepts explicit host RGBA8 textures and indexed textured triangles, with
top-left pixel scissors and clip-space vertices. There is no SDL renderer,
Vulkan, MoltenVK or Xenos command processing in this path. SDL3 supplies the
standalone validation window and its CAMetalLayer only.

Offscreen results are tightly packed RGBA8, copied after GPU fence completion.
Upload and readback row pitches are padded independently of source dimensions.
Frames are synchronous and resource handles are validated before recording;
destroyed IDs, invalid indices/scissors, nonfinite vertices and excessive
dimensions/geometry fail explicitly. This is a bounded 2D rendering slice:
single-sample UNORM output, one mip level, copy or straight-alpha blending,
nearest/linear sampling, no depth/stencil or Xbox shader execution.

Plume is pinned to
[`0ce184b19270e49236afbb74052a6b267494b86b`](https://github.com/renderbag/plume/tree/0ce184b19270e49236afbb74052a6b267494b86b).
`recomp/plume-pin.json` records the upstream archive SHA-256 and every consumed
source/header/license hash. CMake checks those files before building. Dependency
installation is separate from configuration; no Git or network is invoked by
CMake. Metal-only compilation avoids Plume's unused Vulkan/D3D dependencies.

`tools/prepare_plume.py` applies a small, explicit extension in the **build
directory**: a separate `MSL` shader format using Apple's runtime compiler,
without fast math, creation/submission failure propagation, deterministic
zero-initialized depth bias, synchronous present callback completion and a
dispatch-data release. The upstream checkout
stays unchanged; the normal `METAL` format still
accepts compiled libraries. This accommodates the missing offline Metal compiler
component on this machine. Native MSL is embedded in the executable and does
not depend on the working directory. Changes to the pinned extension context
fail configuration instead of silently applying to a different revision.

## Reproduce

```sh
python3 tools/setup_plume.py
cmake -S . -B build-plume -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DBLUR_PLUME_RENDERER=ON
cmake --build build-plume -j 4
python3 -m unittest discover -s tests -v
ctest --test-dir build-plume --output-on-failure
./build-plume/blur-render-tests
./build-plume/blur-plume --frames 5
```

An already downloaded, hash-checked archive can be supplied to setup with
`--archive PATH`. `BLUR_PLUME_SOURCE_DIR` accepts an alternative installation of
the same pinned files. GPU tests need a normal macOS GPU-access context; the
sandbox reports no available Metal device and fails explicitly. Standard CTest
contains the CPU contract test and CLI check. Enable `BLUR_PLUME_GPU_TESTS` only
where GPU access is available.

`blur-plume` is an explicitly labeled synthetic validation host. It accepts
`--rgba PATH --width N --height N` to inspect an already linear host image,
read-only. This is image presentation, not Xbox texture-layout decoding or
game rendering. An unbounded run exits on close/Escape; `--frames` applies to
this standalone validation tool, not to the Xbox game.

## Observed validation

- ARM64 Debug CMake build passed with AppleClang 21.
- Required Python discovery: 96 tests, one existing skip, passed.
- Plume CTest: four tests passed, including native-assets and render contracts.
- Real Apple M3 offscreen GPU tests passed: every pixel of asymmetric corner
  texture draws, odd-pitch 67x19 upload/readback, target resize and reupload;
  alpha blend/scissor checks, two-draw attachment preservation, linear sampling,
  resource destruction and continued rendering after rejected submissions.
- Standalone native window presented five synthetic frames and exited zero.

These are measured host tests. They do not prove fidelity to Blur's guest
rendering, address the observed dark/red Xbox title output, or measure FPS.
No game was launched or capture armed for this work. Original input, saved
captures, user data and prior native binaries were not changed.

The old `build/` cache still enables `BLUR_RECOMP_STARTUP_TESTS`; that optional
target cannot rebuild because its local `recomp-kit/runtime/imports.h` is absent
after prior cleanup. This dependency gap is independent of Plume. The fresh
Plume build does not depend on recomp-kit.
The separate fresh standard build (including the movie viewer) and all three
of its CTest checks also passed.

## Xbox integration remaining

Update: [the first captured Xbox UI pass now renders exactly through Plume](PLUME_X360_ADAPTER.md).
Live integration remains incomplete.

The actual Xbox host selects its GPU plugin in `BlurX360App::OnPreSetup` in the
ignored Metal candidate project. It still selects `xenos/metal`. Plume cannot
be selected there by renaming the backend: it supplies GPU operations, not a
ReXGlue graphics system, PM4/Xenos implementation or Blur-specific D3D hooks.

Latest preserved Xbox context is
`local/x360-feasibility/internal-workspaces-20261025/CURRENT_STATUS.md` and the
Metal candidate's `capture-v2` checkpoint. Capture002 is complete and the game
exited zero. It establishes a dark/red Press START title screen, not main-menu
or race correctness. Earlier Vulkan run5 contains the UI atlas/geometry/shaders
needed for a separately verified UI slice.

Next, recover the first selected Blur graphics call/resource/shader contract
against the pinned XEX, with SHA-256/RVA/evidence/confidence for any guest symbol.
Implement a bounded adapter and replay its actual resources/draws offscreen
through Plume; compare the pass output with the corresponding captured stage.
Only then wire that supported slice into a **separate** Xbox candidate, with
explicit rejection/coverage reporting for unsupported work. No guest address,
hook, shader, endian/tiling rule or success return was invented in this change.
The existing backend is required until complete screen passes, resources,
shaders, synchronization and state transitions are supported and validated.

Sol Medium is a reasonable handoff for bounded work on this documented API and
its repeatable tests. The whole Xbox renderer replacement remains substantial
recovery/integration work; the foundation test results are not an acceptance
gate for a completed game-renderer switch.
