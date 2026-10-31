# CRT feasibility follow-up: compare before extending the translator

Planning decision, 2026-10-14. This is proposed work, not a report of a successful
DLL translation or execution. It follows `CRT_FEASIBILITY_RESULT.md` and refines
the next gate in `CRT_NEXT_PHASE_PLAN.md`. The ownership architecture and later
validation requirements in that plan remain in force.

## Recommendation

Keep the proposed architecture: one translated CRT owns CRT state, with native
Win32/NT services underneath. Before implementing FISTTP/UD2 support, perform
**one bounded comparison against the Microsoft VC90 runtime in the game's
redistributable**. Inspect an ignored copy without running the installer. If its
identity and metadata justify proceeding, attempt strict translation on the
unchanged pinned kit. Stop with a comparison report before building a DLL host.

This explicitly extends the previous one-candidate budget to one additional
candidate. It does not open an indefinite search through CRT versions. The
Microsoft image is a candidate until its version, assembly identity, exports,
dependencies and behavior have been checked; proximity to the game is not proof
that this modified Blur executable uses that exact runtime.

The CrossOver failure is small in instruction count but not yet proven small in
semantic cost. FISTTP needs separate rounding/exception treatment; UD2 needs
fault-site exception records and control-flow handling. The kit's CPU feature
policy adds another constraint. A metadata comparison is cheaper than committing
to those changes, and the Microsoft image could also provide a future ABI
reference. Fewer Wine-specific dependencies and fewer instruction gaps are
**hypotheses**, not findings about the unextracted DLL.

| Option | Cost and risk | Next decision |
| --- | --- | --- |
| Inspect one Microsoft VC90 candidate | Small staging/preflight change; possibly one Ghidra export and strict translation. May expose different loader, relocation, TLS, instruction or OS-service problems. | Do first; compare measured blockers. |
| Patch the CrossOver path | Reusable instruction support, but requires FP-state tests, precise fault addresses, CFG changes and an explicit CPU-feature policy. Still leaves Wine imports and ABI parity unproven. | Preserve as a conditional follow-up, with the contract below. |
| Expand the custom CRT now | Requires coordinated FILE, locale, arguments, heap and cleanup implementations without runtime evidence that this is cheaper. | Defer; coherent native CRT remains the fallback. |

## Evidence and its limits

Observed identities:

- Kit: `eb32cc42623e0e9e868b9d27423a57d5a5cba56b` in
  `local/recomp-kit`; inspected checkout is clean.
