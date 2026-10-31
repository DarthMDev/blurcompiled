# Tools

| Tool | Purpose |
| --- | --- |
| `setup_rexglue.py` | Verify and install the pinned public Apple Silicon SDK; no game download |
| `prepare_xbox_game.py` | Verify a user-owned Xbox executable; prepare a local host and optionally generate C++ |
| `setup_plume.py` | Fetch and verify the pinned Plume subset |
| `prepare_plume.py` | Prepare Plume sources for native Metal support targets |
| `embed_native_shaders.py` | Embed project-native Metal source |
| `plume_bridge_host.cpp` | Standalone bridge validation host |
| `plume_guest_replay.cpp` | Local Xbox packet replay |
| `plume_ui_capture.py` | Normalize and compare bounded local captures |
| `build_plume_state_candidate.py` | Maintainer-only instrumented SDK candidate builder; not the public game setup path |

See [game build instructions](../docs/BUILD_GAME.md) and
[development setup](../docs/DEVELOPMENT.md). All game inputs, generated code,
shader fixtures, captures and build outputs stay ignored and local.
