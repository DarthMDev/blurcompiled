# Next CRT phase: bounded MSVCR90 feasibility experiment

Planning decision, 2026-10-08. This document proposes work; it does not report a
DLL translation, build, or execution. The existing Blur experiment remains intact.

Follow-up, 2026-10-14: the first strict CrossOver DLL attempt failed; see
`CRT_FEASIBILITY_RESULT.md`. The next bounded comparison and conditional translator
patch contract are in [CRT_TRANSLATOR_NEXT_PHASE_PLAN.md](CRT_TRANSLATOR_NEXT_PHASE_PLAN.md).
That follow-up refines the next experiment; the ownership and integration gates
below remain requirements.

## Decision and scope

Recommend a **bounded DLL feasibility experiment**, aiming for a **single
DLL-owned CRT above native Win32/NT services**. Use the inspected CrossOver i386
`msvcr90.dll` as the first candidate. Do not yet choose it as Blur's production
runtime, expand the custom FILE/locale implementation, or recompile a second CRT.

The hypothesis is that lifting this DLL can retain a coherent x86 CRT ABI and
reduce the amount of coupled state we must implement. Its absence of a PE TLS
directory and its nonoverlapping preferred base make a small experiment plausible.
Its Win32/NT dependency surface, translation coverage, and VC90 compatibility
remain unproven. Export-name coverage is only a way to select a candidate.

The intended hybrid boundary is below the CRT: translated CRT code owns CRT
objects; native services supply guest memory, handles, files, synchronization,
TLS slots, and OS queries. Any later native replacement inside the CRT requires
its own ABI and ownership proof. This is not permission to mix the existing CRT
adapters with DLL implementations opportunistically.

| Route | Cost and risk | Decision |
| --- | --- | --- |
| Extend native CRT adapters | Fast incremental builds; direct control of guest storage. FILE, locale, argument/environment state, cleanup, invalid-parameter handling, FP and C++ EH create a substantial compatibility implementation. Synthetic tests alone cannot establish VC90 parity. | Preserve as fallback; defer broad expansion. |
| Lift MSVCR90 with native OS services | Smaller candidate than Blur; potentially preserves internal layouts and algorithms. Requires strict translation, real DLL initialization, dependency behavior, import ordering, and differential tests. Can move the missing-runtime problem down to Kernel32/NTDLL. | First experiment. |
| Mix CRT implementations by import name | Appears incremental but risks incompatible heaps, stream layouts, locale pointers, errno, encoded values, and exit tables. Mapped-export precedence also differs from shim registration. | Reject as an architecture. Allow only measured replacements within one owner's ABI. |

DLL lifting does not solve the translator's FP or SEH limitations. The experiment
must be cheap to reject before another whole-Blur build. No graphics, Amax,
assets, menu, or gameplay milestone is part of this decision.

## Evidence and provenance

Evidence classes used here:

- **Observed:** local source, PE metadata, or existing report inspected during
  this planning task. This establishes what the source/bytes/report say.
- **Upstream-reported:** claims in the pinned kit sample; not reproduced here.
- **Hypothesis:** expected compatibility or reduction in work, requiring a test.
- **Proposed implementation:** new host/adapter behavior, not recovered source.

Pinned identities:

| Input | Identity |
| --- | --- |
| recomp-kit | `eb32cc42623e0e9e868b9d27423a57d5a5cba56b`; `git rev-parse HEAD` confirmed it and its checkout was clean. |
| Blur.exe | SHA-256 `dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`; the ignored executable copy was rehashed read-only. |
| CrossOver MSVCR90 candidate | SHA-256 `f58ddc0f08d75e2d750ad66d54be059cd90e428704b5b4351a4efc1550df13ba`; installed DLL rehashed read-only. |

The candidate source is
`/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows/msvcr90.dll`.
It is a Wine/CrossOver implementation, not Microsoft's original VC90 runtime.