- Blur: SHA-256
  `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
- CrossOver DLL: SHA-256
  `f58ddc0f08d75e2d750ad66d54be059cd90e428704b5b4351a4efc1550df13ba`.
- Redistributable inspected read-only at
  `/path/to/Blur/_Redist/vcredist_x86.exe`: 1,821,192 bytes,
  SHA-256 `c6a7e484f4d84883bc1205bccea3114c0521025712922298ede9b2a1cd632357`.
  `cabextract -l` lists `vc_red.cab` (1,442,522 bytes) and `vc_red.msi`
  (232,960 bytes), plus installer/resources. No payload was extracted or run
  during this planning turn. DLL version, payload hash and assembly identity
  remain unknown; cabinet timestamps are not version evidence.

The preserved CrossOver attempt failed in six automatically identified functions:
FISTTP at RVAs `0xfbe3` and `0x58bb9`; UD2 at `0x17df4`, `0x2926c`,
`0x4bf36` and `0x4c493`. These RVAs belong only to the CrossOver SHA above.
`CRT_FEASIBILITY_RESULT.md` and ignored
`reports/recomp-crt-instruction-blockers.json` contain function RVAs, export
labels, instruction bytes, listing hashes and evidence. Confidence is high for
the encodings; function boundaries and runtime reachability are unverified.
The eight dangling direct-call diagnostics are downstream of missing bodies;
they are not yet evidence of eight additional semantic defects. Statistics were
not emitted after the fatal error, so there is no complete coverage result.

Additional observations from the pinned source:

| Source | Observation | Consequence |
| --- | --- | --- |
| `runtime/x86.h`, `fround_cw`, `fto_i*`, `fist_i*` | FIST helpers follow guest RC. The stack uses binary64 plus exact-integer metadata retained from FILD. Existing conversion comments cover masked invalid operations. | Aliasing FISTTP to FISTP or converting every value through `double` is insufficient. General 80-bit fidelity is not present. |
| `tools/recomp/translate.py`, `X87_INT`, `X87_MEM_INT`, `emit_x87` | FISTTP is absent from integer operand classification and emission. | Both classification and semantics need tests; merely recognizing the mnemonic is insufficient. |
| Same file, `TERMINATORS`, `successors`, listing-gap and final-fallthrough logic | UD2 is absent from control-flow termination handling. | An emitter alone can leave fabricated fallthrough/discovery targets. Preserve independent branch entries after a trap. |
| `runtime/seh.cpp`, `fill_context`, `recomp_seh_raise` | Both CONTEXT.Eip and ExceptionAddress are populated from `rd32(ESP)`. | Setting `c->eip` before the existing raise function alone does not report the UD2 fault address. |
| Same file, `call_handler` | ExceptionContinueExecution is explicitly unsupported. | A fault helper is not general resumable Windows SEH or proof of VC90 C++ EH. |
| `runtime/cpu.cpp`, `recomp_null_access` | Null checks need compile-time and runtime enabling; the comment says EIP can be the last synchronized boundary. | The low-address loads before the four UD2s need enforced checks and accurate fault context before runtime error-path claims. |
| Same file, `recomp_cpuid` | Leaf 1 ECX is zero; EDX advertises FPU/TSC/CMOV only. | Do not silently advertise SSE3 to reach FISTTP paths or assume instruction emission defines a consistent guest CPU. |
| `tools/recomp/tests/test_translate_insns.py`, `run_once` | Generic comparisons cover GPRs, selected EFLAGS and memory, not all x87 state. | New differential tests must explicitly observe defined FP status, TOP and tags. |

The pinned sample `samples/wine-msvcrt` remains upstream precedent for a different
Wine CRT. Its old x87 environment warning is not current evidence of a missing
emitter: this pin has FNSTENV/FLDENV support. No sample addresses or claimed
runtime results transfer to Blur.

## Smallest next experiment

### 1. Preserve inputs and prepare a separate candidate manifest

Use a new ignored root such as `local/recomp-crt-msvc90-preflight/` and unique
reports. Record the parent CrossOver report hashes and baseline Blur/config/kit
hashes before work. Copy the redistributable read-only, verify source and copy,
and extract only into that root. Inspect member names before extraction; reject
absolute paths, traversal and symlinks. Never execute installers or modify the
installed game. Record extraction tools, arguments and payload hashes.

Select exactly one i386 MSVCR90 payload using the package assembly manifest and
version metadata. Inspect Blur's embedded/external manifest, if present, and
record any version/policy mismatch. Do not infer a precise version from its DLL
import name. Keep license/redistribution notices with the local evidence.

The old wrapper hard-codes the CrossOver hash, paths, base and size. Generalize
only candidate description/output-root handling as needed, with synthetic tests;
retain exact hash checks and immutable output rules. Do not edit an old manifest
to make changed tooling appear to be the original run. Create a new manifest
with its own source fingerprints and explicit ancestry.

The interpreter path recorded in the old report,
`local/recomp-venv/bin/python`, was unavailable during this planning turn, and
the current `python3` lacks `pefile`. Restore a documented isolated tool
environment from recorded requirements before reproduction, recording versions.
Do not silently substitute the latest kit or weaken manifest verification.

### 2. Compare PE and loader requirements before translation

Record machine, version, image base/size, entry, sections/permissions, relocation
types, static TLS template/index/callbacks, delay imports, load configuration,
imports by name/ordinal, forwarders and export code/data classifications. Compare
all 204 Blur MSVCR90 imports; missing or differently classified imports need an
explicit explanation. Names alone never pass an ABI gate.

Build a dependency inventory against actual kit registrations and signatures;
separate implemented behavior, signature-only declarations, missing services and
unknown behavior. Include dynamic loading obligations revealed by source/listings.
Do not implement OS services or extract additional system DLLs in this experiment.

Compute a half-open, nonoverlapping guest map with checked 32-bit arithmetic.
The existing CrossOver proposal uses a 257 MiB arena, mapping its DLL at
`[0x10000000,0x100a9000)` beyond fixed heap/stack/TEB/TLS/shim regions.
That map applies only to that image. A Microsoft preferred base may require a
different arena or relocation; neither is assumed available. Audit arena
allocation/commit behavior before proposing a much larger address space.

Pinned aux loading has no general relocation pass, requires the configured
preferred base, and does not support auxiliary static TLS. A collision, relocation
requirement, high-base memory cost or static TLS requirement is a recorded loader
blocker. Do not rebase the PE bytes or edit its header to hide it. Metadata may
still justify a translation-only comparison, but cannot authorize host execution.

### 3. At most one strict whole-DLL comparison

Proceed only if an unambiguous i386 candidate is available and the comparison can
run without loader or translator modifications. Export hash-pinned listings into
a separate Ghidra project. Record the actual PE entry and export roots. One export
and one strict whole-DLL translation are allowed; no listing repair, native
replacement, permissive flag or root-limited pruning in this comparison.

Use external limits: 15 minutes for the analysis/export process, 2 minutes for
translation, with child-process cleanup and partial diagnostics preserved. These
are experiment caps, not speed predictions. A timeout or ambiguous statistics
is an incomplete comparison, never success. No long Blur translation or ARM64
host build is included.

Inspect both the exit status and emitted statistics: failed/unmodelled bodies,
unresolved direct targets, table gaps, undecoded table sites, and other recovery
errors. Require complete coverage of the candidate entry and intended exported
probe functions. Compare failures by actual bytes and RVA, not only error counts.
For every recovered label include its candidate SHA, RVA, evidence and confidence.

### 4. Produce the decision evidence and stop

Write a comparison with both candidate hashes, strict results, imports, layout,
TLS/initialization requirements and estimated categories of new work. The result
must say which facts are measured, which remain hypotheses, and whether either
candidate meets the **translation** gate. Do not report compatibility or successful
initialization from generated C.

This is the next planning checkpoint: notify the user so they can switch to Astra
before choosing loader changes, translator changes or a custom CRT expansion.

## Concrete decision and stop rules

**Experiment success:** one reproducible candidate comparison that either selects
a credible next executable experiment or rejects this Microsoft candidate with
specific evidence. A negative result can complete the comparison.

**Translation gate success:** strict translation completes with complete reports,
no failed/unmodelled functions or unresolved direct/table targets, and verified
entry/export dispatch roots. Any remaining recovery diagnostics must be explained
before proceeding; exit zero alone is insufficient. Passing says nothing about
CRT ABI, initialization, FP correctness or gameplay.

**Prefer Microsoft for the next executable probe** only if the comparison shows
a credible lower total burden across translation, loader, dependencies and ABI
validation. Fewer exports/imports alone do not decide. A high preferred base or
static TLS can outweigh fewer instruction failures; assess these explicitly.

**Retain CrossOver as the next candidate** if Microsoft is unavailable, mismatched
or materially harder, and the bounded semantic contract below is credible.
If both demand broad FP/SEH/loader work, stop DLL work and plan a coherent native
CRT slice. Do not automatically spend another candidate or patch budget.

Stop immediately on input/hash changes, path escape, ambiguous payload selection,
unexpected architecture, malformed PE data, overwritten evidence, permissive
translation, or any attempt to modify the installed game. Record missing tools
and unavailable evidence rather than claiming a reproduction.

## Conditional CrossOver patch contract

This is a proposed follow-up, not authorization to patch the pinned checkout in
this comparison. Use a separate checkout at the same base pin, and retain a
reviewable handwritten patch plus its hash. Its scope is two instruction families
and the narrow fault-address plumbing required to test them; no general CPU,
80-bit FPU, Windows EH or Wine subsystem rewrite.

**FISTTP:** add integer operand classification and width-aware conversion/store/
pop helpers. Test truncation independently of all four RC settings, 16/32/64-bit
destinations, signed limits, fractional values, signed zero, NaN/infinity, masked
invalid results, precision status, stack underflow, TOP/tags and exact integers
above 2^53. Preserve control word and host FP state. Validate supported precision
control settings without claiming binary64 represents arbitrary ext80 values.
No unchecked out-of-range C casts and no loss of `st_exact` metadata.

Intel specifies FISTTP as truncate/store/pop, with C1 cleared, C0/C2/C3 undefined,
and invalid, stack and precision exceptions; it requires SSE3. Use its instruction
entry as the semantic contract, not a FISTP alias.
[Intel instruction reference](https://cdrdv2-public.intel.com/671110/325383-sdm-vol-2abcd.pdf).
Resolve unmasked/pending exception ordering against the manual and a qualified
oracle before implementation. If that exceeds the bound, document a guarded
supported-state envelope and stop when execution leaves it; do not call that
full instruction parity. A guard is an explicit unsupported-state failure, never
a successful emulated exception or permissive translation mode.

**CPU feature gate:** document the relationship between emitted instruction support
and the guest CPUID profile. With the current no-SSE3 profile, do not silently run
FISTTP as though the profile advertised support. A feature-supported synthetic
profile can validate the helper; a production profile needs a separately tested
consistent contract. Do not enable all SSE3 based on one implemented instruction.
Reaching this conflict in the DLL probe is a stop requiring a feature-policy
decision, not a reason to change CPUID opportunistically.

**UD2:** make it a trap with no ordinary fallthrough. Audit successor/liveness,
listing-gap, final-fallthrough and discovery handling, including independently
reachable instructions after UD2. Introduce an explicit fault-site path that
supplies the instruction address to both the exception record and CONTEXT;
preserve existing software-raise behavior. Merely assigning `c->eip` is insufficient.
Raise guest illegal instruction (`0xc000001d`), with zero parameters and ordinary
fault flags, and terminate explicitly if unhandled. Never return zero, skip the
body, consume a guest return address, or label the site padding. Intel specifies
that saved EIP points at the faulting instruction.
[Intel invalid-opcode description](https://cdrdv2-public.intel.com/843823/252046-sdm-change-document-1.pdf).
Windows exception flags and fault addresses must retain their defined meanings.
[Microsoft EXCEPTION_RECORD](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record).

Test an unhandled trap and a synthetic registration-chain observer, including
exact ExceptionAddress/CONTEXT.Eip, unchanged GPRs/ESP/EFLAGS, absence of subsequent
memory writes, and explicit rejection of unsupported continuation. Do not present
that rejection as Windows' normal behavior. General resumption and VC90 C++ EH
remain separate blockers. Enable null checks and verify that a preceding invalid
load faults before UD2; accurate load-site reporting needs its own test, not the
last synchronized call address. Do not broaden that fix into general memory
protection silently.

**Validation and bound:** qualify the x86 oracle first. The existing Unicorn tests
need explicit x87 status/tag observations, and oracle disagreements require a
manual or native-x86 check rather than relaxed comparisons. Include synthetic CFG
and fault tests plus FIST/FISTP and existing SEH regressions. Budget one patch and
one corrective revision, then at most two strict DLL attempts. Stop on a third
instruction family, general resumable EH, required ext80 arithmetic, unresolved
CPU-feature policy or continued differential mismatch. No DLL execution until the
instruction tests and strict gate pass.

## Ownership and later execution gates remain unchanged

| State | Owner and boundary |
| --- | --- |
| CRT allocations | Selected DLL owns malloc/free/realloc metadata and lifetime. Native OS heap services return guest arena addresses. Never free across CRT owners. |
| FILE and descriptors | Selected DLL owns x86 FILE objects and CRT descriptor tables; native services supply guest-visible OS handles and I/O. No host `FILE *` in guest memory. |
| Locale, ctype, errno, arguments/environment | DLL owns CRT layouts and per-thread/process state. Native services implement the required NLS/TLS/process contracts. |
| Encoded pointers | One coherent guest 32-bit encoding contract for its full lifetime; callbacks remain guest addresses. Do not mix adapter and DLL cookies. |
| CRT data imports | Bind to the chosen DLL's real data exports, distinguishing an exported cell from its pointee and from functions. |
| Initialization/cleanup | Loader owns module/TLS lifecycle; DLL owns its CRT initialization and teardown; Blur owns its executable initializer tables. Do each once in dependency order. |

Before any later dynamic probe, enforce missing-import/unknown-dispatch/interpreter
failures at point of use; prove the actual nonzero PE entry was dispatched and
attach succeeded. The kit's successful return for an absent entry lookup must not
pass. Explicitly handle detach, dynamic TLS and module lifetime. No auxiliary
static TLS or relocation is considered implemented by this plan.

Before Blur integration, map/register dependencies before binding static imports;
initialize dependencies/TLS before executable startup. Current aux mapping after
the main IAT patch does not redirect Blur's existing CRT imports. Disable the 26
MSVCR90 adapters plus their data registration together in the DLL variant, and
assert each CRT binding's owner. Preserve the original adapter build. No hot
migration of heap, FILE, locale or encoded-pointer state.

After a candidate passes these gates, resume the original isolated allocation/
data probe and then FILE/locale/arguments/lifecycle tests, with an independent
same-binary x86 baseline and a compatible Microsoft VC90 ABI baseline. Keep the
original eight-service/two-runtime-fix-round bound, 10-second runtime watchdog
and three fresh passing runs per milestone. These budgets have not been spent
by the failed translation. A printed data value alone is not an ABI assertion.

If DLL feasibility fails, plan one native CRT owner with tested guest layouts,
starting at the first required C++ initializer dependency closure. Do not combine
native FILE/locale objects with a partly initialized DLL. Keep unimplemented
behavior explicit, and require original-runtime comparisons before claiming parity.

## Implementation order and validation obligations

1. Restore/record the isolated tooling environment; preserve baseline hashes.
2. Add candidate/output-root support and synthetic malformed-PE/path/manifest tests.
3. Stage and inspect one Microsoft payload; record compatibility and loader risks.
4. Run one gated export/strict translation; save the candidate comparison.
5. Stop at the planning checkpoint, before patching the kit or building a host.
6. Only after that decision, execute the selected bounded patch or loader/probe
   work; then progress through the original CRT boundary and Blur startup gates.

After code changes run `python3 -m unittest discover -s tests -v`, the normal
CMake build, and relevant opt-in startup CTests/build as required by AGENTS.md and
the existing experiment. For kit changes also run the targeted instruction/CFG/
SEH tests and kit build checks. Preserve exact commands, tool versions and output.
No need to regenerate the full game to validate candidate preflight tooling.

This planning turn changed documentation only. Historical validation remains
51 Python tests, normal/opt-in builds and three CTests passing as recorded in
`CRT_FEASIBILITY_RESULT.md`; those checks were not rerun here. No DLL translation,
build or execution occurred in this turn. There are still no verified rendered
frames, menu or gameplay.
