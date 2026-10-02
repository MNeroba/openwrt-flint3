# RTL8372N P1-A VLAN table foundation

Updated: 2026-10-03. This implementation is stacked on [P0 PR #104](https://github.com/perceival/openwrt-flint3/pull/104), base `c27cfb20f04b31c0f8fa35bdef5cb4d2836fcd8f`. The broader plan remains [Issue #100](https://github.com/perceival/openwrt-flint3/issues/100) and [P1-RESEARCH.md](P1-RESEARCH.md).

## Implemented scope

| Change | Behavior / purpose |
| --- | --- |
| Shared table mutex | Protect idle, data staging, command, completion and readback as one operation; future L2 operations must use the same mutex |
| VLAN read | Read one raw table word for VID 1–4094; reject invalid VID/null output before I/O; publish output only on success |
| VLAN write | Check VID and untag/member relationship; wait before staging; execute; read the same VID and require exact word equality while retaining the mutex |
| Port-mask codec | Reject values outside the documented 10-bit fields and untag bits outside membership; preserve every other bit of the input word |
| Bootstrap integration | VLAN 1 uses the new codec/write path; expected data word and write command match #104; new idle checks and readback can fail setup |
| Neutral bit-25 name | Rename `VLAN_DATA_VALID` to `VLAN_BOOTSTRAP_FLAGS`; keep the raw bit set as before, without claiming a proven validity or IVL meaning |
| Build plumbing | Add `rtl837x_table.o` to the module; bump package release to 4; enable existing ARM64 build workflow for `rtl8372n-p1-tables` |

This is a concrete first part of P1-A. It does **not** implement general DSA
VLAN callbacks, L2 lookup/hit/delete/iteration, FDB/MDB, hardware bridge/STP,
BPDU trapping, bridge flags, LAG or policing. The only runtime writer remains
the VLAN 1 setup path. No user-facing VLAN offload is advertised.

## Transaction and failure contract

1. Validate arguments before register access.
2. Acquire `table_lock`; wait for execute bit 0 at `0x5cac` to clear.
3. For a write, stage the single VLAN word at `0x5cb8` and submit selector 3 /
   VID / WRITE / EXECUTE through `0x5cac`.
4. Wait for completion; submit a selector-3 read for the same VID; wait and
   read `0x5ccc` without releasing the lock between write and verification.
5. Return the first transport/poll error or `-EIO` for a readback mismatch;
   release the mutex on every exit. Each idle/completion wait retains the
   existing 10 us poll interval and 1 ms budget. Budgets are per wait, not a
   hard deadline for a whole transaction or a parent MDIO transfer.

Lock order is **table engine → regmap/map lock → parent MDIO lock**. Table
access does not nest PHY/SDS engine locks or manually hold regmap's map lock
or the parent MDIO lock. The locked read helper asserts its table-lock contract.

A timeout after command submission can leave a committed or still-busy hardware
entry. No speculative inverse write is attempted. The bootstrap caller returns
the error into existing setup cleanup, which best-effort isolates ports and
powers down PHYs; probe cleanup also asserts the managed reset when available.
That cleanup and reset behavior still require hardware validation. Future
runtime VLAN/bridge callbacks need their own state/rollback policy before use.

## Source disposition

The new file is an original GPL-2.0-only implementation using the table layout
published in MIT-noticed [RTLPlayground `rtl837x_regs.h`](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_regs.h)
and the selector/readout operations in [`rtl837x_port.c`](https://github.com/logicog/RTLPlayground/blob/f0aea3dcac056e3274fd39e1c76a7117471c37da/rtl837x_port.c).
The existing MIT notice is retained. No restricted generated header, SDK source
or PHY/SerDes patch data is added.

| Definition / operation | Specific source | Disposition |
| --- | --- | --- |
| Control `0x5cac`, VID in upper half, selector in the next byte, execute bit 0/write bit 1 | `rtl837x_regs.h` table-access comment and `TBL_*`; `vlan_get/create` command bytes | Explicit field packing; selector 3 follows code, not the conflicting selector-2 prose |
| Write `0x5cb8`, read `0x5ccc` | `RTL837x_TBL_DATA_IN_A`, `RTL837x_L2_DATA_OUT_A`; `vlan_get/create` | One VLAN word; no L2 status/method interpretation introduced |
| Member bits 9:0 / untag bits 19:10 | `vlan_create` packing and `doc/vlan.md` | Checked codec; no board CPU-9 policy imported |
| Raw bit 25 | Parent P0 bootstrap, corroborating packing in `vlan_create` | Preserved opaquely; no validity test or IVL/FID API built around it |
| Mutex, bounds, error handling, verification and preserved-field codec | New implementation | Exact readback is a fail-closed policy, not a proven hardware guarantee |

The unresolved VLAN-bit-25 and L2-bit-29 interpretations and the parent's
retained SDS/source-lineage review remain open. This PR neither resolves them
nor authorizes importing additional source. Full-word readback may expose
hardware normalization/reserved-bit differences; report the written/read values
rather than weakening the comparison without evidence.

## Build and review evidence

| Gate | Status | Scope |
| --- | --- | --- |
| Whitespace | PASS | `git diff --check` on this implementation |
| New source style | PASS | Linux 6.18 `checkpatch.pl --no-tree --strict --file`; 0 errors, warnings or checks |
| Complete patch style | 0 errors, 1 reviewed warning | New-file MAINTAINERS reminder; this is an OpenWrt package, not a new in-tree Linux registration |
| Current ARM64 module build | PENDING | A new exact-revision build is required; #104's four-object artifact does not cover this five-object module |
| Current OpenWrt package/image | NOT RUN | Parent workflow/artifacts do not cover the modified source and release 4 |
| Runtime/codec tests | NOT RUN | No automated suite or fault-injection result is claimed |
| BE9300 hardware | NOT RUN | No previous SDK-driver or P0 result is attributed to this change |

Build results, exact source SHA and artifacts will be recorded here before
claiming compilation for this revision. A successful mainline module build
would cover APIs/linking, not OpenWrt packaging or switch behavior.

## Required first-device follow-up

Use [FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md) for the **new image
revision**, beginning with T0. The inherited #104 build result is not its T0.

- Confirm VLAN 1 bootstrap completes and no `VLAN ... readback mismatch`, table
  timeout, lock warning or setup failure appears. Record the VID/word in any
  mismatch; capture complete dmesg/pstore and silicon revision.
- Confirm member/untag/PVID readbacks via the agreed read-only method. The
  expected configuration is still #104's used ports, including CPU, untagged
  in VLAN 1; this change does not introduce trunk or bridge VLAN support.
- Repeat P0 jack/link, CPU traffic, standalone negative forwarding, software
  bridge lifecycle and warm/cold reset checks. Added table readback must not
  be treated as evidence of packet forwarding or BPDU correctness.
- If busy, transport failure or mismatch can be reproduced, record elapsed
  behavior and the existing setup cleanup/reset outcome. Use a controllable
  bench and the documented recovery path; no unreviewed write/debug interface
  is introduced to force those failures.

Next P1 work still needs L2 status/method/capacity/delete semantics, BPDU CPU
routing, CIST/fast-age and a database-aware bridge/VLAN design. The new table
mutex is the lock future table users must share; it is not proof that those
features are already supported.
