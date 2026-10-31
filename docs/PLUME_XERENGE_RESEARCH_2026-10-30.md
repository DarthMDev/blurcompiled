# Plume / Xerenge research — October 30, 2026

## Conclusion

The linked fork is useful as reference material, but replacing the Plume dependency would not establish a fix for Blur's dark/red scene. The strongest findings are two concrete SDK correctness fixes and the related Xerenge renderer's diagnostic approach. Neither has been established as the cause of Blur's current visual defect.

## Revisions and scope

Reviewed `shipa-2/plume-xerenge` at `919b921590fa8bed44da50fa9ace2e132de1aa00`, Xerenge at `25a63c2d1145d9085293482677cb00e9a3368721`, and its SDK fork at `bda52d674b2b6a99bbe725efd51f13996b434405`. The Plume fork diverges from our pinned `0ce184b19270e49236afbb74052a6b267494b86b` by 24 commits ahead and six behind. At review time our pin equals upstream renderbag/plume main. A handful of source/API snapshots were downloaded into ignored reports; no repository was cloned or installed.

The fork's substantial work is Vulkan compatibility, caches, descriptor offsets, copies and pass controls. Its native Metal diff against our pin adds depth/packed-float format mappings and wireframe state; it does not contain a demonstrated Blur color fix. Its pass-area/attachment-access interfaces have default no-op implementations for backends that do not override them. The Xerenge macOS route uses Vulkan/MoltenVK. The Plume fork retains MIT licensing; the separate Xerenge title renderer is GPL-3.0, while SDK files carry their own notices. Review those independently when choosing code to reuse.

Sources: [fork](https://github.com/shipa-2/plume-xerenge), [Mac Vulkan support](https://github.com/shipa-2/plume-xerenge/commit/a458a433c8228c6af323bf1ff97e9c7dadd76188), [Xerenge renderer](https://github.com/shipa-2/Xerenge/tree/25a63c2d1145d9085293482677cb00e9a3368721/rexglue/src/plume_renderer).

## Concrete SDK findings

### Texture-fetch exponent bias — confirmed local source defect

[Xerenge's fix](https://github.com/shipa-2/rexglue-xerenge/commit/6c94ffdc5d5c5e3fe630d6d887361f4f605b7472) changes SPIR-V result exponent bias to use fetch word 3, bits 13–18, instead of word 4. Our pinned SDK still has the old word-4 extraction in `spirv_translator_fetch.cpp:2032`; its DXBC translator uses word 3 at `dxbc_translator_fetch.cpp:1957`.

A synthetic fetch with zero exponent and a negative LOD-bias encoding produces an erroneous exponent of -8 in the old calculation: a 1/256 multiplier. This independently reproduces the arithmetic defect described upstream. It matters to Vulkan/SPIR-V, including translated shaders potentially used through Plume. Candidate-010's complete scene currently uses MSC/DXBC, so this defect alone does not establish the cause of the current screenshot. Existing embedded SPIR-V modules would also need independent audit/regeneration; fixing a generator does not change already captured modules.

### In-place saturating vector packs — confirmed emitter defect, Blur applicability unresolved

[Xerenge's vector fix](https://github.com/shipa-2/rexglue-xerenge/commit/25f63de83eba29c93fd8d3ddc020fc51a7689cfd) snapshots both sources before `vpkuhus`/`vpkuwus` writes when registers alias. Our SDK builder still emits the unsnapshotted form at `src/codegen/builders/vector.cpp:1096` and `:1124`. A synthetic in-place unsigned-halfword pack changes bytes that should remain 128 into saturated 255 values under the old algorithm.

An audit of 381 current generated Blur source files found no matching affected direct-saturation pattern or literal opcode names. Therefore the generator issue is real, but there is no evidence yet that it executes in Blur or explains its red appearance. This does not justify a blanket CPU patch/rebuild as a visual fix.

## Useful renderer ideas

Xerenge hooks recovered game Direct3D calls, owns host render targets and submits copies in draw order. It does not provide a generic drop-in Xenos command-processor replacement for Blur. Its shader-register probe narrows an output fault to intermediate shader values; it also provides bounded frame probes and resource/constant tracing. These are stronger diagnostic references than applying a brightness or color-LUT correction. Burnout-specific addresses, shader allowlists, tint heuristics, clear constants and fallback thresholds cannot be carried into Blur without independent evidence.

[Post-processing PR #8](https://github.com/shipa-2/Xerenge/pull/8) is useful for checking guest versus host viewport sizes, downsample chains and half-pixel math. Its fixes concern particular bloom/motion/composite artifacts, not a proven explanation of our dark/red output.

## Implications for the current Blur path

Candidate-010 logs one Plume UI draw, zero composite submissions and zero terminal resolves. The candidate builder retains the SDK draw and only replaces presentation when a Plume swap-source buffer exists; that buffer remains empty in these runs. Thus the visible complete scene remains SDK Xenos/Metal output. A Plume RHI swap alone cannot repair it.

The next priorities are: fix the independently confirmed bridge submission-ID ordering defect with synthetic cross-frame coverage; instrument the actual MSC scene pass, its constants and intermediate outputs; and verify the original 1280-wide / 320-pitch resolve mismatch against current resource dimensions. Keep the failed resolve-sync comparison as negative evidence for that single option, not proof that HDR/readback/format handling is correct. Apply the SPIR-V exponent fix in an isolated candidate when testing that path, preserving the reference SDK and pins. No game launch, runtime patch, SDK modification, or renderer code change was made for this research.

## Local evidence

Ignored `reports/plume-research-20261030/` contains repository/tree/commit snapshots, exact fix diffs, selected source files with hashes, local-source hashes, the generated-pack audit, and synthetic arithmetic checks. These record observations and hypotheses; no guest symbol was recovered or assigned a new meaning.
