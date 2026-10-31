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

## Where to start

- [SDK frontend and candidate records](PLUME_REX_FRONTEND.md)
- [Plume host bridge](PLUME_HOST_BRIDGE.md)
- [Xbox UI adapter](PLUME_X360_ADAPTER.md)
- [Xenia issue research](XENIA_BLUR_GRAPHICS_LEADS_2026-10-30.md)
- [Plume/Xerenge research](PLUME_XERENGE_RESEARCH_2026-10-30.md)

For a graphics PR, identify the exact backend and input hash, show the original
and resulting scene, explain the resource/shader state involved, and include a
focused synthetic check where appropriate. Captures and game shader bytecode
stay local; share sanitized conclusions and reproduction instructions.