The initial inputs read were `AGENTS.md`, `docs/CRT_ARCHITECTURE_HANDOFF.md`,
`docs/STARTUP_RUNTIME_SLICE.md`, `docs/RECOMP_EXPERIMENT.md`, `recomp/game.toml`,
and `recomp/native/startup.cpp`. The existing reports
`reports/recomp-globals-boot.json` and `.log` confirm five successful C
initializers followed by entry into the C++ table, unresolved CRT calls, and an
invalid-range abort. The JSON records exit 6, no timeout, unchanged executable,
and `presented_frames: null`; that null is not a completed zero-frame measurement.
There is no evidence of rendered game frames, menu, or gameplay.

The existing strict translation completed despite 1,127 recovery errors; the
ARM64 host build and one CRC routine's x86 parity test establish only their
documented bounds. The startup adapter supplies 28 functions, of which 26 are
MSVCR90 and two are Kernel32, plus one CRT data import. These are new adapters.

### New read-only candidate observations

PE metadata was inspected using the existing `local/recomp-venv`'s `pefile` on
the hash above, without staging, lifting, or executing the DLL:

- PE32/i386, machine `0x14c`, file size 668,752 bytes, preferred base
  `0x10000000`, SizeOfImage `0xa9000`, PE entry RVA `0xd140`.
- TLS directory and delay-import directory both have RVA/size zero. This does
  **not** imply no dynamic TLS or runtime resolution: it imports `Tls*`,
  `LoadLibraryW`, `GetProcAddress`, and delay-resolution helpers.
- A base-relocation directory exists at RVA `0xa3000`, size `0x537c`.
  The kit does not apply it.
- All 204 Blur MSVCR90 import names exist among 1,452 exports. The candidate
  imports 131 Kernel32 and 12 NTDLL names.
- Forwarders `__threadhandle`, `__threadid`, and `_getdrives` target Kernel32.
  None is in Blur's 204 static MSVCR90 imports. Runtime lookup remains unaudited.

These candidate locations are tied **only** to the candidate DLL SHA-256 above.
Evidence is its optional header/export directory and section flags; confidence
is high for address/section classification, with behavior and signatures untested:

| Entry/export | RVA | Observed classification |
| --- | --- | --- |
| PE entry, no recovered source name | `0xd140` | Entry address; must execute the actual PE startup wrapper. |
| `_acmdln` | `0x96030` | Non-executable `.bss`, candidate pointer variable. |
| `_HUGE` | `0x96040` | Non-executable `.bss`, candidate numeric data. |
| `_adjust_fdiv` | `0x97284` | Non-executable `.bss`, candidate integer data. |
| `__sys_nerr` | `0x12440` | Executable `.text`, **not a data export** in this candidate. |

The old experiment note groups `__sys_nerr` with data imports. Do not carry that
classification into implementation: verify the caller and function ABI. Section
classification alone does not establish data size or a function's signature.
Record the same SHA/RVA/evidence/confidence fields for every subsequently analyzed
entry, callback, override, or recovered symbol; never reuse Blur RVAs for the DLL.

### What the pinned auxiliary path actually supports

Source references below are relative to `local/recomp-kit/` at the pinned commit.

