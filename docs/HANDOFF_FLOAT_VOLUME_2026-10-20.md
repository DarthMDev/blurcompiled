# Bounded float volume storage — October 20

Read AGENTS.md and HANDOFF_INIT_CHAIN_2026-10-20.md first. The early initialization
failure now has a narrow native adapter implementation to test. This does not
establish main-menu or racing reach.

Game SHA-256: dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f.
New host SHA-256: 494c0709f528f765155ee2d651b2adba541d775fdfe789fb5b524d3511c88938.
The previous diagnostic and original pinned hosts remain unchanged.

## New implementation

`local/volume-float-20261020/kit/dx/d3d9.cpp` extends the existing one-level
CPU-only volume adapter with format 116 (16 bytes per texel). Dimensions remain
bounded to 256 per axis, usage zero, levels one, no shared handle. Whole-level
LockBox still requires null box and zero flags. Existing formats 21/22/50 keep
their byte sizes. The observed 8x8x3 request therefore has row pitch 128,
slice pitch 1024 and total storage 3072 bytes. GPU volume sampling and separate
IDirect3DVolume9 views remain unsupported.

This is new native shim implementation, not recovered game behavior.
`source-build-record.json` in that folder pins all 662 copied source files,
the parent source record, unchanged imported game/CRT archives, build script and
new host. `native.patch` records the two changed native files. Build used the
kit formatter and tools/build.py API; recovered guest archives were not rebuilt.

## Validation

- Focused synthetic CPU volume suite: 856 checks, zero failures; all 768 dwords
  across three float-volume slices survived unlock/relock. Descriptor, pitches,
  unsupported format, levels and usage rejection also checked.
- SEH, strict CRT, intrinsics and headless native CTests: four passed.
- Full DX suite: 142003 checks, six pre-existing cursor failures at lines
  2765,2770,2774,2781,2790,2797. It is not a green suite.
- Root Python: 96 tests, one explicit skip; root CMake build and four CTests pass.

Logs: `local/volume-float-20261020/{build,volume-tests,native-tests,dx-tests}.log`
and `reports/volume-float-{unit,build,ctest}.log`.

## Integration

`local/volume-float-20261020/run.py` preserves all 506 libraries from the
completed initialization-stage probe, both earlier hosts and their input hashes.
Only the selected runtime host changes. Strict mode, read-only asset root,
360-second/180000-frame cooperative limits and 430-second outer timeout remain;
frame files are sampled every 600 frames to control disk use. Use a fresh probe
folder and verify result.json before treating it as a new checkpoint.

The completed strict probe ended after **141.83 seconds**, exit 6 / logged
signal 6, at deliberate unmodelled CVTPS2PD VA **00458c96**, RVA **00058c96**,
pinned bytes **0f5ac0**, operands XMM0,XMM0. All input hashes stayed unchanged;
all **506** libraries were preserved. Its result.json is the latest full list.
The report parser's uniform_frames=false is not proof of nonblack output;
this trap exits before a normal frame summary.

Observed stage exits now show both 8x8x3 format-116 creations succeeding,
VA 007098c0 returning AL=1, VA 00418940 returning AL=1, child+1c becoming
0897ef70 (matching parent+88), child+48 becoming 1, and parent VA 00422c40
returning AL=1. Runtime addresses are observations specific to this run. The
previous invalid-pointer fault did not recur before this new trap; its unknown
nonzero value's provenance remains unresolved. No whole-program parity claim.

## Next ownership audit

Baseline chunk_006.c names owner VA 00457d90 / RVA 00057d90 with 4249
instructions, 51 alternate entries (52 including the primary) and end VA 0045c60e / RVA 0005c60e.
The repaired listing contains 4243 instructions and ends at RET VA 0045c350 /
RVA 0005c350. Baseline additionally includes suffix code at VA 0045c5ed,
0045c5ff and 0045c609 (RVAs 0005c5ed,0005c5ff,0005c609). That discrepancy
needs independent ownership evidence; neither silently discard the suffix nor
raise a bound solely because the baseline header includes it. The main listing
alone already exceeds the current 16 KiB emitter cap. Thirteen known CVTPS2PD
sites belong to this baseline owner. Audit them as a batch before another probe.
Local preliminary evidence: `local/owner-00457d90-20261020/`.

Independent read-only Ghidra ownership query confirms the reached trap and RET
belong to primary 00457d90; all six disputed suffix boundaries are unowned.
That alone does not prove they are a separate original function. The primary
extent through RET is 17857 bytes, with 51 alternate entries (52 including
primary). `ownership.txt`, `boundary-delta.json`, and `ghidra-command.json`
preserve the evidence. Manual reasoning prompt: `deepseek-prompt.txt` in the
same local audit folder. Review ownership/preservation before changing bounds.
