# rtl8372n: prepare a minimal DSA and private-PHY bring-up candidate

## Problem

The shipping RTL837x package uses the old SDK-backed driver. Its author has
[deprecated that source and pointed to the DSA refactor](https://github.com/RuijieNetworksCommunity/rtl837x-gsw-driver/issues/2#issuecomment-5935360083),
with removal of the retained register-definition header planned. This candidate
implements the narrow P0 step of [#100](https://github.com/perceival/openwrt-flint3/issues/100),
as discussed in [#99](https://github.com/perceival/openwrt-flint3/issues/99).

## Changes

- Replace the SDK object list with a reduced RTL8372N DSA/register transport.
  Restricted generated headers and PHY/SerDes patch arrays are excluded.
- Serialize PHY/SDS commands; add checked runtime field helpers, per-PHY C22
  pages, reset-safe writing and fail-closed PCS/error handling.
- Add a private PHY layer with actual-port native MMD access and published
  Realtek capability/status behavior; forced 2.5G is unsupported.
- Use CPU-only isolation/flooding and disable hardware learning. Hardware
  bridge offload is omitted; software fallback is the intended P0 path.
- Target Linux 6.18 and add MDIO devres dependency. BE9300's coordinated DTS
  uses `realtek,rtl8372n`; legacy BE6500 nodes are outside this candidate scope.
- Include the source/operation ledger and first-device test procedure.

## Validation

See [P0-STATUS.md](P0-STATUS.md) for current evidence. Local whitespace/style
checks are separate from ARM64 compilation and the OpenWrt package/image gate.
[ARM64 module compilation/modpost passed](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059225390)
for `954a84bd7c` with `W=1` against Linux 6.18.39. The run retains its config,
logs and module. [Full BE9300 OpenWrt image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177)
is still pending. Later documentation-only commits leave this source snapshot
unchanged. Hardware validation is pending.

## Review questions and limitations

- Source-owner/maintainer disposition is still required for flagged provenance
  rows, especially SDS definitions/polarity facts. No authorization or sign-off
  is inferred from repository licensing.
- PHY AN, physical 10G CPU link, operation after cold reset, CPU tag semantics,
  isolation, RMA/BPDU behavior and software fallback need BE9300 bench evidence.
- General VLAN/FDB/MDB/STP, LAG #47, rate limiting #49, statistics, GPIO and EEE
  are not restored. Agree the staged scope before considering a merge.
- Keep the shipping baseline until the replacement passes the agreed gates.
  This is not a proposal to merge a functional reduction into production.
- Final official BE9300 integration remains gated on
  [OpenWrt #23161](https://github.com/openwrt/openwrt/pull/23161).

Do not publish this draft body as a passed-build claim. The agreed P0 build and
provenance gates still apply before opening the PR.
