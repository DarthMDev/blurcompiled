# Graphics: status and investigations

The local Xbox 360 recomp reaches the title/menu, with the 3D car geometry, wheels,
wet ground reflections, background architecture, and UI now rendering cleanly.
The full displayed frame currently uses the SDK Metal backend. Plume supports
bounded rendering slices; full resource synchronization and frame handoff remain
active work items.

## Verified investigations & root cause fixes

- **EDRAM ownership clobbering resolved**: Blur splits 720p 4x MSAA rendering into
  3 vertical slices. In depth-only clear draws, `RenderTargetCache::Update()` wrapped
  around EDRAM, reassigning base 0 from color target `00D10000` to depth target `00710400`.
  Subsequent color resolves dumped depth data into EDRAM, yielding zero-filled intermediate
  HDR buffers. Fixed by capping single-target update extents to `(kEdramTileCount - rt_base)`
  and filtering resolve dump rectangles by target type.
- **SPIR-V texture result exponent bias corrected**: In `spirv_translator_fetch.cpp`,
  exponent adjustment was mistakenly read from word 4 (bits 13:18), overlapping `lod_bias`.
  Textures specifying negative LOD bias (such as cubemaps and ambient textures) were
  attenuated by $2^{-8}$ to $2^{-31}$. Fixed by loading word 3 bits 13:18 matching DXBC.
- **Resolve pitch clamping resolved**: Fullscreen resolves following quarter-resolution
  passes retained surface pitch 320, causing `draw.cpp` to clamp resolve width to 320 px.
  Fixed by promoting surface pitch to destination copy pitch for 1X fullscreen resolves.
- **HDR buffer validation**: Inspection of raw 64bpp float intermediate `038_0DDAE000.bin`
  confirms the full 3D environment (skyscrapers, illuminated window grids, trees, tunnel
  arch, and car) is rendered by the geometry pipeline.
- **Remaining item**: Background exposure and tone-mapping curve tuning in post-processing
  passes (where background float values near $0.0039$ get clamped to black in the final
  presentation resolve).
- Shared monotonically increasing bridge submission IDs fixed repeated UI
  submission failures; UI draws succeed.
- Plume HDR composite remains deferred without a synchronized scene preload;
  live Plume MSAA and terminal resolve handoff are in progress.

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