| Source | Observed behavior and consequence |
| --- | --- |
| `tools/game_config.py:223` | Auxiliary modules are described as runtime-loaded DLLs. Configuration supplies exact hash, preferred base, size, listings, alignment, and optional entry points. Entries are sorted by key, not dependency order. |
| `runtime/loader.cpp:308` | Searches beside the executable before configured fallbacks; verifies hash, PE32, preferred base, arena fit and auxiliary overlap. Maps sections, binds that DLL's imports, snapshots post-IAT bytes, then registers the module. This is not a dependency-graph loader. |
| `runtime/loader.cpp:364` | Rejects a base different from the PE preferred base. No image relocation support. |
| `runtime/loader.cpp:397` | Merely warns about an auxiliary TLS directory; does not allocate auxiliary static TLS or run its callbacks. |
| `runtime/loader.cpp:262` | Already-mapped module exports precede native data/function shims. Missing mapped exports fail binding. However, this applies only to modules already registered at that moment. |
| `runtime/loader.cpp:752` | **Binds the main EXE before loading auxiliaries.** Blur's static MSVCR90 imports therefore remain shim/data bindings if only a module stanza is added. There is no later rebind here. |
| `runtime/loader.cpp:470` | Named and ordinal export lookup rejects forwarders. Code and data both resolve to their actual guest addresses when supported. |
| `runtime/kernel32.cpp:1138` | DLL entry invocation returns success if the entry is absent or has no translated module lookup. A successful `LoadLibrary` return is consequently insufficient evidence of initialization. |
| `runtime/kernel32.cpp:1149` | `LoadLibrary` attaches a mapped DLL, restores post-IAT bytes on fresh attach, tracks references, and invokes its PE entry. `FreeLibrary` detaches on the last reference. The reserved argument is always zero. |
| `runtime/loader.cpp:819` | `run_entry` invokes the main image's TLS callbacks and EXE entry, without attaching static auxiliary dependencies first. |
| `runtime/kernel32.cpp:518,3095` | Static TLS allocation is based on one main-image `LoaderTls`; guest threads have TLS arrays. The inspected thread path supplies no auxiliary DLL thread notifications. Dynamic TLS is a separate service. |
| `runtime/imports.cpp:229` | `imports_resolve` resolves registered trampolines, not mapped exports. Native callers must not use it as a DLL-export resolver. |
| `cmake/Translate.cmake:57` | Auxiliary listings become separate generated libraries with prefixed tables and whole-archive linking. An explicit overrides header can affect their generated dispatch; native shim registration alone cannot override DLL-internal calls. |

The upstream `samples/wine-msvcrt/README.md` reports four successful exports of
a different `msvcrt.dll`, reached through `LoadLibrary`/`GetProcAddress`. Its
preparer raises `guest_size` to `0x10100000`. Its documented DLL build accepts
table gaps and unmodelled instructions, including UD2, FISTTP and x87 environment
operations, and its execution reached unresolved imports. This establishes a
useful upstream mechanism precedent, not a strict locally verified MSVCR90 or
static-import solution. Do not copy those flags or that DLL's entry-point RVAs.

## Ownership contract

