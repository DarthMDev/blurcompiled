# Development

The source tree targets the Xbox 360 recomp and Plume rendering integration.
For user-owned game inputs and executable generation, see [BUILD_GAME.md](BUILD_GAME.md).

## Portable contracts

CMake 3.24+, a C++20 compiler and Python 3 are sufficient for the CPU packet and
frontend tests. Use `-DBLUR_PLUME_RENDERER=OFF`; neither a game nor an SDK is needed.
Run `python3 -m unittest discover -s tests -v` and CMake/CTest after changes.

## Plume / Metal support targets

On Apple Silicon, install SDL3 3.2+ and SPIRV-Cross (including its MSL, GLSL and
core libraries), then run:

```sh
python3 tools/setup_plume.py
cmake -S . -B build-plume -DBLUR_PLUME_RENDERER=ON -DBLUR_PLUME_GUEST_ADAPTER=ON
cmake --build build-plume --parallel
ctest --test-dir build-plume --output-on-failure
```

These targets build the native renderer, guest packet contracts, replay and
bridge hosts. They do not replace the entire game's graphics backend.
`BLUR_REX_PLUME_FRONTEND=ON` additionally needs the pinned SDK source and a locally
generated `recomp/native/rex_plume_embedded_shaders.hpp`. That captured game
fixture is ignored. Until its input-generation recipe is complete, leave the
live frontend option OFF in a fresh clone.

## Source layout

- `src/render/`: Plume renderer, packet contracts, replay interfaces and bridge.
- `recomp/native/`: Xbox register/constant/resource adapters and diagnostic policy.
- `recomp/xbox/`: game build inputs, analysis overrides and native host template.
- `recomp/*-pin.json`: reproducible dependency and SDK pins.
- `tests/`: synthetic Xbox preparation, capture normalization and C++ contracts.
- `tools/`: SDK setup, user-input preparation and graphics diagnostics.
- `local/`, `reports/`, `build*/`: ignored inputs, generated material and outputs.

The maintainer's `build_plume_state_candidate.py` still consumes an existing
instrumented SDK workspace. It is a graphics experiment tool, not the new public
SDK game-build pipeline. The public pipeline must not depend on saved object
files or previously generated translations.

Only `.codex/hooks.json` is shared as an optional hook template. Other Codex
state and all Entire settings/session data are ignored. Contributors review and
approve hooks themselves; the template is not required to build or run the game. On the original external volume, Git directory metadata is unreliable; use a
normal local filesystem for contributor checkouts.

## Why the tests remain

| Location | What it protects |
| --- | --- |
| `tests/test_prepare_xbox_game.py` | Input hash checks, separate output workspace, preserving original game files |
| `tests/test_setup_rexglue.py` | SDK archive digest and safe extraction |
| `tests/test_plume_ui_capture.py` | Synthetic capture normalization and completeness checks |
| `tests/render/packet_tests.cpp`, `frontend_tests.cpp` | Packet format and portable frontend contracts |
| `tests/render/renderer_tests.cpp` | Renderer state, resource lifecycle and optional real GPU checks |
| `tests/render/bridge_tests.cpp`, `bridge_header_test.c` | C ABI, validation and fault isolation |
| `tests/xbox/` | Optional SDK register, constant, draw, texture and active-submission integration |

The Xbox-specific C++ files cover different boundaries, not five copies of the
same test. They stay separate so a regression points to the failing subsystem.
The test count is smaller because unrelated experiments were removed, not
because failing checks were disabled. Pure-Python fixtures need no game or SDK.
