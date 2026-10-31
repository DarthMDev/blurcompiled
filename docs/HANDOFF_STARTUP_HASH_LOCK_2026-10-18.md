# Startup hash and lock continuation — working checkpoint

Read AGENTS.md and the earlier startup/CVTPS2PD handoffs. The user authorized
continuing autonomously through startup blockers, with small verified steps,
read-only installation, reused host/build/logs and no generated instruction
patches. Work is still in progress toward movies/main menu.

## Completed source changes

- Existing CMPNLESS helper audited and lowered; CVTPD2PS register/m128 lowering
  shares the audited bits-taking helper. Synthetic cases cover alias/distinct
  registers, unaligned memory, NaNs, narrowing boundaries, upper lanes and flags.
- CMPXCHG8B lowering now uses the existing helper. A failed comparison publishes
  its result before yielding the cooperative baton. The old inline lowering
  could spin while another thread was parked between claiming the lock and
  finishing acquisition. A five-second-bounded child regression models the
  owner releasing the lock on a yield, alongside Unicorn instruction checks.
  The 95 instruction tests pass, including that contention case.
- Removed fake crypto hash shims and added actual portable streaming SHA-1,
  handles and query/lifecycle validation. See CRYPTO_HASH_SLICE.md. The fake
  sixteen-byte hash caused gamedata.pak rejection and an early quit request;
  actual SHA-1 removes that path in live probes, preserving game validation.
- Bounded tooling records retained runtime traps; supports explicit 8 KiB
  complete-function bounds, all original alternate entries, audited direct tail
  targets backed by baseline funcs.h, and explicitly enabled finite jump tables
  with all targets in the same complete listing. SEH, arbitrary indirect/open
  control flow and inferred/unbounded/external tables remain refused.
- Ghidra ExportObservedFunction supports audited separate tail entries so a
  callback thunk does not absorb its callee. Only the disposable DB under
  local/startup-native-r2/compare-step/observed-db was modified. Baseline Ghidra
  databases, exports, generated host instructions and installation are intact.

## Runtime evidence and current paths

Pinned executable SHA-256 remains
`dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f`.
The unchanged host is local/startup-native-r2/build/recomp/pop_headless, hash
`f287e9bcc2fe19b2cfddead09e4683196fec0e5e2f842f60324009efa80a1eb1`.
Only native runtime objects and small modules have been compiled; there was no
whole-image regeneration or host relink.

Working stage: local/startup-native-r2/compare-step/. All probes use strict mode.
The ignored run_probe.py combines the three earlier cvtps2pd-final modules, the
new module-4e0620, completed module-???????? directories, two explicit r2 modules,
and caller-supplied crypto/observer modules. Check its current list before reuse.
Always choose a fresh probe name. Example:

```sh
python3 local/startup-native-r2/compare-step/run_probe.py NEW_FRESH_PROBE \
  local/startup-native-r2/compare-step/crypto-build/crypto_startup.dylib \
  local/startup-native-r2/compare-step/import-watch/watch-r11.dylib
```

Read-only observer source: recomp/diagnostics/startup_import_watch.c. It chains
import observers, logs bounded movie/message/shutdown/string-copy observations,
and arms the existing host write watchpoint for the dynamically allocated
application state's +4 field. It never changes guest memory/results or refuses
an import. Early watch.dylib had a symbol-label load failure; later numbered
versions load correctly. Use a fresh build if modifying its source; preserve logs.

Observed startup now proceeds through resource loading beyond both the early
quit and lock deadlock. Additional already-tested CVTPS2PD sites and missing
callbacks were integrated in bounded modules. Missing callback 004d3780 (RVA
000d3780) has 39 Ghidra/Capstone-agreeing instructions. Callback 00bcba80 (RVA
007cba80) is a five-instruction thunk to baseline entry 00bca500 (RVA 007ca500).
Callback 005deca0 (RVA 001deca0) is a five-instruction thunk to baseline entry
005deb30 (RVA 001deb30). Entry identities are high confidence from strict reached
calls, unchanged PE bytes and disposable Ghidra exports; full behavior is not
claimed. Each module directory carries input/generation hashes and evidence.

The small function 00c60980 (RVA 00860980) has a finite six-slot table at VA
00c60da0 (RVA 00860da0), dispatch site 00c60b48 (RVA 00860b48). The decoder's
explicit bound and all six local targets are recorded in its r2 report; all ten
original alternate entries are retained. The earlier failed module directory is
preserved. No table gap was allowed or instruction edited.

The active stage changes as work continues. Consult the latest probe result and
module reports for the next exact failure. Bink opens have been reached, but this
host's video decoding remains OFF and no movie/main menu frame is established.
The observed Bink filenames were empty; that still needs investigation after
startup resource processing. Earlier framebuffer/rate layout gaps remain.

## Validation and reproduction

Native crypto build: local/startup-native-r2/compare-step/build_crypto.py invokes
kit tools/build.py on recomp/crypto. Its dedicated CTest passes. The actual kit
recomp_runtime target compiles with the new source and import registration.
Root tests/CMake/CTests and focused translator suites are logged in compare-step.
The prior final focused suites passed 95 instruction tests and 237 translator,
config, import, SEH and table tests. Re-run checks after new code changes.
Unicorn tests need unrestricted execution because its ARM cache query crashes
inside sandboxed memory mapping; they use synthetic fixtures only.

Focused kit delta: recomp/patches/startup-sha1-lock-reviewed-slice.patch, applied
after the previous startup-cvtps2pd-reviewed-slice.patch and older combined patch.
Root tooling, crypto verification project and diagnostics are ordinary sources.
This working handoff will be finalized with the latest evidence/provenance report.
No goal, automation, subagent, commit or PR was created.
