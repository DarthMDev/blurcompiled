# Blur Xbox 360 recompilation feasibility and macOS plan

Date: October 21, 2026. Status: proposed experiment, not a completed pivot.

## Objective and decision

Evaluate whether the Xbox 360 release provides a better foundation for a native
Apple Silicon version of Blur. Target a correctly rendered, interactive offline
menu first, then one complete offline race. Evaluate Amax compatibility separately;
add Discord Rich Presence after stable game-state events exist. “DRP” is assumed
to mean Discord Rich Presence.

The proposed stack is ReXGlue for recompilation/system services, its existing
Xenia-derived graphics path for initial bring-up and comparison, and a custom
Blur renderer using Plume's Metal backend if the experiment justifies it.
Plume is the graphics interface; we must supply Blur's resource, shader and draw
translation. It is not a complete Xbox 360 graphics implementation.

Promote this route only when measured progress and remaining work compare
favorably with the preserved PC route. A translated executable, nonblack splash,
frame counter or successful login alone does not meet that decision gate.
This plan does not promise a completion date or that either route is near a menu.

## Starting evidence

- The PC checkpoint preserves 507 libraries in
  `local/owner-00457d90-20261020/probe/result.json`, with unchanged hashes and host
  SHA-256 `494c0709f528f765155ee2d651b2adba541d775fdfe789fb5b524d3511c88938`.
  It clears the prior initialization failure and instruction trap, then faults at
  guest VA `00512752` / RVA `00112752`. Its pointer provenance remains unresolved;
  menu/racing and complete rendering are unverified. See
  [the handoff](HANDOFF_OWNER_00457D90_2026-10-21.md).
