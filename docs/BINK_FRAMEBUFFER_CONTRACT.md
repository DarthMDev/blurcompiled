# Blur movie framebuffer contract — observations

This describes a static evidence slice, not a completed decoder adapter. All
addresses refer to Blur.exe SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`,
image base `00400000`. Capstone decoding of actual PE bytes was compared with the
pinned Ghidra listings. `reports/startup-bink-framebuffer-evidence.json` records
ten small instruction spans, including bytes and RVAs. Confidence is high for
these instruction/offset observations; complete function behavior and live
rendering have not been validated.

| Observed entry / RVA | Evidence |
| --- | --- |
| `006841e0` / `002841e0` | Caller passes the record at object + `bb0` and framebuffer description at object + `bf4` to GetFrameBuffersInfo (`00684260` / `00284260`), allocates textures through `00685a20`, then calls RegisterFrameBuffers (`006842a5` / `002842a5`). |
| `00685a20` / `00285a20` | Texture group base is the second argument, corresponding to object + `bb4`; its framebuffer description starts at + `40`. Reads total frame count at + `40`, Y dimensions at + `44`/+`48`, chroma dimensions at + `4c`/+`50`. Frame descriptions advance by `30` bytes while texture groups advance by `20` bytes. |
| `00685cc0` / `00285cc0` | Reads current frame index at texture base + `54`; locks textures and writes returned pointers/pitches into the frame descriptions. For Y, writes at description + `1c`/+`20`; the next planes advance by `c`. |
| `00684570` / `00284570` | Calls the lock routine at `00684673`, RegisterFrameBuffers at `0068468f`, then DoFrame at `0068469f`. Later passes texture group, Bink record and a state flag to `00685dd0`. |
| `00685dd0` / `00285dd0` | Unlocks texture planes and clears description buffer pointers. Uses the current index at texture base + `54`, shifted by five, to select a texture group. |

The observed description layout relative to object + `bf4` is:

| Offset | Observed use |
| --- | --- |
| `00` | Frame count |
| `04`, `08` | Y dimensions |
| `0c`, `10` | Chroma dimensions |
| `14` | Current frame index used to select a texture group |
| `18 + frame*30` | First plane descriptor |
| Plane + `00` | Allocation flag inspected by the allocator |
| Plane + `04` | 32-bit guest buffer address written by texture LockRect |
| Plane + `08` | Pitch written by texture LockRect |

The four descriptors per frame are consistent with Y, two chroma planes and
optional alpha. Their semantic chroma order still needs comparison with decoded
frames; the layout alone does not prove color-channel order or shader behavior.
The existing experimental adapter's dimensions/descriptor offsets agree with
this slice, but RegisterFrameBuffers is currently a no-op and DoFrame does not
populate these locked planes.

An adapter must validate the entire guest destination before copying, honor each
plane's pitch, retain no host pointer beyond the lock lifetime, select and publish
a consistent frame index, and account for EOF/close. This remains an implementation
and verification gate. The standalone resource viewer does not exercise this
contract. The current recompiled diagnostic host has video decoding disabled;
reaching a finished placeholder record would not count as movie playback.

A further observation in `006849f0` / RVA `002849f0`, specifically
`0068508d..00685120` / RVAs `0028508d..00285120`, shows the caller reading the
record's `+18` as a denominator, dividing `+14` by it, then dividing the total
frame count at `+08` by that rate to calculate duration. This is inconsistent
with the current experimental FFmpeg adapter's duplicate counter writes at
record `+10`/+`14`: `+14` would change with every frame and `+18` stays zero.
A zero denominator skips the calculation. This is a statically demonstrated
adapter mismatch for Blur, not a claim that it caused the current resource-load
stall. Its fix needs a per-layout ABI decision; another game's record offsets
must not be inferred from Blur's. The evidence JSON includes this tenth span.
