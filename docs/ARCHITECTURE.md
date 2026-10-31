# Architecture decision 001: reconstruct behavior behind native interfaces

Start with readable C++ reconstruction of narrow, verified portions of the PC
game. Decompiler output is evidence, not automatically portable source. Evaluate
binary lifting with a bounded experiment. A pinned recomp-kit experiment now
passes one original-instruction CRC comparison and strict whole-image translation.
Its ARM64 headless host executes startup but aborts on missing runtime support;
this does not establish working gameplay. See [RECOMP_EXPERIMENT.md](RECOMP_EXPERIMENT.md).

Keep three layers as the code grows:

1. **Recovered game behavior:** resource decoding, vehicle/game rules, state
   machines and wire formats. No SDL, Objective-C, Win32, or host-pointer values
   in persisted formats. Fixed-width integers and explicit endian reads.
2. **Engine services:** asset access, rendering commands, audio, input, time,
   threading, filesystem, network transport, and presence events.
3. **Platform implementations:** SDL3 window/input/audio support; Metal on Apple
   platforms; a later Linux graphics backend. Amax and Discord are independent
   services consuming game state, not patches to native instruction addresses.

A bounded native asset module now reads v2 archives, decodes payloads and maps
one DDS volume into RGBA slices; the host uploads that atlas through SDL/Metal.
Split further modules when working slices make their interfaces concrete; avoid
empty classes that imply implemented systems. See NATIVE_ASSET_SLICE.md for limits.

The native target has 64-bit pointers. PC structures with 32-bit pointers need
explicit decoding and relocation; do not reinterpret a file buffer as a native
C++ structure. Preserve original integer overflow, alignment and floating-point
behavior where gameplay or multiplayer depends on them. Use a fixed simulation
step with rendering decoupled only after recovering the original time model.

For a first real 3D scene, select direct Metal or SDL_GPU after auditing shaders,
render targets, depth conventions, texture formats and resource lifetimes.
The current SDL 2D Metal renderer only proves the host toolchain/window works.

Linux should share the core, asset readers, SDL input/audio, and protocol code.
Phones add memory/thermal budgets, touch controls, lifecycle suspension, and
platform-specific networking/presence constraints; mobile capability is unproven.
