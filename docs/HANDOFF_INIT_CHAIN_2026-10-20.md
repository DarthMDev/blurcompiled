# Initialization-stage audit — October 20

Read AGENTS.md and HANDOFF_FIELD_AUDIT_2026-10-20.md first. Game executable
SHA-256: dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f.
Diagnostic host remains d65e2847e3c15468bb5aec92824ba2a22a2472a98059aca890a63d86671718e4.

## Observed result

`local/init-chain-20261020/probe/result.json` preserves all previous 505
libraries and appends the read-only stage observer: **506 libraries total**.
All hashed inputs remained unchanged. Strict execution ended at the host watchdog
(exit 4) after 395.32 seconds, without outer timeout, signal or recovered-code
trap. Frames remained uniform. Menu/racing and whole-function parity are unverified.

The parent initialization VA 00422c40 / RVA 00022c40 calls VA 007098c0 /
RVA 003098c0 at VA 00422de3 / RVA 00022de3. Its observed return is
EAX 88760800, AL zero. The parent follows its common failure exit before
VA 00418940 / RVA 00018940 and VA 00429150 / RVA 00029150 are reached.
The earlier missing field initialization therefore has a directly observed
preceding failure; scheduler/publication races are not established as its cause.

The existing volume observer identifies the failed request through VA 005c2730 /
RVA 001c2730. The actual API request is width 8, height 8, depth 3, levels 1,
usage 0, format 116, pool 1, no shared handle. The wrapper's internal 00040000
flags are not API usage flags. Native CreateVolumeTexture returns 8876086a.
The game wrapper clears AL on failure, explaining 88760800; do not interpret
that final value as the original API HRESULT.

These instruction identities and call/guard edges are pinned in
`local/init-chain-20261020/ledger.json`; high confidence for static identities
and the observed failure path, not for whole-function behavioral equivalence.
The parent success path creates its resource at VA 00422ed2 / RVA 00022ed2,
stores it at parent+88 at VA 00422eed / RVA 00022eed, and later calls the
initializer at VA 004230c5 / RVA 000230c5. Those later stages were not reached
in this probe.

## Implementation next

The current CPU-only volume adapter accepts formats 21,22,50; format 116 is
rejected. A separate source snapshot/build under `local/volume-float-20261020`
adds bounded 16-byte texel storage and synthetic LockBox round-trip coverage.
It reuses unchanged baseline game/CRT archives. Neither earlier host is edited.
This is new adapter implementation, not recovered guest logic or GPU volume
sampling. Preserve all 506 libraries for its next combined strict probe.
