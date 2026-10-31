# Gemini work review and observed-entry recovery — 2026-10-18

The checkout has no Git commits. Attribution to Gemini follows the user's
description and the dated local files, not a verifiable commit history.

## Observed recent work

The October 17–18 files add `tools/recomp_blur_static_crt.py`, an SSE/generated-code
postprocessor, conditional retirement of duplicate MSVCR90 startup adapters,
handwritten initializer overrides, and broad local recomp-kit changes across
the loader, scheduler, windowing, audio and D3D9/D3DX interfaces. The existing
ARM64 host executes CRT attach, returns from an executable initializer walk,
enters graphics setup and runs Lua-related code. It does not establish a menu
or gameplay. The reference installation was not modified by this review.

The complete local kit diff against its base commit was preserved before this
review's build-tool change as
`recomp/patches/gemini-review-runtime-baseline.patch`. This is an **unreviewed
experimental baseline**, not a blanket compatibility acceptance. The separate
`lazy-static-reviewed-generation.patch` adds a build-tool option to compile an
existing module generation without rerunning whole-image discovery.

The generated-code postprocessor also introduces a null-texture branch bypass,
changes scheduling and substitutes selected setjmp/longjmp call sites. These
are not established by the earlier strict translation result or CRT fixture
parity. In particular, its `recomp_setjmp_return(c, setjmp(*b))` expression is
outside the C standard's supported setjmp invocation contexts. The prior kit
translator already emits an `if` controlling expression. Call-site substitution,
return values, active-frame lifetime and per-thread ownership need a dedicated
synthetic/differential gate before claiming exception/error-path fidelity.
The new module below does not apply this postprocessor. The existing host still
contains its earlier edits, so improved dispatch is not whole-game parity.

## Recovered dispatch entry

All Blur addresses in this checkpoint belong only to executable SHA-256
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
The exact auxiliary CRT remains SHA-256
`ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185`.

| Symbol | RVA | Evidence | Confidence |
| --- | --- | --- | --- |
| Unnamed entry at VA `0x00560000` | `0x00160000` | Runtime indirect call at `0x0056c799`; PE vtable word at `0x00e86938`; corrected Ghidra export; bounded instruction emission; strict host runs | High for reached entry and code bytes; full behavioral parity unverified |

The baseline strict host stopped at this target. Its non-strict counterpart
returned zero for the missing function and continued. Initial automatic Ghidra
recovery found 211 instructions but retained a stale instruction at `0x00560049`
inside the real `ADD ESP,4` at `0x00560048`; it also missed the valid branch
destination `INC EDI` at `0x0056004b`. Independent Capstone decoding exposed this
disagreement. No LES support or instruction trap bypass was added.

`ExportObservedFunction.java` re-disassembled the bounded region through
`0x005602af` in a **disposable database copy**, refusing another function entry
inside the range. The corrected listing contains 212 instructions. The baseline
database and exports remain unchanged.

`tools/recomp_observed_slice.py` uses the existing kit's instruction emitter with
eager flags. It refuses open branches, instruction-boundary gaps, indirect jumps,
SEH and jump tables. External calls dispatch through the existing host. The
output is one extra-code module, compiled through the kit's `tools/lazy_static.py`.
No generated instruction implementation is hand-edited. This is a bounded
structural translation gate, **not** renewed whole-image translation acceptance.
Earlier attempts to run whole-image discovery with sparse inputs were interrupted;
the supplementary full-image attempt hit its 180-second watchdog. Neither passed.

## Execution result and limits

The existing host SHA-256 is
`d813f2b50218a8ce5b636475c1bd7897592c5e943ecee6685a6a3d707668ceff`.
The added ARM64 module SHA-256 is
`e54df388289cf3161001f128c4a8f22b663dd518d58f27eae60eff5c41d649f5`.

| Fresh run | Observed result |
| --- | --- |
| Strict baseline | Guard stops at missing `0x00560000`, exit 6 |
| Strict with recovered module, first run | Five uniform black frames, guest exit 0, host exit 4; no missing dispatch diagnostic |
| Two further five-frame runs | Later SIGSEGV in Lua-related execution; host exit 5 |
| Ten-second / 100-frame diagnostic | Same later SIGSEGV; host exit 5 |

The crash context reports thread 6, synchronized guest EIP `0x00af12e2`
(RVA `0x006f12e2`), EAX `0xfffffff4`, and host fault address `0x70fffffff4`.
EIP is the last synchronized guest context, not proven to be the exact faulting
instruction. Native backtraces contain `fn_00af12a0`, `body_00af0dc0` and the
protected Lua call path. Logs also report missing `UITexture` and nil-call errors.
**Hypotheses:** Lua state corruption, incomplete resource/UI setup or the existing
non-local-jump substitutions could contribute. None is selected as the root cause.
The missing-dispatch failure is removed; stable startup is not achieved.

Evidence lives in ignored `local/recomp-blur-review-r1/`,
`local/recomp-blur-review-strict-r{1,2,3,4}/`,
`local/recomp-blur-review-extended-r1/` and `reports/blur-review-result.json`.
The final regenerated C files were checked byte-identical to the compiled module
sources. Game-derived listings, generated C, libraries, logs and databases remain
ignored.

## Reproduction and next gate

From the repository root, using fresh output paths:

```sh
local/recomp-msvc90-venv/bin/python tools/recomp_observed_slice.py \
  --observed local/recomp-blur-review-r1/observed-function-r2 \
  --out local/blur-observed-next
local/recomp-msvc90-venv/bin/python local/recomp-kit-static-binding/tools/lazy_static.py \
  --game-dir local/recomp-blur-msvcr90 \
  --discovered local/blur-observed-next/discovered.txt \
  --generated local/blur-observed-next/generated \
  --out local/blur-observed-next/observed.dylib
python3 tools/recomp_blur_probe.py --out local/blur-probe-next \
  --extra-code local/blur-observed-next/observed.dylib
```

The probe defaults to strict execution, bounds runtime, kills the process group
on timeout, checks executable/CRT hashes, and preserves fresh profiles, logs and
input hashes. `--diagnostic` explicitly requests observation past runtime guards;
it is never an acceptance mode. The executable's own early frame-cap shutdown
can mask later failures, as the repeated runs here demonstrate.

Next: qualify the setjmp/longjmp seam with nested and threaded synthetic fixtures,
then capture the first invalid Lua state write with bounded guest-memory checks.
Restore instruction behavior and resource ownership before adding rendering
bypasses. Uniform frames, CRT attach and a successful short run cannot close
this gate. Recover UI/asset dependencies only from actual binary/resource evidence.

Validation: 66 Python tests; normal CMake build with startup adapters enabled;
three project CTests (startup adapter, bootstrap help, native assets). The new
slice separately emitted without unsupported instructions and compiled to an
ARM64 module. The kit's broader compatibility suites were not rerun, and this
review does not validate all of the preserved Gemini runtime changes.