Use a fresh process for each route. Do not migrate live allocations, callbacks,
locale pointers, or encoded values between the current adapter and the DLL.
Microsoft documents the hazards of sharing CRT resources across different CRT
instances; this motivates a single owner, not an assumption of Wine fidelity.
[CRT object boundaries](https://learn.microsoft.com/en-us/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries).

| Resource | Owner and permitted native boundary |
| --- | --- |
| CRT allocation family | DLL owns `malloc/calloc/realloc/free`, aligned allocation, `_msize`, `_strdup`, and allocations hidden in CRT objects. Free through the matching DLL API. Native Kernel32 heap services return guest addresses and own underlying arena blocks; host allocations never become guest pointers. Do not call the old adapter's `free` on DLL results, even when underlying blocks happen to share an allocator. |
| Heap handles | Native OS layer must track allocations by guest heap handle, validate handles/flags, and release a destroyed heap's blocks. Current kit `Heap*` largely ignores handle ownership and `HeapDestroy` only removes a handle: adequate semantics and teardown must be tested or supplied. Keep host bookkeeping outside guest memory and guest metadata in 32-bit layouts. |
| FILE and file descriptors | DLL owns x86 FILE objects, standard streams, descriptor table, buffering, mode and error/EOF state. Native file/console services own opaque Win32 handles and translate buffers/counts. Never cast a guest FILE to host `FILE*`, or route DLL streams to native CRT functions. |
| Locale, ctype and multibyte state | DLL owns global/per-thread state, locale objects and returned tables. OS layer provides explicitly tested NLS/code-page behavior. No second native table behind `__pctype_func`; all consumers share the DLL's storage. Begin with C locale, then test a deliberate non-C/code-page case before claiming locale compatibility. |
| Arguments/environment | Native OS layer supplies coherent guest command-line/environment buffers and their OS allocation/free contracts. DLL owns parsing, `argc/argv/envp`, `_acmdln` and CRT environment state. Host `char**` is never exposed. |
| errno and thread state | DLL owns CRT errno and per-thread objects; native layer owns guest TEB/TLS slots and LastError semantics. Retire the adapter's TEB-to-errno map for this route. |
| Pointer encoding | DLL CRT entry points own their ABI. If they delegate to Kernel32 EncodePointer/DecodePointer, supply one process-scoped native service shared by all callers. Validate null, all-ones, callbacks, internal storage and encode/decode round trips. Encodings are opaque; matching original cookie bytes is unnecessary. Do not retain an independent adapter cookie. |
| CRT data imports | IAT points at actual DLL storage, with exact size/alignment and indirection verified. In particular `_acmdln`'s IAT slot refers to the variable, not directly to its string. `_HUGE` must receive real initialization; `_adjust_fdiv` must not have a second native copy. Accessor functions return the same owner's data. |
| CRT locks, FP, callbacks, cleanup | DLL owns numbered-lock mapping, math callback registration, exit arrays and CRT initialization/cleanup. Native critical sections cooperate with the guest scheduler. FP state belongs to each guest CPU context; no host-global approximation. Callback addresses and ABI remain x86 guest values. |
| Process/module initialization | Native loader owns mapping/binding, attach ordering and lifetime; DLL PE entry owns its internal startup; Blur's existing PE entry owns its EXE C/C++ table traversal through the selected CRT. Never manually run the same table twice. |

In an eventual DLL build, exclude all 26 MSVCR90 registrations and its data
registration from `startup.cpp`; retain the two independently tested Kernel32
adapters if needed. Preserve the original adapter build separately. Assert each
Blur MSVCR90 IAT slot resolves to the DLL, with any explicit forwarder policy
recorded. A late native override must preserve DLL-owned state and apply to all
relevant call paths, including internal direct calls, not merely one IAT slot.

## Address space and loading design

For the isolated experiment propose `[game] guest_size = 0x10100000` (257 MiB),
with one MSVCR90 auxiliary at base `0x10000000`, size `0x000a9000`. This is a
new configuration choice. Do not edit the current Blur configuration yet.

| Guest region, half-open | Use |
| --- | --- |
| `[0,0x10000)` | Invalid/null guest addresses; enforce checks in the experiment. |
| `[0x00400000,0x0125b000)` | Existing Blur image when integration is reached; synthetic EXE initially occupies its own PE-sized range. |
| `[0x02000000,0x0e000000)` | Existing guest heap, unchanged. Additional thread stacks/TEBs/TLS consume heap allocations. |
| `[0x0e800000,0x0f000000)` | Main stack: runtime `guest.h` specifies 8 MiB; the older x86.h prose says 1 MiB. Use the constants. |
| `[0x0fdff000,0x0fe00000)` | PEB placeholder page and return sentinel `0x0fdfff00`; preserve existing arrangement. |
| `[0x0fe00000,0x0fe01000)` | Main TEB. |
| `[0x0fe01000,0x0fe01100)` | Main 64-slot TLS array. |
| `[0x0ff00000,0x10000000)` | Reserved shim region; current trampoline allocation uses its first 64 KiB. |
| `[0x10000000,0x100a9000)` | Candidate DLL image, entirely outside the unchanged heap. |
| `[0x100a9000,0x10100000)` | Unassigned headroom; do not allocate heap objects here. |

Check every end with 64-bit arithmetic, PE sections against SizeOfImage, and
all image/runtime reservations for overlap. `gm_valid` only proves arena bounds;
it does not prove allocation ownership, section permissions, or a valid object.
Use guarded fixture buffers and ownership checks. Verify GUEST_SIZE reaches every
runtime/generated translation unit (`CMakeLists.txt:106` defines it globally).
Growing the arena does not increase `GUEST_HEAP_END` or move the stack/TEB/shims.

**Relocations:** no rebase is needed for this one DLL at its preferred address.
Reject a conflicting base. Do not load the sample's `msvcrt.dll` alongside it;
that DLL also prefers `0x10000000`. A future relocation project must coordinate
PE relocation fixups, translated absolute operands, dispatch/table keys, exports,
TLS addresses and evidence provenance. Editing only TOML `base` or applying PE
fixups to data is not sufficient for static generated code.

**Initial dynamic probe:** use the existing auxiliary mechanism with a synthetic
EXE importing only necessary OS services, then explicit LoadLibrary/GetProcAddress.
Require recorded entry execution at the candidate's actual PE entry and exact
export targets before executing any CRT test. Treat skipped/untranslated nonzero
entry as failure even though the pinned kit returns success. No Blur rebind yet.

**Required static-import integration:** after that probe passes, develop and test
a small loader change in a separate kit experiment checkout/build. Map and register
all configured images before binding any IAT; bind code/data against the full
module registry; then snapshot initialized mapping state and establish dependency
order. Initialize OS services and the initial guest TEB/TLS before executing DLL
code. Attach dependencies before EXE TLS callbacks and the EXE entry. For the
first static fixture permit only this CRT plus native OS service dependencies;
reject unsupported cycles/extra guest modules instead of trusting sorted keys.

Keep explicit mapped/attaching/attached/failed states and static lifetime
references, and serialize initialization with guest scheduling semantics. Propagate
attach failure; do not fall back to CRT shims. Honor static versus dynamic attach
and unload versus process-exit reserved-argument semantics using guest values.
The initial dynamic probe's zero reserved argument cannot establish static attach
equivalence. These lifecycle distinctions are specified by
[Microsoft's DLL entry contract](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain).

The pinned loader's executable-first binding, one-at-a-time auxiliary binding,
silent missing-entry success, and absent static dependency attach need explicit
tests. A manual post-load IAT rewrite in a throwaway harness may diagnose a fault,
but is not the integration design and cannot justify marking Blur startup valid.

**TLS and teardown:** this candidate has no static TLS directory, so auxiliary
static TLS remains explicitly unsupported in the first probe and a nonzero
directory must fail preflight. Dynamic TLS still needs allocation/free, per-thread
values, LastError behavior and CRT cleanup verification. Before allowing worker
threads, implement/test DLL thread attach/detach notifications on the new/exiting
guest context, without double-notifying the initial process-attach thread. The
kit's synchronous thread shortcut borrows the caller context and is not evidence
of thread isolation. A later candidate with PE TLS needs per-module templates,
zero-fill/alignment, index allocation, per-thread blocks and TLS callbacks before
its PE entry; it cannot be accommodated by ignoring the warning.

Test dynamic FreeLibrary reference counting and reattach separately from static
process shutdown. Stop workers, dispatch CRT/EXE cleanup according to the tested
exit path, and notify modules in dependency-reverse order with OS services still
alive. Do not manually duplicate `_cexit` and DLL cleanup. Abort/forced-exit runs
do not establish clean shutdown. A fresh-process probe is the default until repeat
attach is verified, including heap, TLS, handle and callback reclamation.

## Smallest decisive experiment and gates

Create a separate, synthetic, no-CRT i386 executable, loosely patterned on the
sample. It initially uses only enough Kernel32 to load the candidate, resolve
exports and report results. All candidate CRT operations must go through guest
calls. Its source and assertions may be tracked; generated PE/C/listings and
reports must remain ignored. Do not regenerate or rebuild Blur for this stage.

Use milestone gates rather than asking whether the program gets farther:

1. **Preflight and strict translation.** Hash staged bytes; inspect imports,
   export types/forwarders, entry, PE/TLS/relocation directories and address fit.
   Audit all 143 imported names against the runtime registry, including ABI and
   function presence. Export candidate listings in an isolated database; seed
   entry/export/callback roots from this DLL's evidence. Run strict translation
   and record failures, recovery errors, dropped bodies, jump tables, unsupported
   operations and exact command line. No `--allow-table-gaps`,
   `--allow-unmodelled`, guessed success stubs, or edited generated C.
2. **First executable gate: attach and ownership smoke test.** Build only the
   small fixture plus DLL and required host runtime. Require real PE attach once,
   a successful return, no unresolved/unknown/interpreted execution, and balanced
   stack/nonvolatile registers. Resolve data/code addresses; allocate 32 bytes,
   write a pattern, grow it, verify preservation, query size and free through the
   DLL. Resolve `_acmdln` with correct indirection and inspect initialized `_HUGE`
   and `_adjust_fdiv` against an x86 baseline. Detach; ensure no callback replay or
   growing heap/handle/TLS count. This is the smallest useful test of the proposed
   owner and service boundary, not just `strlen` working without initialization.
3. **CRT boundary gate, same small executable.** Add arguments, FILE, locale,
   callback and failure-path tests below. Rebuild the small fixture incrementally.
   Add dynamic thread-state tests only after real thread notifications exist.
4. **Static-link gate, still synthetic.** A second fixture statically imports CRT
   functions and data. Apply the tested loader ordering/lifetime changes. Verify
   every CRT IAT target, DLL attach before any EXE TLS/initializer callback, and
   data/accessor identity. Include synthetic DLLs that deliberately fail attach,
   lack translated entry code, expose data, and have unsupported TLS/forwarders.
   Those cases must reject or follow a documented implemented path, never pass
   through zero-return fallbacks. A dynamic-only success cannot pass this gate.
5. **Limited Blur integration, only after gates 1–4.** In a separate build/profile,
   replace the CRT owner as a unit and trace the existing initializer traversal.
   Five C initializers must still succeed; record C++ callback progress and the
   first unsupported dependency. Completion requires reaching the end of that
   table through valid calls, not skipping failed callbacks. No menu/game claim.

### Tests needed for the CRT boundary gate

| Family | Concrete comparison and invariant |
| --- | --- |
| Calling ABI | cdecl, stdcall, varargs and relevant thiscall/special math conventions: argument layout, return channels (including x87/SSE), stack and nonvolatile registers. Measure raw guest calls as well as wrapper calls because `guest_call` restores ESP and can hide cleanup errors. |
| Heap | Zero/overflow/exhaustion, realloc failure preserving original data, several alignments with matching aligned free, calloc zeros, returned addresses within arena, guards unchanged. Ensure HeapDestroy and detach reclaim their owned blocks. |
| Arguments/data | Quoted and empty arguments, controlled environment; `_acmdln` variable and accessor identity where exported; writable mode globals shared with accessors; initialized scalar widths/values. Classify all 204 imports by actual ABI, not name prefix. |
| FILE | `__iob_func` identity and x86 field layout as consumed by fixtures/Blur; open a synthetic file in isolated scratch space, write/flush/seek/read, EOF/error/clear and close; `_fileno`, descriptor ownership, binary/text modes and standard streams. Compare observable behavior and required layout, not host FILE bytes. |
| Locale/ctype | `setlocale("C")`, locale accessors, character tables, unsigned-char/EOF cases, code page and `localeconv`; then one specified non-C case and per-thread locale isolation. Do not accept arbitrary NLS zero results. |
| Pointer/callback state | Encode/decode round trips for full-width values, `__dllonexit` resizing/failure, guest callbacks in order, `_onexit`/cleanup LIFO without replay; `qsort` comparator and varargs formatting as additional call-boundary probes. |
| Threads | Separate errno/locale/TLS for two real guest contexts, TLS slot reuse, locks under scheduler contention, `_beginthreadex` creation and normal cleanup. Avoid host mutexes that block while holding the guest baton. |
| FP and exceptions | Query/change/restore `_controlfp_s`, representative conversion and imported math functions, rounding/NaN/infinity behavior; controlled VC90-compatible C++ throw/catch with destructor unwinding, SEH cleanup, and setjmp/longjmp. Explicitly reject unsupported behavior; do not replace failures with neutral returns. |

The FP/EH tests are a separate acceptance gate before broad C++ startup use. A
small allocation/FILE success may justify continuing investigation, but does not
license game-wide FP or exception claims. The pinned `runtime/x86.h` models x87
registers as `double`, not 80-bit, and `runtime/seh.cpp:180` rejects
ExceptionContinueExecution. Lifting a CRT preserves neither missing CPU semantics
nor an unimplemented exception disposition automatically. Use an actual compatible
VC90 test fixture for its exception ABI; a different compiler's exception model
is not an adequate substitute.

### Dependency work budget and stop criteria

Initial source scanning found no literal non-null NTDLL handlers for the 12
candidate names, and substantial Kernel32 gaps. This is a conservative source
scan, not the authoritative runtime registry audit. In particular inspect:

- Kernel32 EncodePointer/DecodePointer, InitializeCriticalSectionEx, heap flags
  and ownership, command line/environment, NLS/code-page APIs and file/console
  services. A registered handler is not a correctness result.
- `NtQueryInformationFile`, `NtQuerySystemTime`, bitmap helpers,
  `RtlTimeToSecondsSince1970`, and `RtlUnwind`. An existing Kernel32 RtlUnwind
  implementation does not automatically implement the NTDLL import or establish
  unwind parity; verify the contract before sharing it.
- `__wine_dbg_get_channel_flags`, `__wine_dbg_header`, `__wine_dbg_output`,
  `__wine_dbg_strdup`: establish calling conventions, returns and string lifetime
  from source matching the candidate build and/or its bytes. An explicit disabled
  logging policy is acceptable only with verified semantics, not a blanket zero
  stub. Host pointers must never leak through debug string results.
- Runtime/delay-resolution helpers and dynamic loads: record resolution requests;
  fail an unexpected module or unresolved request. Do not recursively lift Wine's
  Kernel32/NTDLL or load Apple's incompatible host libraries as guest modules.

Proposed bound: one candidate; up to two evidence-backed listing-repair rounds;
one initial strict fixture build; up to two incremental runtime-fix rounds,
adding at most eight narrowly specified OS service functions total. These are
project limits, not estimates of compatibility. Run each fixture case with an
external 10-second watchdog and an explicit callback/iteration budget; use fresh
processes and three clean repetitions for a passing milestone. Known-good host
services still need tests for this caller. If the limit is exceeded, record the
blocking dependency set and revisit the route instead of continuing indefinitely.

**Stop immediately or fail the current gate** on a hash mismatch, unexpected
module, arena collision, relocation need, unsupported auxiliary static TLS,
skipped PE entry, unresolved import, unknown dispatch, interpreter fallback,
unmodelled instruction, corrupt guard, foreign-owner free, ABI/stack mismatch,
attach failure, unexplained leak, timeout, or differential mismatch. Instrument
strict failure at the point of use before execution; log scraping alone is too
weak because the kit can continue after missing calls. Unreached import gaps
remain recorded obligations, not silently implemented APIs.

If strict whole-DLL translation fails, diagnose bytes/listings first. Repair only
evidenced flow or add a small instruction semantic with differential tests, within
the bound. Otherwise reject this candidate on the pinned strict path. A separately
proposed root-limited translation could answer a narrower research question, but
must have a closed, enforced call boundary and cannot masquerade as whole-DLL
coverage. No permissive flags are part of this plan.

**Success** at the first executable gate means a genuinely attached, translated
DLL serves the verified allocation/data path without hidden fallbacks and tears
down correctly. **Success** for the architectural recommendation requires the
CRT boundary and static-link gates, plus a viable independent ABI baseline and
an explicit FP/EH disposition. Name coverage, emitted C, a linked ARM64 binary,
or a later abort address is not an acceptance criterion.

## Ordered implementation and validation plan

1. Preserve baseline hashes/configuration and existing successful reports. The
   repository has no commits and many untracked files: no clean/reset/stash-based
   migration. Use an additive fixture directory and separate ignored output root.
   Keep `local/recomp-kit` pinned and clean. If loader changes are required, make
   a separate ignored checkout at the pin and retain reviewable source patches
   with their hashes; do not describe the patched build as the unmodified kit.
2. Stage the candidate read-only source into an ignored experiment directory,
   verify before/after hashes, record CrossOver version/build provenance and
   applicable Wine/third-party notices. Keep PE files, databases, listings,
   generated C, builds, profiles and reports under `local/` or `reports/`.
   Never write the installed game or use it as a writable runtime root. Keep
   sample/experiment build roots separate to avoid replacing Blur's generated code.
3. Add preflight and evidence manifests: input SHA, kit pin plus patch hashes,
   tool versions, config hash, import/export ABI inventory, arena map, listing
   provenance and exact strict flags. Use synthetic PE fixtures for parser and
   loader tests, including malformed ranges and binding-order cases.
4. Execute the strict translation and dynamic attach/ownership gates above.
   Capture events before a crash: current module+RVA, import name, arguments,
   result, EIP/ESP, init phase, allocation/TLS/handle counts, and fallback counters.
   Verify that each code export and the PE entry used by the fixture has generated
   dispatch coverage. Entry lookup by module range alone is not enough.
5. Establish differential baselines before accepting semantics. Run the same
   synthetic x86 fixture against the exact candidate in an independent compatible
   x86/Wine or Windows environment. Compare defined registers/results, error
   state, buffers, field layouts actually consumed, callback order and lifecycle;
   normalize addresses as offsets/identities rather than matching ASLR or cookies.
   Unicorn is useful for pure instruction kernels, not an oracle for DLL loader,
   FILE, locale, OS or C++ runtime behavior.
6. Separately compare the ABI boundary against a hash-pinned compatible Microsoft
   VC90 runtime on Windows. The installed `_Redist/vcredist_x86.exe` is only a
   possible source; inspect/extract an ignored copy without running its installer
   or altering the installation, and record the selected DLL version/architecture/
   hash. Do not assume the redistribution package or CrossOver DLL matches Blur.
   If this oracle is unavailable, label compatibility unresolved and keep the
   result a feasibility result rather than approve broad Blur integration.
7. Complete the CRT boundary tests and required small native OS services. Then
   implement static import binding/init/lifetime with synthetic tests, build the
   static fixture, and prove it uses only one CRT owner. Run threading and FP/EH
   acceptance before enabling corresponding Blur paths.
8. Only then create the separate Blur DLL configuration/build, ensure every CRT
   IAT slot has the intended binding, and run a bounded initializer probe. Compare
   against the existing five-C-initializer checkpoint and record C++ table outcome.
   Preserve the adapter baseline for diagnosis; do not promise rendered output.
9. Publish a decision update recording which gates passed, unresolved ABI/OS
   requirements, observed paths and remaining recovery errors. Select the route
   based on measured work and parity, not an import count.

After any future code changes, run the required
`python3 -m unittest discover -s tests -v` and the normal CMake build, plus the
synthetic startup-adapter checks so the fallback does not regress. For changes
to the experimental kit, follow its AGENTS.md and relevant test suites, build
native targets through its `tools/build.py`/`tools/test.py`, and retain strict
translation reports. Use four build jobs initially, matching the established
workflow. Only broaden/repeat tests for changed behavior or unresolved failures.
Do not reuse the current boot wrapper's provenance check unchanged: a DLL run
must additionally validate candidate, listings/config, kit patches and generated
dispatch inputs before execution.

## Fallback and exit from the experiment

If the candidate fails the bounded gate, preserve the failure evidence and return
to the unchanged native-adapter configuration. Classify the failure first:

- **Wine-specific service burden:** inspect a hash-pinned Microsoft VC90 candidate
  read-only and compare required OS/loader services. A different DLL is a new
  bounded experiment, not an unrecorded substitution or a second simultaneous CRT.
- **Listing/translator issue:** repair a small evidenced defect with differential
  tests, or stop if unsupported instructions/SEH require a substantial kit project.
- **Ownership or OS fidelity issue:** fix the service boundary if within budget;
  do not replace isolated CRT allocators/FILE/locale functions to hide it.
- **ABI mismatch or unavailable independent validation:** retain a feasibility-only
  conclusion. Use the current adapters for narrowly specified work with explicit
  limitations, while obtaining the needed original-runtime evidence.

If native CRT becomes the selected route, design it as one coherent guest runtime:
arguments/data accessors first, then a verified FILE/descriptor family, then C
locale/ctype and thread state, with matching allocation, encoding, callbacks and
teardown. Add one differential slice at a time; keep unsupported calls fatal.
This fallback has a larger long-term maintenance cost and cannot claim VC90
compatibility from synthetic tests alone.

## Work performed for this plan

Only this document was added. Required project inputs, the pinned kit loader,
configuration/build seams, Wine sample, runtime memory/import/thread/heap paths,
and existing local reports were read. Candidate PE metadata and the two input
hashes were checked read-only; the Windows contract sources linked above were
consulted. No DLL was staged, translated, loaded, or executed. No game/source
architecture changes, long translation, build, or test run was performed.
AGENTS.md's post-code-change checks are not triggered by this documentation-only
change; previously reported checks remain historical evidence.
