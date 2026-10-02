# RTL8372N P0 implementation status

Updated: 2026-10-02. This is a source candidate, not a hardware-qualified driver.

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
  Driver registration precedes child-bus creation; missing/wrong binding fails
  probe. Its actual ability/AN behavior needs hardware confirmation.
- Incomplete hardware bridge callbacks were removed. P0 uses CPU-only
  isolation/flood masks and disabled learning; software bridging is intended.
  Reserved RMA/BPDU handling and actual isolation still need bench verification.
- The reset writer is separate from ordinary writes, with no completion poll
  immediately after the reset command. PCS failures produce link-down state.
- DT layout is validated before GPIO reset; one fixed 10G CPU on 3 or 8 and
  internal user ports 4–7 are accepted. Duplicate/unsupported layouts fail.
- Setup failure and teardown quiesce forwarding/PHYs; managed GPIO reset is
  asserted on failed probe/removal. Shutdown clears driver data.
- The module targets Linux 6.18, declares MDIO devres dependency, bumps package
  release to 3 and loads its private PHY driver before normal Realtek autoload.
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
- [OpenWrt image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177)
  is building the complete BE9300 AP image from the same code revision.
  `P0-FEEDS.conf` fixes all five feed revisions; installed revision checks and
  selection of the driver/MDIO-devres packages passed. Generated config, logs
  and images are retained. Package/image completion is still pending.
- Later documentation-only commits do not change the tested source, DTS,
  package Makefile or workflows. No hardware result has passed.

## Remaining gates

1. ARM64 compilation/modpost: passed for the revision above. Re-run for any
   subsequent code changes; OpenWrt-specific compilation remains a separate gate.
2. Build the OpenWrt package, DTB and full BE9300 image with pinned config/feeds.
3. Resolve flagged provenance rows, particularly SDS facts/source lineage.
4. Test PHY binding/AN, real 10G CPU link, cold/warm reset, isolation/fallback,
   reserved control-frame handling and error recovery on the bench.
5. Restore P1/P2 behavior or obtain maintainer agreement to a narrower scope.
6. Publish a Draft only after the agreed P0 build gate; do not propose merging
   this functional reduction as a full replacement.
