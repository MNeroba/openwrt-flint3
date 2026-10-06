# rtl8372n: add P0 DSA bring-up candidate for GL-BE9300

## Problem

The shipping RTL837x package uses the old SDK-backed source. In his
[deprecation comment](https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver/issues/2#issuecomment-5935360083),
Airjinkela recommends the DSA refactor and plans to remove the retained
register-definition header. This PR starts the replacement tracked in
[#100](https://github.com/perceival/openwrt-flint3/issues/100), following the discussion in
[#99](https://github.com/perceival/openwrt-flint3/issues/99).

## Summary

Prepare a minimal RTL8372N DSA bring-up candidate for GL-BE9300:

- Adapt the Airjinkela DSA/phylink/MDIO architecture with retained attribution.
  Remove the SDK object tree, restricted generated header and vendor PHY/SerDes
  patch arrays from the candidate.
- Add a small register map with a pinned source/operation ledger; retain the
  RTLPlayground MIT notice and identify unresolved SDS lineage explicitly.
- Fix runtime register-field handling, serialize complete PHY/SDS transactions
  and document child/parent MDIO lock ordering.
- Add per-PHY C22 pages and a private internal-PHY driver using actual-port
  native MMD access, published capability/status definitions and 2.5G autoneg.
  Missing/wrong PHY binding and unsupported speed encodings fail closed;
  forced 2.5G is unsupported.
- Use a dedicated reset writer, fail-closed PCS reads, early topology validation
  and setup-failure/teardown quiescing.
- Use CPU-only isolation/flood masks with hardware learning disabled. Incomplete
  hardware bridge callbacks are removed; untagged software bridging is the
  intended P0 fallback and still needs bench validation.
- Target Linux 6.18, declare MDIO devres dependency and build `rtl8372n_dsa.ko`
  with the kernel RTL8_4 tagger. Coordinate BE9300's switch compatible to
  `realtek,rtl8372n` and remove unsupported legacy switch properties.
- Include reproducible CI inputs, a build/review report and the first-device
  procedure with a result template.

## Validation and build evidence

The package baseline at `954a84bd7c46dbbb2412eeddfb300aad8b4cff35` and
diagnostic `1b7a32bef2` built successfully, but the diagnostic hardware run
failed T1 because RTL8224 bound first. Release 5 failed module CI on a private
kernel macro. Release 6 [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397943227) and [full image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397974073) passed; module SHA-256:
`9ccd428ae58f7650d8f7e47455c24250349e840758208e800146663a44037263`. The
[release-6 hardware report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6014637917) passes T0/T1/T2/T4/T7. Follow-up testing adds link/rate checks for LAN1–LAN3 and bidirectional software-bridge traffic across their three pairings. T3 remains partial: LAN4 had no peer and cable unplug/replug was not tested. T5 remains partial: only 3/6 pairs were available and iperf3 runs were 8–10 seconds versus the 30-second procedure. T6 has software output only; T8 is router-endpoint CPU traffic. Package release 7 adds a PHY callback guard. Its ARM64 module build passed; the full image build is in progress, and no release-7 hardware result exists yet.

| Check | Result | Evidence |
| --- | --- | --- |
| ARM64 / Linux 6.18.39 | **Diagnostic PASS; release 5 compile failed; release 6 and 7 module PASS** | [Release 4 module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197) passed. [Release 5 CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37395423769) failed because `DEFAULT_GPIO_RESET_DELAY` is private to `of_mdio.c`. Release 6 [module CI passed](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397943227). Release 7 [module CI passed](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454484695): all four objects compiled with `W=1`, modpost/link succeeded, no candidate compiler warnings; module SHA-256 `8163371788badabaf4777f88b24e6de0559634a574c1cc59c3033822df2eeb37`. |
| BE9300 OpenWrt configuration | **PASS** | AP config, pinned-feed verification and driver/MDIO-devres selection in the [target run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177) |
| OpenWrt package / DTB / full image | **Release 6 PASS; release 7 in progress** | Baseline, diagnostic and release-6 image builds passed; maintainer T0 details for release 6 are in the [bench report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6014637917). The [release-5 image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37395426353) was cancelled after its module compile failed. The [release-7 image build](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454704524) is running. |
| Whitespace | **Release 7 PASS** | `git diff --check` passed for the release-6 to release-7 change. |
| checkpatch | **Release 7: 0 findings** | The release-7 source diff has 0 errors, warnings or checks under strict `checkpatch.pl`. |
| BE9300 hardware | **Release 6: T0/T1/T2/T4/T7 PASS; T3 PARTIAL; T5 PARTIAL (3/6 pairs); T6 software output only; T8 recorded** | [Initial release-6 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6014637917), [T3/T5 follow-up](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6016542658) and [redacted raw logs](https://gist.github.com/perceival/f7abebb5395db63b97d3775ebd2c544a). The three available pairs among LAN1–LAN3 pass bidirectional connectivity and short TCP runs in the CPU/software-bridge path. LAN4-dependent pairs, 30-second runs and physical cable cycles remain open. Release 7 needs a fresh T0/T1. |

The [ARM64 artifact](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059225390/artifacts/11248964933)
contains the generated config, complete build logs and module. Its module
SHA-256 is `d47c1109ab19c30f81f7a7ccd034d787b1fe2e2684acb99c89389cee1b93f1d6`.
This is an API-check artifact, not an OpenWrt installation package.

The full target workflow uses `configs/ap.config` and five pinned feeds. It
retains build inputs, generated config, logs, target packages/images and image
checksums when available. A partial artifact upload does not establish success.

### Release-6 warning diagnosis

The serial log's `failed to power down PHY on port 0/1/2: -22` comes from our
DSA `port_disable` callback. Release 6 treated all non-SerDes ports as PHYs,
while the accessor accepts only ports 4–7 and returns `-EINVAL` before any PHY
transaction. Package release 7 limits both enable and disable callbacks to the
supported PHY-port mask. The release-7 ARM64 module build and source checks
passed; the full image and fresh T0/T1 hardware confirmation are pending.

The conduit `lan` counter `tx_errors=2^64-2` is separate from the switch
driver. Qualcomm PPE computes it with an unsigned subtraction of
`tx_frames_g` from `tx_packets`; the displayed value is consistent with a
two-count underflow. The supplied log has only the aggregate `ip -s link`
result, not both raw MIB operands, so their exact values and the reason they
differ remain unverified. This does not establish actual packet loss; it needs
separate raw-MIB validation.

The `10GBASE-R link not up before USXG_EN` message follows `wan` inband/USXGMII
setup in the successful release-6 log and does not stop that boot. It is not
evidence of a failed DSA CPU link, and it does not explain the earlier release-4
log ending. That older stop remains unexplained because the log contains no
panic or hung-task evidence.

## Confirmed PHY binding cause and registration correction (2026-10-06)

Perceival's [diagnostic report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6006419562) confirms all four PHYs read
ID `0x001ccad0`, but the in-tree `RTL8224 2.5Gbps PHY` driver wins binding.
Adding another matching ID or relying on module ordering provides no priority.

Release 5 failed ARM64 compilation because `DEFAULT_GPIO_RESET_DELAY` is
private to kernel `of_mdio.c`. Release 6 replaces it with a named local 10 us
default matching upstream. It reports unsupported `ethernet-phy-package` nodes
explicitly.

The release-6 correction:

- Registers the private bus with automatic scanning disabled, discovers actual
  PHY IDs and sets a device-specific matcher before registering each PHY.
  The per-device MDIO callback admits the private driver on enabled ports 4–7.
  Linux checks OF matches before invoking this callback; vendor-specific child
  compatibles need separate review. BE9300 has no child MDIO PHY nodes.
- Preserves optional MDIO-node/PHY-node association and reset delays; rejects
  invalid, duplicate or missing explicit internal PHY addresses and unsupported
  C45 PHYs/package nodes. The BE9300 path needs no DT compatible change. WAN
  Realtek remains available.
- Retains completed-binding checks and all probe/read-error diagnostics.
  Registered PHYs are owned by managed bus teardown; failed registration frees
  the unregistered device. No ID spoofing or post-probe rebind is used.
- Leaves PHY/SerDes/reset/forwarding register programming unchanged.

The [failure/fix report](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/PHY-PROBE-REPORT.md)
records the release-6 results and the warning analysis. The PCS line appears
during WAN setup on the successful release-6 boot, so it does not explain the
prior log ending. Release-6 module and image CI passed. Its hardware report
confirms private PHY binding and router reachability. Follow-up T3/T5 covers
link/rate checks on three connected jacks and bidirectional CPU/software-bridge
traffic across all three pairs among them. LAN4, physical cable cycling, the
full 30-second/six-pair matrix and hardware T6 readback remain incomplete. The
release-7 PHY-port guard needs a fresh image and repeat T1.
P1-A remains separate.

## Scope and remaining work

| Stage | Scope | Current status |
| --- | --- | --- |
| P0 | Probe/reset, register access, internal PHY, 10G CPU PCS, native tags, four LAN jacks and CPU/software forwarding | Release 6 passes T0/T1/T2/T4/T7. T3 is partial (LAN1–LAN3 rates; peer interface cycles on LAN2/LAN3; no LAN4 peer or physical cable cycles). T5 is partial (3/6 pairs among LAN1–LAN3, bidirectional ping and 8–10-second iperf3 runs; procedure calls for 30 seconds). T6 has software output only; T8 is CPU-endpoint data. Release 7 guard needs fresh image/T1. |
| P1 | Hardware bridge/VLAN, FDB/MDB, STP/BPDU and bridge flags | Not implemented; [source/dependency plan](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/P1-RESEARCH.md) prepared; BPDU/database/table semantics remain gates |
| P2 | LAG #47 and rate limiting #49 | Not ported; prior feature requirements remain applicable |
| Other baseline interfaces | MTU/jumbo, alternate tags, mirroring, MIB/ethtool, EEE and GPIO parity | Not established; restore or agree individual deferrals |

Only RTL8372N with the explicit compatible is included in the candidate scope.
BE9300 uses CPU port 3 at fixed 10GBASE-R and internal PHY/user ports 4–7.
Legacy BE6500 `realtek,rtl837x` nodes are unsupported by this candidate.

This Draft is for technical/provenance review and staged hardware bring-up.
Release 6 confirms basic probe/topology and router reachability, but not complete
P0 qualification. Keep the shipping baseline until the acceptance criteria in
#100 pass or the maintainer explicitly agrees the corresponding feature
deferrals. PPE/NAT and
802.11r remain outside this PR. Final official driver and board submissions
should remain separate; [OpenWrt #23161](https://github.com/openwrt/openwrt/pull/23161)
was open and unmerged at the previous integration check. The independent `908810c09b` image failed T1 in the earlier diagnostic run.
Release 6 now passes T1 and selected P0 checks; complete release-7 and the
remaining P0 matrix before the separate P1-A image and A0–A6 tests.

### P1 research and maintainer scope update (2026-10-04)

Public sources cover substantial VLAN/L2/CIST operations, but they disagree
on the VLAN selector description, VLAN bit 25 and L2 bit 29. The new plan
records these conflicts, the Linux DSA CPU/database requirements, two BPDU
delivery options and a staged implementation/bench matrix. No P1 runtime
callbacks were added by that research update; it left the baseline P0 build
inputs unchanged. The later diagnostic source revision is tracked separately.

Perceival agrees with the P0 hardware → P1 parity → P2 order and accepts one
initially offloaded hardware bridge for P1. Multiple bridge domains/MST remain
deferred pending semantics work; unsupported domains require CPU-only/software
fallback and isolation tests. Keep P1-A separate until the remaining P0 matrix and provenance gates are
resolved. Prepare shared table transactions and checked codecs first;
prove BPDU CPU delivery, CIST and dynamic flush before enabling hardware
bridge/VLAN/flags; then add FDB/MDB with explicit database/CPU-entry semantics.

## Provenance review

The restricted header and vendor patch arrays are excluded. The existing
package `LICENSE` is unchanged. Remaining public-source lineage is recorded
rather than described as fully cleared: Airjinkela's
[SDK disclosure](https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver/issues/2#issuecomment-5946313878)
still matters, particularly for SDS command fields and polarity definitions.
Repository licensing and matching register values are not presented as proof
of independent origin or as source-owner authorization. No third-party
Signed-off-by is inferred.

## Review documents

- [Build/review report and exact inputs](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/BUILD-REPORT.md)
- [Current implementation status](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/P0-STATUS.md)
- [Source and operation ledger](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/PROVENANCE.md)
- [PHY probe failure analysis and diagnostic rerun](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/PHY-PROBE-REPORT.md)
- [First hardware test matrix and report template](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/FIRST-HARDWARE-TEST.md)
- [P1 feasibility, source conflicts and staged acceptance matrix](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/P1-RESEARCH.md)
- [Pre-PR audit and phased remediation plan](https://github.com/MNeroba/openwrt-flint3/blob/rtl837x-dsa-port/package/kernel/rtl837x/PRE-PR-PLAN.md)

@perceival, thank you for the release-6 bench report. The follow-up comment
below records the port-callback fix and requests the remaining T3/T5 checks.
