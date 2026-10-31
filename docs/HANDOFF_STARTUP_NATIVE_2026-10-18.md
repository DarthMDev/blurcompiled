# Continuation handoff — startup toward movies and main menu

**Newer checkpoint:** read [the verified CVTPS2PD continuation](HANDOFF_CVTPS2PD_2026-10-18.md)
after this historical handoff. Three bounded modules pass the original blocker;
the next old generated trap is VA `004e067d` / RVA `000e067d`.

User asked to stop for a new conversation because the context is large and the
previous session consumed their usage window. Give concise updates and work in
bounded verified steps. Preserve the existing build and avoid repeating broad
analysis, whole-image generation or the full native build without a concrete need.
No goal, automation, subagent, commit or PR was created. The repository has no
commits: source files are untracked. Do not use git log to reconstruct progress.

## Workspace and rules

- Workspace: `/path/to/blur-native`.
- Read `AGENTS.md`. Installed `/path/to/Blur` is read-only reference.
- Binaries, assets, generated game C, Ghidra DBs and reports stay ignored under
  `local/` or `reports/`. Never edit generated instructions to bypass a failure.
- Distinguish observed behavior, hypotheses and new implementations; do not
  claim a playable/decompiled game or invent Amax authentication.
- After code changes run `python3 -m unittest discover -s tests -v`, the root
  CMake build, and relevant native/translator checks. Synthetic parser fixtures.
- No subagents unless explicitly requested. Applicable kit rules are in
  `local/recomp-kit-static-binding/AGENTS.md`; native compilation goes through
  its `tools/build.py` helpers, formatting through `tools/format.py`.

Pinned inputs:

- Blur SHA-256 `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`, base `00400000`.
- MSVCR90 SHA-256 `ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185`, base `78520000`.
- Kit HEAD `eb32cc42623e0e9e868b9d27423a57d5a5cba56b`, with experimental local changes.
- Interpreter: `local/recomp-msvc90-venv/bin/python`.

## Current achieved checkpoint

The whole corrected translation is compiled and linked into a Mach-O ARM64
experimental host:

`local/startup-native-r2/build/recomp/pop_headless`

It uses the normal generated-code `-O2` setting. One 17 MB chunk took about ten
minutes; an attempted cancellation found the process had already finished, so no
lower optimization override was applied. The full build took about twenty minutes.
Subsequent incremental changes are much cheaper. Do not restart it gratuitously.

Corrected generation: 48,409 game functions / 68,568 dispatch entries, plus 2,608
CRT functions / 3,290 entries. No reported failures or unresolved table entries.
69 main recovery events remain (JSON truncates to 50 diagnostics); diagnostics
and 67 withdrawn guesses exactly match the previous corrected MOVSD generation.
This is not whole-image behavioral acceptance. Structural raw-entry/body coverage
passes independently via `tools/verify_recomp_generation.py`.

The new host uses translated Lua bodies: speculative repair overrides in
`recomp/native/startup.h` are opt-in and disabled. Other earlier native/runtime
experiments remain unreviewed; do not claim their parity. No historical
`tools/patch_unmodelled_sse.py` postprocessor was used on the new generation.

## What changed this conversation

1. Configured nonlocal returns per game with `[translate].intrinsics`, removing
   another game's hardcoded defaults. Blur setjmp thunk VA `00d449ce` / RVA
   `009449ce` is PE-import-backed `_setjmp3`. Exact CRT longjmp is VA `78594458`
   / RVA `00074458` (export table). Generated setjmp is a legal controlling
   expression; landing preserves arbitrary nonzero guest EAX values, zero becomes
   one. Runtime rejects a different CPU context. Nested synthetic native cases
   cover 0, 1, 7, -7 and INT_MIN. Full stale-buffer and CRT unwind parity remain open.
2. Fixed intrinsic generation omitting internal continuation definitions. Only
   the audited primary entry is replaced; raw internal translated entries remain.
   CRT continuation `785944a6` / RVA `000744a6` is an observed unwind continuation.
   Main generation used the earlier generator revision (snapshot saved at
   `local/startup-native-r2/translator-main-source-snapshot.py`); its only intrinsic
   body is a single-entry thunk, so the later continuation adjustment has no
   effect there. CRT was regenerated after that fix.
3. Diagnosed and fixed the real resource-loading stall. The stream decoder at
   VA `00add770` / RVA `006dd770` returned -3, `incorrect header check`, consuming
   two bytes. Input matched shaders-speed.pak at `0x60004`. File tracing proved
   ReadFile ignored OVERLAPPED: requested 534528, descriptor at 393216. The new
   implementation honors 64-bit offsets, retains async cursors, updates completion
   status/count and events, and keeps synchronous cursor advancement. Completion
   is immediate; queued async I/O, completion ports and GetOverlappedResult are
   not implemented by this slice. Contract source:
   https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile
   Synthetic immutable fixtures cover positioned bytes, high offsets/EOF, partial
   reads, invalid metadata, cursor/offset behavior and events.
