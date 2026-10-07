# RTL8372N DSA driver candidate

This package contains an early, RTL8372N-only DSA bring-up candidate for
OpenWrt, with the limited P0 scope described below. See
[PROVENANCE.md](PROVENANCE.md) for source and register origins. The detailed
[first-hardware test plan](FIRST-HARDWARE-TEST.md) covers the P0 build gate,
Flint 3 smoke tests, expected results, failure evidence, and a maintainer report template.
The [pre-PR review and remediation plan](PRE-PR-PLAN.md) records blocking
source findings, build requirements and the staged publication gates.
The [P1 feasibility and implementation plan](P1-RESEARCH.md) maps public
VLAN/L2/STP sources, conflicting field descriptions, dependencies and future
acceptance tests. It does not add P1 runtime support.

## P0 scope

The current code provides:

- MDIO-backed 32-bit register access, chip identification, and software reset.
- Serialized Clause 22, Clause 45, and OCP access to integrated PHY ports 4–7,
  including per-PHY C22 page selection.
- A private PHY driver with native MMD access to the actual port, standard
  autonegotiation and published Realtek 2.5G status decoding. Forced 2.5G is
  explicitly unsupported. Release 7 observed the expected 2.5G/1G/2.5G/1G
  rates on LAN1–LAN4. The author reports physical unplug/replug on all jacks;
  the attached capture shows explicit link-down intervals only for LAN2–LAN4,
  so LAN1's cycle evidence needs clarification.
- Phylink plumbing for internal PHY ports 4–7 and the 10GBASE-R SerDes MACs
  on ports 3 and 8.
- SerDes mode selection and the optional `sds0/1-{rx,tx}-swap` device-tree
  properties, using the small set of open register operations listed in the
  provenance manifest.
- Native RTL8_4 CPU tagging, a VLAN 1/PVID 1 bootstrap, and CPU-only port
  isolation/flooding with learning disabled. Hardware bridge offload is omitted;
  DSA's software bridge fallback is the intended P0 path. Release-7 testing
  passed bidirectional traffic across all six LAN pairs with 30-second iperf3
  runs and 20/20 pings; hardware VLAN/isolation readback remains unverified.

The driver currently supports one CPU port and rejects cascaded DSA ports.
Only RTL8372N is accepted by chip-ID detection. The candidate matches only
`realtek,rtl8372n`; the BE9300 candidate DTS uses that explicit compatible.
Legacy `realtek,rtl837x` nodes, including the current BE6500 node, are not
supported by this P0 replacement. Keep the shipping package on the maintainer's
branch until board compatibility and feature deferrals have been agreed.

## Not in P0

The candidate has no DSA VLAN add/delete callbacks, FDB or MDB offload, STP
offload, LAG, rate limiting, hardware MIB/ethtool counters, GPIO controller,
or EEE support. The initial VLAN 1 setup only bootstraps the switch; it does
not provide general VLAN offload. LAG and rate limiting remain the work
tracked by Issues #47 and #49.

The SerDes path selects the 10GBASE-R mode and applies the board's optional
polarity swaps. It does not contain PHY firmware, vendor patch arrays, or the
full SerDes initialization sequence used by vendor SDKs. The initial diagnostic
run failed T1 because the in-tree RTL8224 PHY driver bound first; release 6
fixed the binding. Release 7's [ARM64 module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454484695)
and [full-image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454704524)
passed. The hardware report confirms all four private PHY bindings, stable
three-warm/one-cold reset behavior, and no port 0–2 power-down warnings. T3's
raw evidence for the LAN1 cable cycle needs clarification. T5 passed all six
software-bridge pairs; counters record 77 new LAN3 RX drops and TCP
retransmissions, without a defined throughput threshold. T6 remains
unverified in hardware. T4's 100-packet check passed on release 6 but was not
repeated in full on release 7; the release-7 logs show successful pings. The
WAN-side `10GBASE-R link not up before USXG_EN` message still appears, while
boot continues and the switch CPU link is up. See
[PHY-PROBE-REPORT.md](PHY-PROBE-REPORT.md) for the full evidence and remaining
P0 gates.

## Device tree

The current Flint 3 node uses `compatible = "realtek,rtl8372n"`, `reg = <29>`,
CPU port 3, and user ports 4–7. The driver consumes these top-level boolean
properties when present:

```dts
sds0-rx-swap;
sds0-tx-swap;
sds1-rx-swap;
sds1-tx-swap;
```

The P0 driver does not provide a GPIO controller and ignores the legacy
`rtl837x,sds0mode`, MDI-reverse, and PHY-TX-polarity properties. SerDes mode is
selected by phylink from the port's `phy-mode`; only `10gbase-r` is accepted
for ports 3 and 8. PHY enable/disable callbacks are limited to internal PHY
ports 4–7; release-7 module and full-image CI passed, and the hardware log
confirms the port 0–2 warnings are gone.

## Build output

The OpenWrt package remains `kmod-rtl837x-dsa`. It builds
`rtl8372n_dsa.ko` (including its private PHY layer) and enables the kernel's
`tag_rtl8_4` DSA tagger. It explicitly depends on MDIO devres. Linux 6.18 is the
supported API; 6.12 compatibility is not claimed.

See [P0-STATUS.md](P0-STATUS.md) and [BUILD-REPORT.md](BUILD-REPORT.md) for
the current evidence and remaining gates. The ARM64 module build and full
BE9300 AP-config OpenWrt package/DTB/image build passed for source revision
`954a84bd7c46dbbb2412eeddfb300aad8b4cff35` ([module run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059225390),
[target image run](https://github.com/MNeroba/openwrt-flint3/actions/runs/37059229177)).
Diagnostic `1b7a32bef2` passed [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197)
and [image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273960255),
but T1 failed at PHY binding. Release 5 failed module compilation because it
referenced a private kernel macro. Release 6 [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397943227)
and [image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397974073)
passed; its candidate module SHA-256 is
`9ccd428ae58f7650d8f7e47455c24250349e840758208e800146663a44037263`.
The maintainer's [release-6 report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6014637917)
passes T0/T1/T2/T4/T7. The [T3/T5 follow-up](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6016542658)
adds partial coverage of connected jacks and the three pairs among LAN1–LAN3.
Release 7's [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454484695)
and [full-image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37454704524)
passed, and the maintainer's [hardware report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6037094988)
confirms T0/T1/T7 and the full six-pair T5 matrix. The report claims T3 PASS;
the attached log shows LAN2–LAN4 link-down samples but no LAN1 down/up event,
so that one evidence item needs clarification. T6 remains unverified in hardware.

## Staged follow-up

1. Clarify the missing LAN1 link-down/up evidence in the release-7 T3 capture.
   Repeat the release-7 100-packet T4 check and record the separate T2 command
   output if needed for the exact-source matrix.
2. Resolve a safe hardware read-only method for T6 VLAN/isolation readback.
   Preserve the T5 RX-drop/retransmission observations in subsequent runs.
3. Prepare the shared table engine and resolve VLAN/L2 field meanings and
   source lineage, following [P1-RESEARCH.md](P1-RESEARCH.md). Source design can
   proceed while P0 hardware results are pending.
4. Prove BPDU CPU delivery, CIST states and dynamic fast-age before enabling
   hardware bridge/VLAN/flags; then add database-correct FDB/MDB management.
   Each runtime step needs a build and its documented bench gates.
5. Reach P1 parity or record agreed deferrals before porting LAG (#47) and
   rate limiting (#49). Restore other baseline interfaces in separate, sourced
   and tested changes or obtain explicit maintainer agreement to defer them.
