# RTL8372N P1-A VLAN table foundation

Updated: 2026-10-07. Stacked on [P0 PR #104](https://github.com/perceival/openwrt-flint3/pull/104).
Current parent source: 27103b8e6b705e838eada250f33231b2c1a8c045.
The broader plan is [Issue #100](https://github.com/perceival/openwrt-flint3/issues/100)
and [P1-RESEARCH.md](P1-RESEARCH.md).

Current implementation commit: 04128d1dbb19b547ab546c6032fbb5143d8c74e7.
Source tree: bf2b6955bb05ab6c82322d89d1069a733c345204. Package release: 8.

## Implemented scope

| Change | Behavior / purpose |
| --- | --- |
| Shared table mutex | Protect idle, data staging, command, completion and readback as one operation; future L2 operations must use the same mutex |
| VLAN read | Read one raw table word for VID 1–4094; reject invalid VID/null output before I/O; publish output only on success |
| VLAN write | Check VID and untag/member relationship; wait before staging; execute; read the same VID and require exact word equality while retaining the mutex |
| Port-mask codec | Reject values outside the documented 10-bit fields and untag bits outside membership; preserve every other bit of the input word |
| Bootstrap integration | VLAN 1 uses the new codec/write path; expected data word and write command match #104; new idle checks and exact readback can fail setup |
| Setup snapshot | Best-effort reads of VLAN 1, configured PVIDs, isolation, learning limits, flood masks and VLAN controls; logs raw state and known-field mismatches without failing probe |
| Neutral bit-25 name | Rename `VLAN_DATA_VALID` to `VLAN_BOOTSTRAP_FLAGS`; keep the raw bit set as before, without claiming a proven validity or IVL meaning |
| Build plumbing | Add `rtl837x_table.o` to the module; bump package release to 8; enable existing ARM64 build workflow for `rtl8372n-p1-tables` |

This is a concrete first part of P1-A. Snapshot read failures or mismatches are
reported for hardware diagnosis but do not fail probe. The exact table write
readback is stricter: a mismatch returns -EIO into the existing setup cleanup.
Neither policy has been tested on the BE9300 yet. It does **not** implement
general DSA VLAN callbacks, L2 lookup/hit/delete/iteration, FDB/MDB, hardware bridge/STP,
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
Separate public read and write calls do not make a read/modify/write pair
atomic; future shared membership updates also require a state lock.

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
| First rebased ARM64 attempt | FAIL, fixed in current code | [Run 37632384225](https://github.com/MNeroba/openwrt-flint3/actions/runs/37632384225) found variable-mask FIELD_GET and format warnings in the diagnostic snapshot; corrected in implementation commit 04128d1dbb |
| Current ARM64 module build | PASS | [Run 37638430759](https://github.com/MNeroba/openwrt-flint3/actions/runs/37638430759) built all five objects with `W=1`, modpost and module link; no candidate warnings/errors |
| Earlier pre-rebase P1-A prototype build | PASS, historical | [Linux 6.18.39 run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37070037416) built the five-object prototype at commit 250f5d469d46ff5de07dfe8a96e3fe90636248ff; this artifact does not cover this rebased source or package release 8 |
| Inherited broad CI matrices | CANCELED | Four automatic kernel/package runs for the original feature branch were stopped; no all-target pass is claimed |
| Current OpenWrt package/DTB/full image | PASS | [Run 37638590756](https://github.com/MNeroba/openwrt-flint3/actions/runs/37638590756) built the BE9300 AP-config image and `kmod-rtl837x-dsa` package for source revision `ad5d1b27a2f8b67cf75c2fa2468c60f5ca1739fd` |
| Runtime/codec tests | NOT RUN | No automated suite or fault-injection result is claimed |
| BE9300 hardware | NOT RUN | No previous SDK-driver or P0 result is attributed to this change |

### Current build inputs and artifacts

- CI-tested source revision: `ad5d1b27a2f8b67cf75c2fa2468c60f5ca1739fd`.
  The code implementation is `04128d1dbb19b547ab546c6032fbb5143d8c74e7`,
  and `package/kernel/rtl837x/src` has tree
  `bf2b6955bb05ab6c82322d89d1069a733c345204`. The later changes on top of the
  CI-tested revision are documentation-only.
- Focused module job: GitHub Ubuntu 24.04, AArch64 cross compiler, Linux
  6.18.39; [run 37638430759](https://github.com/MNeroba/openwrt-flint3/actions/runs/37638430759).
  All five objects, `W=1`, modpost and linking passed without candidate
  warnings/errors. Downloaded `rtl8372n_dsa.ko` is ELF64 AArch64; SHA-256
  `51f9f78d2fa97d5468bf1f318736d4582f07e63c928ac99665f66acda98dc262`.
- Full target job: [run 37638590756](https://github.com/MNeroba/openwrt-flint3/actions/runs/37638590756),
  artifact [11498101189](https://github.com/MNeroba/openwrt-flint3/actions/runs/37638590756/artifacts/11498101189).
  The BE9300 AP-config package, DTB and full image build passed. The resulting
  package is `kmod-rtl837x-dsa-6.18.39.0.0.2-r8.apk`; it contains
  `rtl8372n_dsa.ko` under `/lib/modules/6.18.39/`.
- Sysupgrade image:
  `openwrt-qualcommbe-ipq53xx-glinet_gl-be9300-squashfs-sysupgrade.bin`;
  SHA-256 `408bbd403f3d5fec62ebc736b77c7db0baf1cee66dbbb92e66e61f5d68b3d3cd`.
- The full-image build used pinned feeds: packages
  `493b2ae11c3148f43b3ab680ac2b2bb78cc8430c`, LuCI
  `aa3d48836e90ae0706c8d8f9b46b8371e45cfe1f`, routing
  `4b9891b9136259f93294a424507ed24c5e8c1cbd`, telephony
  `5d68d53c160a325ea9d03fce393e051573bcc736`, and video
  `816fa8fe0ca759cc5d1ba71af1a716405bf4dda4`.

These results establish compilation and target packaging for the current code;
they do not establish runtime table behavior, forwarding, or hardware safety.
No runtime/codec suite, fault injection, or BE9300 boot test has been run. The
broad inherited push/PR kernel and package matrices were canceled deliberately
for this feature branch:
[push packages](https://github.com/MNeroba/openwrt-flint3/actions/runs/37070038337),
[push kernels](https://github.com/MNeroba/openwrt-flint3/actions/runs/37070038397),
[PR packages](https://github.com/MNeroba/openwrt-flint3/actions/runs/37070319023),
[PR kernels](https://github.com/MNeroba/openwrt-flint3/actions/runs/37070319067).
The focused module and full-image jobs were retained; parent #104 runs were
untouched. Earlier pre-rebase prototype build details remain in the workflow
history and do not substitute for these current results.

The detailed [table regression procedure and result template](P1-TABLE-TEST.md)
records expected BE9300 words, test order, A0–A6 evidence and failure triage.

## Required first-device follow-up

Use [FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md) for the **new image
revision**, beginning with T0. The inherited #104 build result is not its T0.

- Confirm VLAN 1 bootstrap completes and no `VLAN ... readback mismatch`, table
  timeout, lock warning or setup failure appears. Record the VID/word in any
  mismatch; capture complete dmesg/pstore and silicon revision.
- Capture every P1-A setup snapshot line. Compare VLAN 1 membership/untag masks,
  configured-port PVIDs, CPU/user isolation, zero learning limits, CPU-only flood
  masks, and VLAN controls with the expected values. Report any diagnostic
  mismatch even if setup continues; do not treat these reads as proof of packet
  isolation or VLAN offload.
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
