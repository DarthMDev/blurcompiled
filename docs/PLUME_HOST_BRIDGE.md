# Native host bridge checkpoint

October 27, 2026. **The loadable Plume UI bridge and native packet builder are
verified. They are not yet connected to the live Xbox command processor.**
The game still selects its existing backend, and an interactive menu/offline
race through Plume remains unverified. Older Xbox builds and useful diagnostics
have not been deleted.

Update: [the pinned SDK register mapper and isolated observer candidate are built](PLUME_REX_FRONTEND.md).
The observer's live windowed validation is pending launch approval.

This is a reasonable point to switch from High to **Sol Medium** for the next
bounded frontend tasks. That is a task-readiness judgment, not a claim that the
full renderer replacement is complete.

## Implemented and observed

`src/render/plume_bridge.h` defines a versioned C API for a native host to load
`libblur-plume-bridge.dylib` with local symbol scope. No Plume or SPIRV-Cross C++
object crosses the library boundary. The actual exported-symbol check shows
only `blur_plume_get_api`. The example host links only the system C/C++ runtime;
it dynamically obtains the API table rather than linking the renderer.

The bridge verifies ABI/structure sizes, lazily creates the GPU renderer, checks
CPU packet eligibility before GPU work, enforces the creating thread, and returns
separate invalid-input, unsupported, ownership, GPU and state errors. Exceptions
are caught at the API boundary. A GPU failure faults that context so it cannot
silently continue. Successful output is fence-complete RGBA8 sample data; its
borrowed pointer expires at the next owner-thread submission or destruction.
The caller must copy it first. Accepted/rejected/GPU-failure counters are exposed;
host frame/draw IDs are monotonic submission identifiers, not guest game events.

`src/render/guest_frontend.*` accepts borrowed native resource views and explicit
current UI state. It rejects unknown/unsupported topology, format, index path,
depth/stencil, coverage, blend, face, sampler and view state. It checks resource
bounds before allocating copies. It does not assume CPU RAM contains the current
GPU resource: the SDK frontend must establish synchronization before supplying
GPU-written data. `encode(Packet)` then emits the portable BPG1 byte stream in
C++; Python is not required by the live producer.

Measured on Apple M3 with the final build:

- The example host dynamically loaded the bridge and submitted the real UI
  packet twice through one context; both complete 10,485,760-byte outputs were
  identical. A stale submission was rejected and exposed no successful view.
- The caller copied the result, destroyed the context, and saved that independent
  copy. Counters were accepted=2, rejected=1, GPU failures=0.
- The final output still matches the captured reference crop exactly: all
  327,680 pixels, four sample slots and all RGBA channels; maximum error zero.
- The native frontend rebuilt the real packet byte-for-byte from borrowed
  resource views. This test supplies the fixed BPG1 state contract; it is not
  evidence that current Xbox SDK state has been mapped.
- ARM64 CMake build, C header compilation/layout checks, nine CTest checks and
  103 Python tests passed (one existing Python skip). Synthetic tests cover
  state eligibility, copied resource ownership, bounds, ABI negotiation,
  unsupported packets, wrong-thread calls and shutdown without GPU initialization.

The external drive disconnected during result finalization. After it remounted,
the project rebuilt and tests passed again. The earlier readback was rechecked,
and fresh `host-002` output verified the rebuilt bridge. No failure of the old
game renderer was inferred from the drive interruption.

## Reproduce

```sh
cmake -S . -B build-plume -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DBLUR_PLUME_RENDERER=ON -DBLUR_PLUME_GUEST_ADAPTER=ON
cmake --build build-plume -j 4
python3 -m unittest discover -s tests -v
ctest --test-dir build-plume --output-on-failure
./build-plume/blur-guest-frontend-tests local/plume-x360-20261027/packet/ui.bpg
./build-plume/blur-plume-bridge-host ./build-plume/libblur-plume-bridge.dylib \
  local/plume-x360-20261027/packet/ui.bpg local/plume-bridge-20261027/host-new
python3 tools/plume_ui_capture.py compare local/plume-x360-20261027/packet \
  local/plume-bridge-20261027/host-new
nm -gU build-plume/libblur-plume-bridge.dylib
```

The GPU host command needs normal macOS GPU access and a new output directory.
It does not execute a game CPU, launch a game, arm a capture or modify original
assets/saves. Generated packets, pixels, shader sources and checkpoints remain
in ignored local directories. `local/plume-bridge-20261027` holds the verified
output and source/build records.

## Next frontend work

1. In a separate Xbox candidate, map **current** SDK draw state into `UiState` and
   supply the corresponding current resource views. Do not copy the test-only
   `eligible()` state factory into runtime code. Start with eligibility/coverage
   reporting; reject unknowns visibly.
2. Confirm the current translated modules and constant packing match the supported
   contract before submitting. The existing Metal `IssueDrawMsl` path has
   translated modules and packed float constants, but the default MSC/DXBC path
   is a different contract. Do not cast or rename one path into the other.
3. Obtain actual texture/index/vertex bytes with established ownership and GPU
   completion. Feed `make_ui_packet`, `encode`, `probe`, then `submit` on the GPU
   owner thread. The example host demonstrates ABI/lifetime use, not a live hook.
4. The only advertised capability is a **zero-initialized UI pass**. It does not
   support loading an existing render target, persistent pass chains, other
   shader pairs, depth/stencil, full-screen HDR composition or presentation.
   Those must be implemented and verified before this can replace the complete
   game backend. Matching a shader pair alone is insufficient eligibility.

Keep the prior Xbox build as the reference until the complete new rendering path
is verified. Deferred cleanup remains authorized after replacement: retain a
runnable baseline, unique captures/diagnostics, original inputs, saves and shared
dependencies; remove only obsolete/redundant products with a recorded inventory.

No new guest function was recovered or hooked in this checkpoint. The bridge,
packet format and frontend types are new native implementations. See
[the captured UI pass](PLUME_X360_ADAPTER.md) for its observed shader identities
and comparison limits.
