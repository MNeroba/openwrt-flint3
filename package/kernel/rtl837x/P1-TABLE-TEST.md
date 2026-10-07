# RTL8372N P1-A table regression procedure and report

Updated: 2026-10-07. Applies to the dependent
[P1-A PR #1](https://github.com/MNeroba/openwrt-flint3/pull/1).
Stacked on [P0 PR #104](https://github.com/perceival/openwrt-flint3/pull/104),
parent source 27103b8e6b705e838eada250f33231b2c1a8c045.

Current implementation commit: 40da0af192417ffccc3a4b9e1124e808779ce467.
Source tree: 8185243d6c00df0e4ce8310017f9f39d5b3bcdb9; package release: 8.
These are requested future checks, not completed hardware results.

## 1. Revision, build and test order

1. Establish the P0 baseline using [FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md)
   and post its exact-source results under #104. If unavailable, mark baseline
   comparison BLOCKED; the new branch cannot establish a P0-to-P1 regression
   comparison without those results.
2. Build the P1-A OpenWrt package, DTB and complete BE9300 image from the exact
   revision to be installed, using the documented AP config and pinned feeds.
   Confirm `rtl8372n_dsa.ko`, `tag_rtl8_4.ko`, package/dependencies and image
   checksum. Record package release 8. This is the P1-A T0 gate.
3. Use the documented TFTP/initramfs recovery path, independent management and
   dmesg/pstore collection from the P0 procedure. Keep the known-good image.
   The project does not assume a usable normal UART console on BE9300.
4. Complete the table-specific rows below and repeat all applicable P0 T1–T8
   and additional regression checks on the new image. Record each result
   separately from the P0 baseline.

An earlier P1-A prototype passed the focused ARM64 API build at source
250f5d469d46ff5de07dfe8a96e3fe90636248ff and source tree
eefee5ba892bff81e29e269b099867cc67eeb0b3. Its artifact is recorded in
P1-TABLE-REPORT.md. This build predates the rebase onto the P0 Release-7 source,
the setup-snapshot code and package release 8; it is historical evidence only
and does not qualify the current branch. The current source needs fresh ARM64
and OpenWrt target builds. Even a successful API build is not an OpenWrt
installation package or a completed T0.


## 2. Expected bootstrap values

For the documented **BE9300 topology only**, CPU port 3 and user ports 4–7:

| Item | Expected source value |
| --- | --- |
| VID | `1` |
| Members | Ports 3–7, bitmap `0x0f8` |
| Untagged | Same bitmap `0x0f8`, including CPU, as in #104 |
| PVID of the used ports | `1` |
| VLAN word staged at `0x5cb8` | `0x0203e0f8` = raw bit 25 + members + (untagged << 10) |
| Write command at `0x5cac` | `0x00010303`: VID 1, selector 3, write + execute |
| Verification read command | `0x00010301`: VID 1, selector 3, execute |
| Completed transaction | Execute bit 0 clears; read word matches the staged word exactly |

The word values describe what this source requests. They do not settle bit 25
validity/IVL semantics or prove packet forwarding. The read command is the last
command in a successful verified write; do not expect the write command still
to be present afterwards. Hardware can also change status bits; record raw
observations and timing rather than treating a stale control-word snapshot as
an independent command trace.

Use an agreed, serialized table-read method if available. Reading `0x5ccc`
alone returns the last table output and does not independently select VID 1.
Do not improvise raw MDIO writes or introduce concurrent external access to the
engine. The candidate exposes no userspace VLAN-table/debug control interface.
If the necessary method is unavailable, mark independent readback BLOCKED.

## 3. Requested checks

| ID | Procedure | Expected outcome / evidence |
| --- | --- | --- |
| A0 | Record package/image build and installed revision, kernel, config/feed lock and checksums | Exact-source target package/DTB/image gate passes before boot; API artifact is not substituted |
| A1 | Boot with the documented board layout; collect full dmesg/pstore and DSA/PHY binding | Setup completes; no table timeout, `VLAN ... readback mismatch`, registration error, warning or oops |
| A2 | Where an agreed method exists, obtain the selected VID 1 word, member/untag masks and PVIDs | Word/masks match section 2; record method and raw values. A last-output register read alone is insufficient independent evidence |
| A3 | Repeat three warm boots and one full power cycle; repeat A1/A2 and router reachability each time | Same bootstrap result across resets; report each boot separately and record any first failing boot |
| A4 | Repeat every applicable P0 T1–T8 and additional check on this image | Four jack mappings and PHY rates/binding, physical CPU link, all six LAN pairs, standalone isolation, software bridge lifecycle and negative forwarding retain expected P0 behavior |
| A5 | Repeat existing concurrent PHY reads / port down-up and recovery checks | No new MDIO/lock/PHY/CPU-link errors. This is a P0 shared-bus regression check; it does not exercise simultaneous VLAN/L2 transactions |
| A6 | Only if a reviewed mechanism can reproduce busy/transport failure or mismatch, record the failing phase and cleanup | Error reaches setup failure; record elapsed behavior, isolation/PHY-power/reset outcome and recovery. Otherwise mark fault injection NOT RUN/BLOCKED |

A successful setup is evidence that the driver's write/read equality check
completed. It does not establish the independent table observation in A2 or
the forwarding checks in A4. Exact equality can reveal silicon normalization
or reserved-bit differences; attach both written and read words before changing
the policy.

Current runtime table traffic is VLAN 1 setup only. There are no DSA VLAN
callbacks or userspace tools to test invalid VID/mask inputs, general VLAN
add/delete or table contention. Codec/error/concurrency validation beyond this
bootstrap remains unrun source/future-test work; do not claim it from ethtool
concurrency. Hardware bridge/FDB/MDB/STP/BPDU, LAG and policing qualification
remain later stages. Do not form a physical loop before the separate BPDU gate.

## 4. Failure evidence

| Failure | Attach first |
| --- | --- |
| Table/probe timeout | Full boot log, first error, timing/boot kind, revision, chip ID and transport evidence available through the agreed method |
| Readback mismatch | VID, written/read words from the error, topology/port masks, boot kind, full log and silicon revision; do not infer a bit-25 meaning from the mismatch |
| Setup cleanup/recovery problem | First error plus later cleanup/reset errors, management/recovery outcome, isolation/PHY observations if available |
| Forwarding or link regression | Corresponding P0 row, topology, link states, counters/captures and baseline versus P1-A source/image identifiers |

## 5. Result template

Post results in the dependent PR, linking the baseline report under #104.
Use **PASS / FAIL / BLOCKED / NOT RUN** for every row and retain detailed P0
pair/rate/boot results. An unavailable test is not a pass.

```text
P1-A source/image commit:
Kernel release / OpenWrt version:
Package version-release / module identity:
Image filename / SHA-256:
Build log / configuration / installed feed lock:
Board / chip ID / silicon revision:
DT compatible / CPU port / user ports / polarity flags:
P0 baseline commit + image checksum + report link:
Recovery / independent management / pstore method:

A0 exact-source target build:
A1 bootstrap / DSA / PHY binding:
A2 independent VID 1 readback:
  Method:
  Raw VLAN word / members / untagged:
  Used-port PVIDs:
A3 resets (warm 1 / warm 2 / warm 3 / cold):
A4 P0 T1-T8 + additional checks (attach complete matrix):
A5 PHY/bus concurrency and recovery:
A6 busy/error/mismatch injection (mechanism or reason not run):

Mismatch VID / written word / read word, if any:
First failure and elapsed observations:
Cleanup/reset errors and recovery outcome:
Attached full logs / pstore / readbacks / counters / captures:
Overall result / first blocker / suggested follow-up:
```

Use [P1-TABLE-REPORT.md](P1-TABLE-REPORT.md) for current source/build evidence
and [PROVENANCE.md](PROVENANCE.md) for source origins. The [wider P1 plan](P1-RESEARCH.md)
continues to track unimplemented offloads and their separate acceptance gates.
