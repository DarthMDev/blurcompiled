# Tool guide

## Current renderer and contributor tools

| Tool | Purpose |
| --- | --- |
| `setup_plume.py` | Fetch and verify the pinned Plume subset into ignored local storage |
| `prepare_plume.py` | Prepare that subset for the CMake Metal integration |
| `embed_native_shaders.py` | Embed project-native Metal source for support targets |
| `plume_bridge_host.cpp` | Standalone bridge validation host |
| `plume_guest_replay.cpp` | Local packet replay diagnostic |
| `plume_ui_capture.py` | Local capture inspection; generated payloads are not publication inputs |
| `build_plume_state_candidate.py` | Build an isolated candidate from an existing private SDK workspace; not a portable game build |

## Historical PC tools

`recomp_*.py`, `recomp_slice_host.c`, `patch_unmodelled_sse.py`, `ghidra/`,
`inspect_install.py`, `inspect_pak.py`, `read_pak_resource.py`,
`native_asset_probe.cpp`, `verify_native_assets.py`, `verify_recomp_generation.py`
and `play-intro.command` support earlier PC research or asset experiments.
They are retained because tests/build configurations still reference them.
They are not the Xbox 360 game setup pipeline. Do not use PC offsets or binaries
as interchangeable Xbox inputs.

Run tools with `--help` and inspect their inputs first. Keep outputs in ignored
`local/` or `reports/`. Never commit original game files, captures, generated game
translations, decompiler output, credentials or user data.