4. Added bounded read-only stream observer `recomp/diagnostics/stream_decode_watch.c`.
   It logs at most eight negative returns and never repairs data/results.
5. Verified the static Bink lock/register/decode/unlock contract against PE bytes.
   See `docs/BINK_FRAMEBUFFER_CONTRACT.md`: registration is currently a no-op,
   decoded planes are not populated, and the record's frame-rate fields conflict
   with duplicate counter writes. These are separate future integration gates.

## Current exact blocker — start here

After the positioned-read fix, startup passes the old stream error, reads many
additional archive offsets and issues 173 texture-creation requests, then aborts
in about four seconds on:

- VA `00797e53`, RVA `00397e53`, bytes `0f5ac0`.
- Actual PE decode: **`CVTPS2PD XMM0,XMM0`**.
- Reached in native `fn_00798140` (recovered function ownership includes earlier
  blocks); stack also includes `body_00c3b160`, `fn_00c31240`, `fn_00c312fc`.
- The runtime's “CPU extension not advertised” message is misleading: this is
  SSE2, and an experimental `recomp_cvtps2pd` helper already exists at
  `local/recomp-kit-static-binding/runtime/x86.h` around line 415. The translator
  still emits `recomp_unmodelled(...)` for CVTPS2PD. Audit the helper, add
  meaningful synthetic native-vs-Unicorn coverage (aliasing/register forms and
  memory form if supported), then wire the emitter. Do not duplicate the helper
  blindly, advertise unrelated CPU features, patch generated C, or bypass the call.
- Prefer a correctly generated bounded affected-function module using existing
  observed-slice/lazy-static tooling before an expensive whole-image rebuild.
  Inspect ownership/alternate entries carefully; do not invent a standalone entry
  at the unsupported instruction. If a full regeneration is necessary, explain
  the concrete dependency first. Existing original listings must stay intact.

Current run has no stream observer errors and no missing dispatch report. It has
only a uniform black startup PPM; integrated movies/main menu remain unverified.
Read `docs/STARTUP_MOVIE_PROGRESS.md` for exact evidence and confidence notes.

## Reproduce and inspect

```sh
cd /path/to/blur-native
python3 tools/recomp_blur_probe.py \
  --out local/FRESH_OUTPUT_NAME \
  --binary local/startup-native-r2/build/recomp/pop_headless \
  --assets '/path/to/Blur' \
  --extra-code local/startup-native-r2/stream-watch/watch.dylib \
  --seconds 10 --frames 600
```

`--out` must be fresh. The observer is optional; it changes no guest results.
Profiles/logs are per-probe; Win32 writes still use ignored scratch executable
paths rather than a fully separate per-probe overlay. Reference assets remain
read-only. The observer library is built through kit `tools/lazy_static.py` with
`--generated local/startup-native-r2/stream-watch/generated` and the staged game
configuration. Current host **has video decoding OFF**. A finished Bink placeholder
is never playback evidence.

Existing evidence (do not overwrite):

- `local/startup-native-r2/probe-r1/`: corrected host before read fix, timed out.
- `.../probe-stream-r1/`: decoder text/input prefix.
- `.../probe-stream-offset-r1/`: requested/current offset mismatch.
- `.../probe-positioned-r1/`: resource advancement, exact unsupported SSE2 abort.
- `reports/startup-native-r2-*.json` and logs; final source/binary hash manifest
  `reports/startup-native-r2-final.json`.
- Fresh build config and helper `local/startup-native-r2/build_host.py`.
  This helper configures and builds; prefer invoking imported `tools/build.py`
  `build()` directly for incremental targets so configuration does not churn deps.
- Kit cached dependency sources are already wired in CMake. Do not download SDL,
  glslang or FFmpeg to work around local configuration.

Validation at handoff:

- 74 root Python tests; root CMake build; all four root CTests pass.
- 201 kit translator/configuration tests pass.
- Fresh native `seh_tests`, `crt_strict_tests`, `intrinsics_tests` pass. Strict
  suite includes ten subprocess cases, including the positioned-read fixture.
- Root `blur-movie` remains the previously verified silent ARM64/Metal resource
  viewer. It is separate from recompiled game startup; run it with
  `./build/blur-movie --assets '/path/to/Blur' --movie BizarreSplash.bik`.

Patch artifacts:

- `recomp/patches/startup-native-r2-reviewed-slice.patch`: focused delta from the
  previous movie-runtime checkpoint, including positioned reads.
- `recomp/patches/startup-native-r2-runtime-combined.patch`: full kit diff against
  its pin, including unreviewed earlier work; reproducibility, not acceptance.
- `startup-intrinsics-reviewed-slice.patch` is the earlier narrower patch.

No new full movie/menu/gameplay milestone is claimed. Next bounded goal is to
remove the proven CVTPS2PD lowering gap with tests, then identify the first new
startup blocker without repeating this investigation.
