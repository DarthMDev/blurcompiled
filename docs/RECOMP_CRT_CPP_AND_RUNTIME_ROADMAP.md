# Recomp CRT C++, Startup Runtime, and Network/Presence Architecture

Planning and architecture roadmap, 2026-10-17.
This document synthesizes verified empirical progress from the recompiler experiments with
architectural blueprints for full native execution on macOS, private server (Amax) support,
and native Discord Rich Presence.

---

## 1. Executive Summary & Verified Milestones

### Verified Empirical Milestones
1. **Microsoft VC90 Runtime Passes Static Load-Time Binding on Apple Silicon (2026-10-15)**:
   - Executable SHA-256: `ed0170d3de86da33e02bfa1605eec8ff6010583481b1c530843867c1939d2185` (exact SxS `9.0.21022.8` matching `Blur.exe`'s embedded manifest).
   - Patches: `recomp/patches/msvc90-static-binding.patch` and `recomp/patches/msvc90-runtime-static-r1.patch`.
   - Real DLL attachment executes before EXE entry; dynamic heap (`malloc`, `free`, `_msize`), binary `FILE` stream I/O, and C-locale match CrossOver 26.3 reference observations.
2. **Freestanding C++ Exception Probe Blocker Resolved**:
   - In `local/recomp-crt-msvc90-eh-r1`, linking `exception_probe.cpp` halted on `undefined symbol: const type_info::'vftable'` (`??_7type_info@@6B@`).
   - **Working explanation (ABI parity unverified)**: In MSVC 9.0, `msvcr90.dll` exports `type_info::~type_info()` (`??1type_info@@UAE@XZ`) and methods, but `??_7type_info@@6B@` is statically linked into PE modules from `msvcrt.lib` (`ti_inst.obj`).
   - **Unverified claim in the prior notes**: the game supplies an equivalent type-info vtable. No hash/RVA-backed recovery record or executed exception-parity result was found in this review; do not use this claim as an acceptance gate.
   - **Observed local artifact**: `local/recomp-crt-msvc90-eh-r2/original/exception-probe.exe` and a handwritten type-info shim exist. Linking does not verify ABI conformance, exception execution or unwind parity. Those gates remain open.
3. **Core Repository Integrity**:
   - 59 Python tests pass (`python3 -m unittest discover -s tests -v`).
   - CMake builds (`blur-native`, `blur-asset-probe`, `blur-startup-tests`) pass all default and opt-in CTests.

---

## 2. Transitioning Blur.exe to Lifted MSVCR90 & C++ Initializers

### Critical Pitfall: Split-Brain CRT State
Handwritten adapters in `recomp/native/startup.cpp` previously supplied narrow stubs for 26 CRT functions.
Retaining these while invoking the lifted `msvcr90.dll` leads to mismatched heap arenas, corrupt `_ioinit` streams,
and uninitialized per-thread data (`_tiddata`).

**Action**: Unify CRT ownership under the lifted `msvcr90.dll`. Retire or forward duplicate C stubs in `startup.cpp`
so that both C initialization (`.CRT$XIA`..`.CRT$XIZ`) and C++ initialization (`.CRT$XCA`..`.CRT$XCZ`) operate on
the exact same CRT state.

### Win32 / NT Kernel Services Required Before `.CRT$XCA`..`.CRT$XCZ`

| Subsystem | Required Before C++ Init? | Implementation Requirement |
| --- | --- | --- |
| **Implicit Static TLS (`FS:[0x2C]`)** | **Yes (Mandatory)** | If `Blur.exe` references `__declspec(thread)` variables, constructors access `TEB->ThreadLocalStoragePointer` (`FS:[0x2C]`), indexed by `_tls_index`. Allocate `SizeOfZeroFill + (EndAddress - StartAddress)` and point `FS:[0x2C][0]` to the initialized template. |
| **FLS / Dynamic TLS (`TlsAlloc`)** | **Yes (Mandatory)** | `msvcr90.dll` stores per-thread data via `_getptd()`. Any static constructor touching locale, `errno`, or formatting queries this slot. |
| **Guest Stack & SEH (`FS:[0x00]`)** | **Yes (Mandatory)** | Static constructors emit `_EH_prolog3` / `__SEH_prolog4`, reading and writing `FS:[0x00]`. Initialize `FS:[0x00] = 0xFFFFFFFF`, set `FS:[0x04]` / `FS:[0x08]` stack limits, and point `FS:[0x18]` to the TEB itself. |
| **Recursive Critical Sections** | **Yes (Mandatory)** | Subsystem singletons instantiated in `.CRT$XCA` call `InitializeCriticalSection[AndSpinCount]` and acquire locks recursively on the main thread. |
| **PE TLS Callbacks** | **Conditional** | Run `IMAGE_DIRECTORY_ENTRY_TLS.AddressOfCallBacks` if present; MSVC 9.0 game builds typically have a null or empty array. |

### Canonical Startup Execution Sequence (`startup.cpp`)
1. **Initialize Guest TEB**: Set `FS:[0x00] = 0xFFFFFFFF`, establish stack bounds (`FS:[0x04]`, `FS:[0x08]`), self-pointer (`FS:[0x18]`), and static TLS pointer array (`FS:[0x2C]`).
2. **Execute DLL Attachment**: Invoke `msvcr90!DllMain(hModule, DLL_PROCESS_ATTACH, NULL)` to run `_CRT_INIT` (allocating `_crtheap`, PTD tables, and stdio streams).
3. **Run Blur TLS Callbacks**: Execute any non-null callbacks from Blur's TLS directory.
4. **Execute C Initializers**: Invoke `msvcr90!_initterm_e(__xi_a, __xi_z)`.
5. **Execute C++ Static Constructors**: Invoke `msvcr90!_initterm(__xc_a, __xc_z)`.

---

## 3. Network and presence follow-up (unverified design)

Winsock and native Discord IPC are potential platform boundaries, as described
in [AMAX.md](AMAX.md). This checkpoint contains no recovered Amax authentication,
endpoint, ticket validation, presence application ID or state mapping evidence.
Earlier text in this document asserted account/ticket behavior and recommended a
signature-verification patch without executable or protocol evidence. Those
claims are withdrawn. DNS redirection alone does not implement authentication.
No network connection or signature bypass is part of the current startup slice.

Native Discord IPC remains a new implementation proposal. Validate the protocol,
application identity and recovered scene transitions before implementing it.

## 4. Review checkpoint, 2026-10-18

See [RECOMP_GEMINI_REVIEW.md](RECOMP_GEMINI_REVIEW.md) for observed startup results,
source caveats and the next bounded recovery. The local headless path remains
experimental: uniform presented buffers and runtime counters do not establish
rendering fidelity, a menu, exception parity or gameplay.
