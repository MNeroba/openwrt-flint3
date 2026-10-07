# RTL8372N P0 implementation status

Updated: 2026-10-07. This is a source candidate, not a hardware-qualified driver.

[BUILD-REPORT.md](BUILD-REPORT.md) consolidates publication evidence, exact
revisions, artifacts, feature readiness and remaining acceptance gates.

## Latest hardware result

Release 6 passed T0/T1/T2/T4/T7. Release 7 (`79afa2c51a3c2396c33ed511ed092d799c52e1bf`)
passed ARM64 module and full-image CI and has a new [hardware report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6037094988)
with [raw boot and test logs](https://gist.github.com/perceival/145d80ee322c48e88870ec011ae1200d).
On release 7, T0/T1/T7 pass: all four PHYs bind to the private driver, the
port 0–2 power-down warnings are absent, and three warm reboots plus one cold
cycle preserve link and reachability. T5 passes all six LAN pairs with
bidirectional 30-second TCP and 20/20 pings; it remains CPU/software-bridge
traffic. The run recorded 77 new RX drops on LAN3 and TCP retransmissions, but
no P0 throughput threshold is defined. T3 is reported PASS for all four jacks;
the attached raw capture visibly shows link-down intervals for LAN2–LAN4 but
not LAN1, so the LAN1 cycle evidence needs clarification. Release 6 T2/T4
remain PASS; release 7 logs show DSA setup and pings but do not include a
separate T2 command report or a new 100-packet T4 run. Hardware T6 remains
unverified; the release-7 CPU snapshot lacks softirq data. See
[PHY-PROBE-REPORT.md](PHY-PROBE-REPORT.md) for exact evidence and remaining gates.
P0 is not fully qualified.

## Source work completed

- Runtime contiguous-field helpers validate masks and value range. Raw bitmaps
  use separate regmap updates; the disjoint MAC fields are programmed separately.
- PHY commands are serialized over idle/staging/execute/poll/readback. PHY
  addresses/masks are restricted to 4–7. Parent MDIO locking uses the nested
  subclass; lock order is documented.
- C22 page state is per PHY, including the page-0/0xa40 alias and address bounds.
  The private driver supplies phylib page callbacks using unlocked bus helpers;
  page save/select/read/restore remains under the child MDIO bus lock.
- A private PHY driver uses per-port native MMD access, published capability and
  speed decoding, standard autoneg and explicit rejection of forced 2.5G.
  Driver registration precedes child-bus creation. Each discovered PHY receives
  a device-specific private-driver matcher before registration; automatic bus
  scanning is disabled. Hardware IDs and the external WAN driver are retained.
  The gate checks successful
  device binding under the device lock and both private-driver identities;
  missing/wrong/unbound PHYs fail probe. Release 7 confirms private binding and
  1G/2.5G links on all four jacks. Full partner-advertisement behavior remains
  unmeasured. The per-device MDIO
  callback runs after OF matching; the BE9300 childless MDIO layout is covered,
  while vendor-specific PHY compatibles need separate review.
- Incomplete hardware bridge callbacks were removed. P0 uses CPU-only
  isolation/flood masks and disabled learning; software bridging is intended.
  Reserved RMA/BPDU handling and actual isolation still need bench verification.
- The reset writer is separate from ordinary writes, with no completion poll
  immediately after the reset command. PCS failures produce link-down state.
- DT layout is validated before GPIO reset; one fixed 10G CPU on 3 or 8 and
  internal user ports 4–7 are accepted. Duplicate/unsupported layouts fail.
- Setup failure and teardown quiesce forwarding/PHYs; managed GPIO reset is
  asserted on failed probe/removal. Shutdown clears driver data. PHY
  `port_enable`/`port_disable` now skip non-PHY ports and issue PHY writes only
  to internal PHY ports 4–7 (package release 7).
- The module targets Linux 6.18, declares MDIO devres dependency, and package
  release is now 7. Selection on the private bus no longer relies on autoload
  priority; the ordinary Realtek driver remains available for external PHYs.
- BE9300 uses `realtek,rtl8372n`. Legacy BE6500 `realtek,rtl837x` is unsupported
  by this candidate, so it must not replace the shipping package yet.
- The source ledger and first-device procedure were updated; general VLAN,
  FDB/MDB/STP, LAG, rate limiting, statistics, GPIO and EEE remain later stages.

## Checks and limits

- `git diff --check`: passed at the source-fix stage.
- Kernel `checkpatch.pl --no-tree --file`: no errors. One expected warning for
  the mutable regmap configuration copy, whose `lock_arg` must be set per device.
  This style check does not establish compilation or functional correctness.
- A kernel-header preparation attempt against the cached Linux 6.18.38 tree
  stopped at `defconfig`: macOS `ld` rejects `--version` and Kbuild reports an
  unsupported linker. No target module was compiled by that attempt.
- [ARM64 module CI passed](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059225390)
  for `954a84bd7c` against checksum-pinned Linux 6.18.39: kernel exports/tagger,
  all four candidate objects, `W=1`, modpost and module linking succeeded.
  There were no compiler warnings in the candidate step. The earlier failed
  API check led to use of the public `phy_drivers_register/unregister` API.
  The artifact includes `.config`, complete build logs and the AArch64 module.
  Module SHA-256:
  `d47c1109ab19c30f81f7a7ccd034d787b1fe2e2684acb99c89389cee1b93f1d6`.
  This API-check artifact is not an OpenWrt installation package.
- [OpenWrt image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177) completed successfully on 2026-10-02 for source
  revision `954a84bd7c46dbbb2412eeddfb300aad8b4cff35`. The full BE9300 AP-config package/DTB/image workflow and
  artifact confirmation passed. All five pinned feed revisions, installed
  revision checks, and driver/MDIO-devres package selection were verified.
  The [full build artifact](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177/artifacts/11254652883) contains 317 files including the
  generated configuration, logs, packages and target images. This is build
  baseline-build evidence only. Release-7 CI and hardware results are recorded
  separately below.
- The maintainer independently reports a T0-equivalent build of #104 revision
  `908810c09bd9adfbbc7d25437a9d50b55b2de940`, using the reference AP
  config with `wsdd2` and vendor `ssdk` stripped. The image revision matches
  its tree; checksummed sysupgrade includes `rtl8372n_dsa.ko` and
  `tag_rtl8_4.ko`, with the previous `rtl837x` module absent. The image is
  installed on the recoverable bench ([T0 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-5976007832)).
  The [2026-10-04 hardware report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-5983997672)
  records **T1 FAIL; T2–T8 BLOCKED**: chip ID `0x83727000` reads successfully,
  then internal-PHY binding fails on port 4. No traffic check has passed.
- Diagnostic `1b7a32bef2` passed both [module](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197) and
  [image](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273960255) CI. The [2026-10-06 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6006419562)
  confirms maintainer T0 PASS and T1 FAIL: all four IDs are `0x001ccad0`, but
  the in-tree RTL8224 driver wins binding. A later boot hang is also reported.
- Release 5 failed compilation on a private kernel macro. Release 6 fixes that;
  [ARM64 module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397943227)
  and [full image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397974073)
  passed. The [release-6 bench report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6014637917)
  passes T0/T1/T2/T4/T7. Its [T3/T5 follow-up](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6016542658)
  covers connected jacks and all three pairs among LAN1–LAN3; LAN4, physical
  cable cycling, 30-second traffic runs and hardware T6 remain open for the
  release-6 follow-up. The earlier stop was not reproduced, but its cause is
  not established. Release 7's port-callback guard passed ARM64 module and
  full-image CI and has been exercised on hardware; the port 0–2 warnings are
  absent in its boot log.

## P1 research update

[P1-RESEARCH.md](P1-RESEARCH.md) records the public VLAN/L2/CIST material and
DSA requirements available for follow-up source work. Three conflicting
descriptions (VLAN selector, VLAN bit 25 and L2 bit 29) are identified explicitly.
BPDU delivery, database semantics and failure handling gate hardware bridge
offload. That research update changed documentation only and added no P1
callback or hardware result; the P0 inputs were identical to the CI revision
at that step. Release 5 failed ARM64 compilation on a private kernel macro;
release 6 fixes it and needs its own build and bench run.

## Remaining gates

1. ARM64 compilation/modpost: release-7 [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454484695) passed.
   Re-run if source or build inputs change.
2. OpenWrt package/DTB/full-image build: release-7 [full-image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454704524)
   passed for source commit `79afa2c51a3c2396c33ed511ed092d799c52e1bf`.
   Re-run if source or build inputs change.
3. Resolve flagged provenance rows, particularly SDS facts/source lineage.
4. Clarify LAN1's physical link-cycle evidence in T3 and repeat the full 100-packet
   T4 check on release 7. T5's six-pair, 30-second run is complete. Hardware
   T6 VLAN/isolation readback, reserved control-frame behavior and the remaining
   recovery/concurrency checks are open. Release 7 T7 passed three warm reboots
   and one cold power cycle; the WAN-side PCS message still appears during boot.
5. Restore P1/P2 behavior or obtain maintainer agreement to a narrower scope.
6. The Draft remains a source and provenance review candidate. Hardware
   qualification and required source-lineage decisions remain open.
