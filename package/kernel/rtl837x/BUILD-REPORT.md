# RTL8372N P0 build and review report

## Latest result (status checked 2026-10-06)

| Gate | Result | Evidence / scope |
| --- | --- | --- |
| Mainline ARM64 compilation | **Diagnostic PASS; release 5 failed; release 6 pending** | Diagnostic `1b7a32bef2` [passed](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197). Release-5 [build failed](https://github.com/MNeroba/openwrt-flint3/actions/runs/37395423769): `DEFAULT_GPIO_RESET_DELAY` is private to `of_mdio.c`. Release 6 fixes it; rebuild pending. |
| OpenWrt configuration | **PASS** | BE9300 AP configuration; all five pinned feeds verified; driver and MDIO-devres packages selected |
| OpenWrt package, DTB and image | **Diagnostic PASS; release 5 image pending; release 6 pending** | Baseline and diagnostic builds passed as linked. Release-5 [full image build](https://github.com/MNeroba/openwrt-flint3/actions/runs/37395426353) was in progress at last check. Release 6 requires a fresh image build and T0 before flashing. |
| Source whitespace | **PASS** | `git diff --check` against the proposed base |
| Kernel style review | **Release-5 patch: 0 checkpatch findings; compile failed** | Release 6 fixes the missing macro and needs a new checkpatch/build. |
| BE9300 hardware | **T1 FAIL; T2–T8 BLOCKED** | The [2026-10-06 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6006419562) confirms IDs `0x001ccad0` on all four ports; RTL8224 binds instead of the private driver. Later boot hang unresolved. Release-6 hardware rerun pending. |
| Retained source provenance | **OPEN REVIEW** | Restricted header and patch arrays excluded; SDS field/polarity lineage remains unresolved |
| Replacement acceptance in #100 | **NOT MET** | Hardware and feature parity, or agreed deferrals, remain required |

The CI full target-build run passed on 2026-10-02 for the tested source revision.
On 2026-10-04, the maintainer also reported an independent T0-equivalent build
of #104 revision `908810c09bd9adfbbc7d25437a9d50b55b2de940`, using the reference
AP config with `wsdd2` and vendor `ssdk` stripped. The resulting image revision
matched its source tree; its checksummed sysupgrade contains
`rtl8372n_dsa.ko` and `tag_rtl8_4.ko`, with the old `rtl837x` module absent.
That image was installed on the recoverable bench on 2026-10-04. T1 failed at
the internal-PHY binding check; T2–T8 are blocked. The switch ID is observable,
but internal-PHY access, CPU/LAN links and forwarding remain unqualified.

## Confirmed PHY binding cause and registration correction (2026-10-06)

The [diagnostic hardware report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6006419562) confirms successful C22 ID
reads on all four ports and completed binding to the in-tree RTL8224 driver.
T0 passed; T1 failed and T2–T8 remain blocked. The complete cause/fix and next
bench requirements are in [PHY-PROBE-REPORT.md](PHY-PROBE-REPORT.md).

Release 5 suppresses automatic internal-bus discovery and assigns the private
matcher before PHY registration, but its ARM64 build failed on an inaccessible
kernel macro. Release 6 replaces it with a local named 10 us value matching
upstream OF-MDIO behavior and rejects unsupported `ethernet-phy-package` nodes
with an explicit error. The later boot hang remains unresolved. Release 6 needs
fresh module/image builds and hardware T0/T1; traffic tests remain unqualified.

Diagnostic release 4 (`1b7a32bef2`) passed [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197) and
[full-image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273960255); those passes do not qualify releases 5 or 6.

## Follow-up research (updated 2026-10-04)

[P1-RESEARCH.md](P1-RESEARCH.md) adds a source-grounded plan for VLAN/L2 table
access, bridge flags, FDB/MDB, CIST and BPDU delivery. No P1 runtime support or
hardware result is added. The module and full target-image builds passed for
the unchanged P0 build inputs. Research findings are not hardware or functional
qualification.

## Revisions and reproduction

- Proposed base: `perceival/openwrt-flint3:flint3-be9300`,
  `2365932733ca8ec3b346621d9cec2eb3df3b2cf3`.
- CI source: `954a84bd7c46dbbb2412eeddfb300aad8b4cff35`.
- Maintainer-side exact-head build report: `908810c09bd9adfbbc7d25437a9d50b55b2de940`
  ([comment](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-5976007832)).
- Tested `package/kernel/rtl837x/src` Git tree:
  `785d7682936058c86e90af809e16694ac6dc7492`.
- Documentation commits through `7be7f8d541` retain the baseline build inputs.
  The 2026-10-05 diagnostic revision changes three driver sources and bumps the
  package release. DTS, configuration, workflows and pinned feeds are unchanged;
  its diagnostic builds passed. Release 5 (2026-10-06) failed ARM64 module CI because `DEFAULT_GPIO_RESET_DELAY`
  is private to the kernel OF-MDIO implementation. Release 6 fixes that and
  requires new module and OpenWrt image builds.
- Build hosts: GitHub-hosted Ubuntu 24.04. ARM64 API check uses
  `aarch64-linux-gnu-`; OpenWrt uses the project toolchain/config.

### Mainline API build

[Successful run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059225390)
completed on 2026-10-02 at 20:20:15 UTC.

[Build artifact](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059225390/artifacts/11248964933)
contains the generated kernel `.config`, kernel/module logs and
`rtl8372n_dsa.ko`. There are no compiler warnings in the candidate compilation
step. Kernel exports and the RTL8_4 tagger were built before modpost.

| Input / output | SHA-256 |
| --- | --- |
| `linux-6.18.39.tar.xz` | `a7a7e3d2ae9d95e74197223a8d4eb5f6be7aac21b6e6de27e9685d001c1f8cb0` |
| Mainline `rtl8372n_dsa.ko` | `d47c1109ab19c30f81f7a7ccd034d787b1fe2e2684acb99c89389cee1b93f1d6` |

This is an API/modpost artifact, not an OpenWrt installation package. The
minimal kernel config selects `KUNIT`/`REGMAP_BUILD` to enable the hidden
regmap core; no KUnit test suite was run. This check does not exercise probe,
PHY transactions, SerDes or forwarding on hardware.

### OpenWrt target build

[Full BE9300 run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177) **completed successfully** on 2026-10-02 for source
revision `954a84bd7c46dbbb2412eeddfb300aad8b4cff35`. It used [configs/ap.config](../../../configs/ap.config)
and [P0-FEEDS.conf](P0-FEEDS.conf), then executed `make defconfig` and
`make -j2 V=s`. Package/DTB/image building and artifact confirmation passed.
The [uploaded build artifact](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177/artifacts/11254652883) contains generated configuration,
logs, packages and image outputs.

| Feed | Pinned commit |
| --- | --- |
| packages | `493b2ae11c3148f43b3ab680ac2b2bb78cc8430c` |
| luci | `aa3d48836e90ae0706c8d8f9b46b8371e45cfe1f` |
| routing | `4b9891b9136259f93294a424507ed24c5e8c1cbd` |
| telephony | `5d68d53c160a325ea9d03fce393e051573bcc736` |
| video | `816fa8fe0ca759cc5d1ba71af1a716405bf4dda4` |

The CI image-build and artifact-confirmation gate is **PASS** for the source
revision above. The maintainer separately reproduced revision `908810c09b`
with the documented config adjustments and staged its checksummed image for
bench use. The uploaded CI artifact preserves generated `.config`,
`p0-build-inputs.txt`, installed feed lock, logs, packages and target images.
This establishes successful build/packaging for the baseline. The independently
built `908810c09b` image reached the reported T1 failure on BE9300; it did not
reach traffic tests. A separate minimal OpenWrt dependency-image build has not
been run.

## Feature readiness

Every implemented row below still needs its BE9300 hardware test.

| Feature | Candidate implementation | Required first-device evidence |
| --- | --- | --- |
| MDIO/regmap, detection and reset | Implemented; checked field helpers and dedicated reset writer | Chip ID, no timeouts, cold/warm consistency |
| Internal PHY transport | Serialized C22/C45/OCP; per-PHY pages | IDs, correct binding, concurrent access and recovery |
| PHY autoneg/status | Private driver; published 10/100/1000/2500 decoding | Supported/local/partner modes; peers at available rates; link cycling |
| 10G CPU PCS/SerDes | Mode and polarity plumbing; no vendor patch arrays | Physical PCS at both ends and bidirectional traffic after reset |
| CPU tags / LAN mapping | Native kernel RTL8_4; users 4–7 | Correct source jack, directed TX and no duplicates |
| VLAN 1 bootstrap / isolation | CPU-only matrix; learning disabled; CPU flood masks | VLAN/PVID, isolation/learning/flood readbacks and negative forwarding tests |
| Untagged software bridge | Intended fallback; hardware bridge callbacks absent | First-port join, all six LAN pairs, leave/rejoin and CPU forwarding |
| General switching offload | VLAN/FDB/MDB/STP/bridge flags not implemented | P1 work after P0; BPDU/RMA behavior remains an explicit open test |
| LAG #47 / policing #49 | Not ported | P2 implementation and current-revision bench results |
| Other baseline features | MTU/jumbo, alternate tags, mirroring, statistics, EEE and GPIO parity not established | Restore or agree explicit deferrals |

Only RTL8372N with `realtek,rtl8372n` is claimed as the source scope. The
BE9300 test uses CPU port 3 at fixed 10GBASE-R and users 4–7. Legacy BE6500
`realtek,rtl837x` nodes are unsupported by this candidate.

## Source scope and remaining decisions

[PROVENANCE.md](PROVENANCE.md) records pinned inputs, symbol/operation lineage
and unresolved rows. Airjinkela's [deprecation notice](https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver/issues/2#issuecomment-5935360083)
and [SDK disclosure](https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver/issues/2#issuecomment-5946313878)
are linked explicitly. Public repository licenses are not described as proof
of independent origin or as source-owner permission.

The candidate excludes `rtk-api`, the restricted generated register header and
all vendor PHY/SerDes patch arrays. The existing package `LICENSE` is unchanged;
required author attribution and the MIT notice are retained. Twenty unrelated
local patch-metadata changes are excluded from the published commits.

Before replacement/merge:

1. Re-run the OpenWrt package/DTB/image build if code, configuration or pinned
   feeds change; the full-image gate passed for the revision recorded above.
2. Resolve retained SDS/source lineage questions.
3. Run [FIRST-HARDWARE-TEST.md](FIRST-HARDWARE-TEST.md), including negative
   forwarding, control frames, reset and PHY concurrency checks. A fixed-link
   carrier or one successful ping is insufficient.
4. Restore P1/P2 and the remaining baseline interfaces, or record maintainer
   agreement to each deferral under [Issue #100](https://github.com/perceival/openwrt-flint3/issues/100).
5. Coordinate the final board changes separately. Official IPQ53xx/BE9300
   integration remains tied to [OpenWrt #23161](https://github.com/openwrt/openwrt/pull/23161),
   which was open and unmerged at this check.

Keep the shipping baseline while this Draft is reviewed. Hardware T1 is failed
and T2–T8 are blocked; no successful bring-up, source-owner sign-off or full
functional parity is claimed.
