# RTL8372N P1-B source work report

Updated: 2026-10-07. This follow-up is based on the rebased P1-A source in
[PR #1](https://github.com/MNeroba/openwrt-flint3/pull/1), whose parent is
[P0 PR #104](https://github.com/perceival/openwrt-flint3/pull/104). This is a
hardware-test candidate, not a BE9300 validation report.

## Implemented source scope

The candidate now adds a narrow, testable STP/control-frame path:

| Item | Source behavior | What still needs hardware validation |
| --- | --- | --- |
| CIST state | Maps Linux disabled/listening/blocking/learning/forwarding to the RTL8372N two-bit port state; reads back each write | Field packing, actual data-path effect, state changes on ports 4–7 |
| Fast-age | DSA `port_fast_age` calls a serialized, bounded per-port dynamic L2 flush; flush mode is restored after completion | Dynamic entries are removed, static entries survive, and timeout/recovery behavior is safe |
| BPDU route | Setup installs and reads back an IVL static-L2 multicast entry for `01:80:c2:00:00:00`, VID 1, targeting the single DSA CPU port | CPU RX, ingress-port attribution, RTL8_4 reason/forwarding mark, no LAN egress, delivery while the ingress port is blocking |
| Startup state | Active user ports are set to CIST forwarding before PHY registration; the P0 CPU-only isolation matrix remains in place | Software-forwarded baseline remains intact and no user-to-user hardware path bypasses the CPU |

The static L2 multicast route is an **unverified alternative** to a reserved-
multicast trap. RTLPlayground documents this technique on other boards whose
CPU is an embedded MCU. That does not establish its behavior with the BE9300's
external CPU port 3 or the RTL8_4 tagger. See the detailed maintainer procedure
in [P1-B-TEST.md](P1-B-TEST.md). No physical loop is needed or allowed for the
first BPDU check. This candidate does not change the global reserved-
multicast action; its reset default and precedence against the static entry
must be recorded during the hardware test.

This remains a P0-style CPU-forwarding configuration. It does **not** enable
hardware bridge forwarding, user-configurable VLAN/PVID callbacks, bridge
join/leave, bridge flags, FDB/MDB offload, LAG or rate limiting. BPDU routing
is programmed for VLAN 1 only because general VLAN/PVID offload is absent.
Do not claim full STP or bridge offload from the new DSA hooks.

## Transaction and failure behavior

L2 table staging, command execution, completion polling, status checks and
readback share `table_lock` with VLAN-table operations. Polling is bounded.
The BPDU entry is accepted only if the L2 hit status is set and all three
readback words match the requested CPU-only entry. The helper restores the
previous L2 lookup method after the readback; a failed restore is reported.

CIST writes are read back and mismatches return an error. DSA's STP and
fast-age callbacks are void, so failures are logged. Setup fails closed if the
BPDU entry or initial CIST state cannot be programmed; its cleanup isolates
all switch ports.

The dynamic flush helper saves `0x53dc`, requests the documented dynamic-only
per-port mode, submits one port at `0x53d4`, waits up to one second for command
completion and restores the prior mode. If the engine may still be active, it
does not race a restore against that engine; this needs review against actual
hardware behavior.

## Provenance boundary

The L2 multicast entry layout, table registers and operation facts were
compared with [RTLPlayground](https://github.com/logicog/RTLPlayground/tree/f0aea3dcac056e3274fd39e1c76a7117471c37da)
at pinned commit `f0aea3dcac056e3274fd39e1c76a7117471c37da` (MIT repository;
its STP function is marked public domain). L2 hit/method fields were also
compared with the public [ZTE RTL8372N driver](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c)
at pinned commit `07f8687248578d4be6931c665ff5d08bb6cc3d9d`. The new Linux
transaction code is written for this candidate; no function body was copied.
The references do not prove independent origin of every fact or BE9300
semantics. Existing Air/ZTE lineage and SDS questions remain open in
[PROVENANCE.md](PROVENANCE.md); do not describe the complete candidate as
provenance-clean.

## Validation status

| Gate | Status |
| --- | --- |
| Source review / `git diff --check` / checkpatch | PASS |
| Exact-source kernel/package build | Not yet run; local build environment is blocked before compilation |
| P0 T6 VLAN/isolation readback | Not run on BE9300 |
| BPDU CPU delivery and no-egress capture | Not run on BE9300 |
| CIST state transitions and readback | Not run on BE9300 |
| Fast-age removes dynamic entries and preserves static BPDU route | Not run on BE9300 |

The local OpenWrt build previously stopped in prerequisite checks: this macOS
checkout is on a case-insensitive filesystem, lacks required GNU build tools,
and has no `.config` or target build tree. Apple `make` is too old; `gmake`
did not pass prerequisite checks. No source compilation was reached. The
candidate needs an exact-source Linux CI build before installation.

## Next gate

Run [P1-B-TEST.md](P1-B-TEST.md) on an isolated BE9300 test setup. First confirm
P0 initialization/readback, then test BPDU CPU reception without a physical
loop, STP state callbacks and fast-age. A failed BPDU reason/bridge reception
or any user-port egress blocks P1-C hardware bridge/VLAN offload. LAG (#47) and
rate limiting (#49) remain later P2 work.
