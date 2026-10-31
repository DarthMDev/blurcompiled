# Graphics: help wanted

The local Xbox 360 recomp reaches the title/menu, but the car/ground are dark/red.
The full displayed frame currently uses the SDK Metal backend. Plume supports
bounded rendering slices; full resource synchronization and frame handoff remain
incomplete. A complete offline race has not been validated.

## Verified investigations

- Resolve synchronization alone did not fix the scene.
- Shared monotonically increasing bridge submission IDs fixed repeated UI
  submission failures; hundreds of UI draws now succeed.
- A SPIR-V texture exponent correction was exercised on the experimental
  SPIRV-Cross path; the scene remained dark/red.
- A bounded 320→1280 source-pitch override removed resolve mismatch warnings;
  the scene remained dark/red. The override stays opt-in.
- Plume HDR composite remains deferred without a synchronized scene preload;
  live Plume MSAA and terminal resolve handoff are incomplete.

These are observations, not proof that the untested parts are correct. The next
useful probe is scene HDR color/format before and after resolve and compositing.
Avoid promoting symptom-suppressing overrides without a correct reference.

## Source map

- `recomp/native/rex_plume_state.*`: Xbox register state mapping.
- `recomp/native/rex_plume_constants.*`: shader constant packing.
- `recomp/native/rex_plume_draw.*`: draw eligibility and packet assembly.
- `recomp/native/rex_plume_texture.*`: guest texture ranges and decoding.
- `recomp/native/rex_plume_active.hpp`: active submission and resolve hooks.
- `src/render/guest_draw.cpp`: bounded native rendering.
- `src/render/plume_bridge.*`: versioned host API and submission validation.
- `recomp/metal/`: public SDK patch, dependency pins and local shader recipe.
- `tools/instrument_metal.py`: shared live-hook source transformation.
- `tools/build_plume_state_candidate.py`: legacy maintainer experiment builder.

Reproduce this issue with the [native Metal + Plume build](BUILD_GAME.md#native-metal--plume-game-build).
It now builds the SDK and active hooks from source with your own game inputs.
The stock public SDK Vulkan/MoltenVK path is a separate comparison configuration
and shows different artifacts. Plume support tests and accepted UI submissions
do not establish that Plume produced the complete game frame. Start with measured intermediate data before changing formats or
turning an experimental override on by default.

For a graphics PR, identify the exact backend and input hash, show the original
and resulting scene, explain the resource/shader state involved, and include a
focused synthetic check where appropriate. Captures and game shader bytecode
stay local; share sanitized conclusions and reproduction instructions.
