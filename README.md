# BlurCompiled

An experimental native recompilation of the **Xbox 360 version of Blur**, using
ReXGlue for the guest runtime and **Plume** for the native graphics integration.
The current development platform is **macOS on Apple Silicon**.

**The local recomp launches and runs. Graphics still need fixing.** The title/menu
has persistent dark/red scene rendering. Complete race correctness and stability
have not been established. This is a development project, not a finished release.

You must supply your own game files. Do not submit game executables, extracted
assets, generated game translations, or captured game shader binaries.

[Contributing](CONTRIBUTING.md) · [Milestones](docs/ROADMAP.md) ·
[Build and architecture](docs/DEVELOPMENT.md) · [Graphics help wanted](docs/GRAPHICS.md)

## What works, and what needs help

| Area | Current state |
| --- | --- |
| Xbox 360 recomp runtime | Launches and reaches the title/menu in the local development setup |
| macOS / Apple Silicon | Current development and validation platform |
| Plume | Verified bounded UI/composite rendering slices and live bridge integration; full scene/resource/resolve handoff incomplete |
| Graphics | Dark/red scene remains; highest priority |
| Windows and Linux | Ports needed; contributors welcome |
| Online multiplayer | Not implemented or verified |
| Discord Rich Presence | Planned; no live integration |
| Public game build | Reproducible setup from user-supplied game files still needed |

The current displayed game frame uses the SDK Metal backend. Plume is being
integrated incrementally; it does not yet render the complete game frame.
The older PC binary reconstruction is historical work, not the current target.

## Help wanted

We especially welcome contributors with Xenos GPU, Metal, shader translation,
render-target resolve, HDR/color-format, Windows, and Linux experience. Start with
[the graphics investigation](docs/GRAPHICS.md), then propose a small change with
an observable before/after result. Porting, reproducible builds, documentation,
online-service research, and Discord integration are also useful contributions.

## Building from source

The checked-in CMake project builds support libraries, diagnostic hosts and tests.
It **does not yet build the complete game from a fresh clone**. The working game
candidate currently depends on an ignored local ReXGlue workspace and generated
translation outputs. Making that pipeline reproducible is an explicit milestone.

For the baseline support targets, install CMake 3.24+, a C++20 compiler, Python 3,
SDL3 3.2+, and zlib, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
python3 -m unittest discover -s tests -v
ctest --test-dir build --output-on-failure
```

See [development setup](docs/DEVELOPMENT.md) for the optional macOS Plume targets
and the remaining local-workspace limitations. No ready-to-play download is
provided here.

## Milestones

- [x] Launch the Xbox 360 recomp in the local Apple Silicon development setup.
- [x] Validate bounded native Plume rendering and bridge slices.
- [ ] Fix scene graphics and verify render-target/shader correctness.
- [ ] Complete Plume scene, resource, composite and resolve integration.
- [ ] Publish a reproducible game build using user-supplied inputs.
- [ ] Validate an offline race, controls, audio, saves and stability.
- [ ] Bring up Windows and Linux builds and platform CI.
- [ ] Research and implement online multiplayer compatibility.
- [ ] Add opt-in Discord Rich Presence.

Acceptance criteria and dependencies are in [the roadmap](docs/ROADMAP.md).

## Online and presence

Online support is future work. We need to establish which services the Xbox 360
version actually uses before choosing an implementation. Amax and an Xbox Live
360 replacement/private server are research directions, not working integrations.
[LibreLive](https://librelive.net/) advertises Xbox 360 replacement services;
compatibility with Blur and this recomp has **not** been verified. Redirecting
an endpoint alone does not implement authentication or matchmaking.

Discord Rich Presence is a separate planned integration. It should be optional,
respect privacy, and work without requiring a multiplayer connection.

## Project notes and credits

[Documentation index](docs/README.md) · [Tool guide](tools/README.md) ·
[Publishing and development trails](docs/PUBLISHING.md)

Built around the work of the ReXGlue, Xenia and Plume communities. Dependency pins
and their upstream locations are recorded under `recomp/`. Their licenses remain
applicable. Original project code is licensed under **GPL-3.0-or-later**. See [LICENSE](LICENSE)
and [third-party notices](THIRD_PARTY_NOTICES.md); upstream code retains its own license.

Blur and Xbox are trademarks of their respective owners. This is an independent
community project with no affiliation or endorsement.
