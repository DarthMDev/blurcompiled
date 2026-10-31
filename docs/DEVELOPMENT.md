# Development setup

## Support targets

Requirements: CMake 3.24+, C++20 compiler, Python 3, SDL3 3.2+ and zlib.
The baseline commands are in the [README](../README.md).

## Plume on macOS

An Apple Silicon Mac and Metal development tools are needed for the current
integration. Install the pinned Plume subset, then configure:

```sh
python3 tools/setup_plume.py
cmake -S . -B build-plume -DBLUR_PLUME_RENDERER=ON
cmake --build build-plume --parallel
ctest --test-dir build-plume --output-on-failure
```

`setup_plume.py` downloads a hash-pinned upstream archive into ignored `local/`.
These commands build diagnostic hosts and libraries, not the complete game.
Inspect CMake options before enabling legacy experiments.

## Working game candidate: remaining reproducibility gap

`tools/build_plume_state_candidate.py` currently reads recorded compiler/linker
commands and verified objects from a developer-local ReXGlue workspace. It does
not fetch or regenerate that workspace. The local game candidate is evidence of
launch progress, not a fresh-clone build recipe. Contributors should start with
the support targets unless they are specifically working on the setup pipeline.

Needed: pin and fetch the SDK source, document dependency setup, verify the
user-supplied Xbox executable, run translation locally, and generate a portable
build graph. No generated game code or original binaries should be committed.

## Layout

- `src/`: support code, assets/parsers and rendering interfaces.
- `recomp/native/`: native runtime/Plume adapters and diagnostic policies.
- `recomp/`: dependency pins and earlier recomp configuration.
- `tests/`: synthetic fixtures and support-target tests.
- `tools/`: current tools plus retained historical PC experiments; see its index.
- `docs/`: current contributor guides and detailed research records.
- `local/`, `reports/`, `build*/`: ignored development data and build outputs.

The live frontend additionally requires `BLUR_REX_PLUME_FRONTEND=ON`, a pinned
local SDK, and a locally generated `recomp/native/rex_plume_embedded_shaders.hpp`
fixture from your own game inputs. That header contains captured game shader
payloads and is excluded from Git. Existing local copies are preserved. A public
fixture-generation recipe is still needed; a fresh clone should leave the live
frontend option OFF. Do not blindly stage the research workspace.