- ReXGlue's upstream build configuration has Apple Silicon macOS and Vulkan
  support; its installed macOS Vulkan dependencies include MoltenVK. This is
  infrastructure evidence, not evidence of Blur compatibility.
  [Build configuration](https://github.com/rexglue/rexglue-sdk/blob/main/CMakeLists.txt),
  [installed dependencies](https://github.com/rexglue/rexglue-sdk/blob/main/cmake/rexglueConfig.cmake.in).
- Plume provides Metal, Vulkan and Direct3D 12 interfaces. Its upstream README
  describes shader compilation as the consumer's responsibility and notes API
  stability limitations. Pin a revision rather than tracking a moving branch.
  [Plume](https://github.com/renderbag/plume).
- Xerenge describes a title-specific renderer that intercepts Direct3D calls,
  uses shaders translated ahead of time, and draws through Plume while retaining
  the Xenia-derived renderer as a reference. It reports that its Plume path is
  faster for Burnout Revenge. Treat that as an architectural example and a
  project report, not a Blur benchmark or reusable Blur implementation.
  [Xerenge](https://github.com/shipa-2/Xerenge).
- XenonRecomp documents function-boundary/jump-table limitations and exception
  limitations. These are risks to audit in the selected toolchain, not proof that
  every ReXGlue revision has identical limitations. Use ReXGlue's supported
  generation pipeline first; evaluate a separate XenonRecomp producer only after
  proving compatible context, calls, imports and generated-code ABI.
  [XenonRecomp](https://github.com/tzsimon/xenonrecomp).

## Architecture

```text
Pinned Xbox 360 executable and assets (read-only local input)
    -> audited PPC generation -> native ARM64 game code
    -> ReXGlue kernel/XAM, memory, threads, files, input and audio
    -> selectable graphics adapter
         A: existing Xenia-derived GPU path / Vulkan / MoltenVK
         B: recovered Blur graphics calls / resources / shaders
              -> Plume / Metal
    -> recovered game-state events
         -> optional native Discord presence
         -> separately validated networking adapter / Amax
```

Keep guest addresses and endianness explicit. Do not reuse PC RVAs, layouts,
shader binaries, hooks, save structures or network assumptions without console
validation. Reuse PC research as leads and existing parsers only where fixtures
prove equivalent formats. Keep SDL/Metal and other platform APIs behind native
platform boundaries.

## Stages and acceptance gates

| Stage | Work | Required evidence before advancing |
| --- | --- | --- |
| 0. Preserve and identify | Pin the PC checkpoint; identify user-supplied Xbox 360 executable, assets and update version. Record title/build identity, hashes, executable mapping, imports and tool revisions. | Reproducible input manifest; original files unchanged; console version unambiguous. |
| 1. Toolchain trial | Build the chosen ReXGlue revision for ARM64 macOS; run its relevant synthetic CPU/runtime tests and a graphics presentation test. Check the supported generator and macOS backend combination. | Native ARM64 build, test results, a rendered host test frame and clean shutdown. Host smoke rendering is not Blur rendering. |
| 2. CPU/system bring-up | Generate Blur code with recorded boundaries/dispatch coverage. Batch verified instruction, import and runtime fixes. Exercise startup, files, memory, threads and input without fabricated success returns. | Repeatable startup path, service/failure inventory, no unexplained missing dispatch on that path, working external timeout and diagnostic output. |
| 3. Reference graphics and pivot review | Run the existing graphics path; capture a real Blur frame, shader/resource inventory and menu interaction if attainable. Diagnose failures by subsystem. | Correctly rendered interactive menu is the promotion target. If blocked, a captured Blur frame/workload may support a renderer experiment, but does not establish menu reach. Publish the go/no-go comparison. |
| 4. Plume/Metal vertical slice | Recover the minimum menu graphics calls, resource lifetimes and shaders. Implement one complete pass chain through Plume/Metal and compare it with the reference. | Matching geometry, text, textures, blending, depth and transitions for the selected screen; input changes visible state; repeatable capture comparison. |
| 5. One offline race | Extend rendering, input, timing, audio, loading and saves for one vehicle/course. Preserve the original time model before optimization. | Start race, drive, exercise powerups/collisions/opponents, finish, show results, save/reload and return to menu without a crash. |
| 6. Amax compatibility | Recover console networking and compare it with the existing PC Amax client. Implement only verified authentication/session and race contracts. | Controlled login, lobby, match start, synchronized race, results, disconnect/reconnect with compatible peers. Each transition separately verified. |
| 7. Discord and packaging | Drive native presence from confirmed menu/lobby/race events; package a relocatable macOS application. | Correct state transitions and shutdown; absent Discord does not block play; fresh user-supplied data setup and offline race pass. |

### Stage 0: prerequisites and isolation

Implementation needs a user-supplied Xbox 360 executable and matching assets.
No console dump has been validated in this project. Record disc/region/update
identity before choosing offsets or tool configuration. Do not mix retail and
updated executables or PC and console asset sets.

Preserve the PC hosts, all 507 libraries, latest input list, source/build records
and compact failure evidence. No PC source conversion is required for this plan.
Use an isolated Xbox experiment directory, proposed as `local/x360-feasibility/`,
with original inputs, generated code, builds and captures ignored. Put reusable
source and documented recovery decisions in tracked source only when concrete.
A separately requested repository/worktree can be chosen at implementation time.

Record executable SHA-256, VA and RVA (from the verified image base), byte evidence
and confidence for each recovered symbol. Hash generated output and dependency
revisions, compiler flags and final host binary. Record any required SDK changes
as small patches rather than copying another game's runtime wholesale.

### Stages 1–3: decide whether the pivot earns further work

Inventory Blur's kernel/XAM imports, exception use, computed branches, threading,
TLS, endian conversions and vector instructions before claiming complete CPU
coverage. Test touched translation forms against original-instruction behavior
where a reliable harness exists. Do not hide missing behavior with successful
stubs, blanket invalid-instruction skips, skipped initialization or silent reads.

For each graphics run collect draw/present counts, rejection reasons, shader
translation/binding status, render-target checksums, guest stage events and
termination reason. Distinguish a clear, movie, loading screen, title screen and
interactive menu. Use a console/reference capture or a validated emulator capture
for comparison; an emulator output is not automatically ground truth.

Review after the first build and at most three combined game probes. This is a
review checkpoint, not a three-run success promise. Continue only with a concrete
blocker, a small testable next slice and credible reuse of existing infrastructure.
If it needs broad kernel, exception or GPU reconstruction before a useful frame,
record that cost and compare with the PC route before authorizing another large
work package. A failed MoltenVK path does not by itself disprove the console
route; an independently captured workload can test Metal feasibility.

The pivot decision report must compare observed menu progress, unresolved runtime
contracts, graphics coverage, build/probe cost, and Amax compatibility risk. Prior
PC effort is not a reason to select either route. No evidence yet establishes
that the Xbox path will be faster to complete.

### Stages 4–5: custom graphics without losing a reference

Locate stable console graphics boundaries and verify calling conventions before
intercepting them. Inventory buffers, vertex declarations, index formats, tiled
and compressed textures, mip levels, sampler state, shader constants, render-target
and depth formats, resolves and resource synchronization. Check for Xenos-specific
operations such as EDRAM resolves, predication or memexport when encountered;
do not assume Blur uses or avoids them.

Translate the actual console shader programs through a tested pipeline to Metal
compatible output. Plume does not supply this translation. Preserve shader IDs,
inputs, constants and outputs in comparison evidence. Start with a menu pass,
then one car/track scene; add uncommon effects from observed needs. Keep the
reference graphics path selectable and working when practical. If it cannot run
on the target Mac, retain captured reference workloads and comparisons elsewhere.

Keep game simulation unchanged while bringing up rendering. Compare equivalent
scenes, resolution, effects, timing and frame pacing. Measure CPU/GPU frame time,
median and tail latency, memory and shader stutter. Count real rendered/presented
frames and verify game speed. A higher frame counter or different quality level
does not prove Plume faster. Keep graphics worker parallelism out until ownership
and synchronization are proven.

### Stage 6: Amax is a separate compatibility gate

Perform a cheap console/PC networking comparison during feasibility so this risk
is visible before committing to the pivot. Full integration follows offline play.
Audit platform identity, authentication, session framing, lobby discovery, NAT,
matchmaking and gameplay synchronization; do not infer console compatibility
from shared hostnames or Demonware branding.

The local PC installation has an authentication plugin as well as redirects.
Endpoint replacement alone is insufficient evidence of compatibility. Windows
plugins cannot simply load into native ARM64 macOS. Recover contracts through
source where available and controlled reference behavior; use synthetic fixtures
and keep credentials and captures outside tracked source. See [AMAX.md](AMAX.md).

First determine whether Amax accepts the console client protocol at all. If it
does not, estimate a verified native compatibility adapter separately. Do not
invent messages or promise PC crossplay. A console-only network path, if found,
is a different deliverable. Cross-platform racing requires demonstrated compatible
session and simulation behavior. No server contacts or messages to maintainers
are authorized by this planning document.

### Stage 7: Discord Rich Presence

Recover the desired state labels, images, timing and party information from the
existing PC evidence, then map confirmed console events to native presence.
Validate application/assets availability at implementation time. Treat join
invitations as a separate network-backed feature; display-only presence does not
establish working join or multiplayer. Keep account/network identifiers out of
public presence. Presence must not control simulation or startup success.

## Build, probe, storage and reasoning discipline

- Cache pinned SDK builds and shared archives. Regenerate/rebuild only affected
  code where the toolchain supports it. Avoid cloning full source/build trees for
  every diagnostic change.
- Audit a coherent batch, run meaningful synthetic checks, compile once, validate
  dispatch conflicts, then run one combined probe. Always retain the full latest
  accepted module list if the chosen runtime uses modules. Never carry PC modules
  into the console address space.
- Give each probe a fresh directory, independent wall-clock timeout, bounded logs
  and sparse frame captures. Preserve inputs before and after. Checkpoint CPU,
  graphics and termination evidence together so one run answers several questions.
- Run this repository's required Python suite and CMake build after code changes,
  plus relevant upstream/native suites. Report known failures explicitly. Planning
  text alone does not require rebuilding the existing PC host.
- Track disk use before large builds. After a milestone, prune only reproducible
  stale outputs and redundant captures with a recorded retention list. Preserve
  originals, latest usable builds, manifests, logs and representative failures.
- For ambiguous ownership, shader semantics, lifetime or protocol diagnosis,
  prepare one compact evidence packet for the user's manual DeepSeek chat:
  DeepThink on, Search off. Implement conclusions only after local verification.
  Do not repeat deep reasoning or expensive probes without a new discriminator.

## First implementation work package

1. Validate the console input location/version and preserve the PC checkpoint.
2. Pin and inspect one ReXGlue revision and compatible dependencies; build and
   test ARM64 macOS CPU/runtime plus graphics presentation.
3. Generate a Blur import/function inventory, identifying the first complete boot
   slice and its missing contracts.
4. Run the first bounded combined startup probe; record the actual blocker.
5. Deliver a feasibility report with the next batch and the decision gates above.

This work package produces evidence for the pivot. It does not include a full
custom renderer, multiplayer implementation or a promised playable build.
