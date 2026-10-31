# Roadmap

Milestones describe acceptance criteria, not release dates. Current platform:
macOS / Apple Silicon. Current target: Xbox 360 recomp plus Plume integration.

| Milestone | Completion criteria | State |
| --- | --- | --- |
| M0 — Local launch | Recompiled runtime launches and reaches the title/menu | Achieved locally |
| M1 — Community source baseline | Source/provenance/license audit; clear docs and tools; sanitized Git/Entire publishing | Published; focused Xbox-only cleanup complete |
| M2 — Reproducible game setup | Fresh clone plus user-supplied verified game inputs generates and builds the runtime without a private workspace | Verified on Apple Silicon through fresh translation, native compilation and headless help; gameplay validation pending |
| M3 — Graphics correctness | Explain and fix dark/red scene; compare shaders, HDR intermediates and resolves; complete Plume frame/resource handoff | Highest priority |
| M4 — Offline validation | Complete a race; verify controls, audio, saves and repeated-session stability | Open |
| M5 — Windows | Reproducible build, selected Plume backend, correct graphics/input/audio and CI on supported Windows hardware | Open |
| M6 — Linux | Reproducible build, selected Plume backend, correct graphics/input/audio and CI on supported Linux hardware | Open |
| M7 — Online | Map actual Xbox 360 service calls; select a documented compatible backend; validate sign-in, sessions and race connectivity | Research |
| M8 — Discord Rich Presence | Opt-in integration with privacy settings, reconnect handling and accurate menu/race state | Planned |

Windows and Linux investigation can proceed alongside graphics work. Neither
platform is currently advertised as working. Plume's current checked-in CMake
integration is macOS/Metal-only; portable renderer code alone is insufficient.

For online, evaluate Amax applicability and Xbox Live 360 replacement/private
server options independently. [LibreLive](https://librelive.net/) describes
Xbox 360 services but does not establish Blur recomp compatibility. Record
protocol evidence, licensing, platform support and reproducible interoperability
before choosing it. No authentication or matchmaking implementation is claimed.
