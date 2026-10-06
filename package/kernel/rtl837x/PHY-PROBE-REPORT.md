# RTL8372N internal-PHY binding failure and correction

Updated: 2026-10-06 after the release-6 hardware report. The RTL8224 binding
blocker is resolved on hardware: T0/T1/T2/T4/T7 passed. T3 is partial, T5 was
not run, T6 is software configuration output only, and T8 is CPU-endpoint data,
not switch-forwarding evidence. Release-6 module and full-image CI both passed.
A package-release-7 follow-up now limits PHY power callbacks to ports 4–7; its
CI and hardware results are pending. P0 is not yet fully qualified.

## Earlier diagnostic hardware failure (release 4)

Perceival's [diagnostic-revision report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6006419562) tests `1b7a32bef2`
on GL-BE9300 with the reference AP configuration, `kmod-qca-ssdk` and
`kmod-qca-nss-ppe` removed, and `kmod-phy-realtek` retained for WAN.
T0 passed: both candidate/tagger modules are in the image and SHA-256 was
verified. The report does not publish the new image checksum itself.

The switch ID is `0x83727000`. All four internal PHYs read PHYSID1 `0x001c`
and PHYSID2 `0xcad0` with `err=0`. Port 4's binding summary is:

```text
id=0x001ccad0 clause=C22 bound=1 driver=RTL8224 2.5Gbps PHY private_phy=0
```

The standard Realtek driver has successfully bound where the private driver
was required. Setup correctly rejects it with `-ENODEV`.
**T1 FAIL; T2–T8 BLOCKED.** PHY-ID discovery now has positive evidence;
private feature probing, link negotiation, CPU traffic and forwarding do not.

The operator reported no further release-4 serial output for minutes and
reproduced the stop after another power cycle. That log ends after
`10GBASE-R link not up before USXG_EN`, after switch registration had already
failed on PHY binding. The complete release-6 log contains the same PCS message
during `wan` setup, then continues through service startup and later link
events; the operator also reports usable SSH and five successful boots. This
makes the PCS line insufficient to explain the old stop. The release-4 log has
no panic, hung-task trace, or other evidence that identifies why output stopped,
so the reported earlier hang remains unexplained.

## Release-6 hardware report and remaining warnings

Perceival's [release-6 bench report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-6014637917)
and [redacted serial logs](https://gist.github.com/perceival/f7abebb5395db63b97d3775ebd2c544a) test source `32d958fe17441f849aad085c6d4c26c0a5275b87`, package release 6, kernel 6.18.39, image revision `r35533+282-3b2bc55dcb`.

| Test | Result | Evidence / limit |
| --- | --- | --- |
| T0 | PASS | Exact-source image built with the documented AP config adjustments; both candidate modules are present. The report does not include the image SHA-256. |
| T1 | PASS | Chip ID `0x83727000`; PHY ID `0x001ccad0` and private-driver binding on ports 4–7. |
| T2 | PASS | Four DSA interfaces exist under `br-lan`, conduit `lan`, both modules loaded. |
| T3 | PARTIAL | lan1 2.5G, lan2 1G, lan3 2.5G; lan4 had no peer and no unplug/replug cycle was run. |
| T4 | PASS | 100/100 pings to the router from one 2.5G client; this is router reachability, not LAN-pair forwarding. |
| T5 | NOT RUN | Only one host was available. |
| T6 | NOT VERIFIED IN HARDWARE | VLAN 1/PVID output reflects software configuration; there was no switch-register readback. |
| T7 | PASS | Three warm reboots and one cold power cycle; binding, link rates and pings repeated, without the earlier stop. |
| T8 | RECORDED ONLY | iperf3 used the router as endpoint and was CPU-bound; it is not a switch-forwarding result or acceptance threshold. |

### Warning diagnosis

- **PHY power-down `-22` on ports 0–2:** Release 6's `port_disable` treated every non-SerDes port as an internal PHY. The PHY accessor explicitly accepts only ports 4–7, so it returned `-EINVAL` before issuing an MDIO/PHY command. Package release 7 now checks the supported PHY-port mask in both `port_enable` and `port_disable`. This is a source-level correction; it still needs CI and hardware confirmation that the warnings disappear.
- **Conduit `tx_errors=18446744073709551614` (`2^64−2`):** This is the unsigned result of an existing Qualcomm PPE statistics calculation in `target/linux/qualcommbe/patches-6.18/0342-net-qualcomm-Update-IPQ9574-PPE-driver.patch`: `tx_packets - tx_frames_g`. At the reported sample the second counter exceeds the first by two. That identifies why the displayed value underflows, but not whether the PPE hardware counters are semantically correct. It is outside the RTL8372N driver and should be handled separately with the raw `lan` PPE MIB/ethtool counters; it is not evidence of RTL8372N packet loss.
- **`10GBASE-R link not up before USXG_EN`:** In release 6 this follows `qcom_ppe ... wan: configuring for inband/usxgmii`; the log later continues and the router is reachable. It is a WAN PCS event, not evidence that the switch-to-SoC CPU link failed, and it does not explain the earlier release-4 log ending.

The next hardware run should use the package-release-7 image, confirm the three spurious power-down warnings are gone, finish T3 on every jack including unplug/replug, and run all six LAN pairs for T5 with two hosts. Keep T6 marked unverified unless an agreed read-only hardware readback is available. The author offered to repeat T3/T5 once the second host is available.

## Why matching the same ID is insufficient

Linux 6.18.39's [driver core](https://github.com/gregkh/linux/blob/v6.18.39/drivers/base/dd.c)
iterates matching drivers and stops after successful binding. A private
`match_phy_device` predicate grants no priority over an already registered
Realtek driver. Module autoload priorities 17/18 do not establish runtime order.
Adding another matching PHY ID does not resolve that ordering issue.

The [MDIO bus matcher](https://github.com/gregkh/linux/blob/v6.18.39/drivers/net/phy/mdio_bus.c)
uses the PHY device's `mdio.bus_match` callback after OF matching. This
per-device callback can be set before registration, preventing the standard
ID matcher from selecting RTL8224 on these private-bus devices. A DT change
is unnecessary for the reported BE9300 path, which has no child MDIO node.
No arbitrary vendor-specific PHY compatible override is promised.

## Registration correction (package release 6)

1. Register the managed internal MDIO bus with all automatic scanning masked.
   This prevents a PHY from being created and bound before its matcher is set.
2. Discover each enabled internal user PHY, addresses 4–7, with
   `get_phy_device()`. Retain the actual hardware IDs and existing C22/C45
   discovery semantics; do not fabricate IDs to avoid `realtek.ko`.
3. Set `mdio.bus_match` to select the private driver on this bus/address range,
   then call `phy_device_register()`. Linux checks OF matches before this
   callback, so the override applies to the BE9300 path with no child MDIO PHY
   node and ordinary PHY-ID matching. A vendor-specific child compatible needs
   separate review; the callback cannot override an earlier OF match.
4. Preserve explicit child-PHY association and bus reset delays. Require one
   unique child address per enabled internal user port. This PHY driver uses C22
   page/ability operations, so reject C45 declarations and C45-only discoveries
   before registration. Reject unsupported PHY package nodes explicitly rather
   than reporting a misleading invalid address.
5. Match upstream OF-MDIO's 10 us default reset delay and parse optional bus
   reset delays.
6. Keep the strict completed-binding gate and feature/MMD/read diagnostics.
   A private probe failure still aborts setup; it cannot become a false pass.
7. Free an unregistered PHY after registration failure. Registered PHYs belong
   to managed bus teardown, including partial registration and setup failure.

The standard Realtek driver remains available for WAN and all other buses.
Reset and underlying PHY/SerDes register values are unchanged; release 7 only
limits PHY enable/disable callback access to ports 4–7. No restricted header or
vendor patch data is introduced.

## Revision-specific build evidence

| Revision | Mainline ARM64 module | Full BE9300 image | Hardware |
| --- | --- | --- | --- |
| Diagnostic `1b7a32bef2`, release 4 | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197) | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273960255) | Maintainer T0 PASS; T1 FAIL; T2–T8 BLOCKED |
| Registration correction, release 5 (`614188cff5`) | [FAIL](https://github.com/MNeroba/openwrt-flint3/actions/runs/37395423769): private kernel macro not visible | Cancelled after release-5 module CI failed | Not run |
| Corrected registration, release 6 (`32d958fe17`) | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397943227) | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397974073) | T0/T1/T2/T4/T7 pass; T3 partial, T5 not run, T6 software output only, T8 recorded |
| PHY callback guard, release 7 | Pending | Pending | Pending; retest warnings and remaining T3/T5 |

Release 5 failed compilation because `DEFAULT_GPIO_RESET_DELAY` is private to
kernel `of_mdio.c`. Release 6 uses a named local 10 us constant matching
`__of_mdiobus_register()` and explicitly reports unsupported PHY package nodes.
Release 6 [ARM64 module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397943227) passed with all four objects, `W=1`,
modpost and link success; candidate compilation has no warnings. Module
SHA-256: `9ccd428ae58f7650d8f7e47455c24250349e840758208e800146663a44037263`. [Full-image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37397974073) also passed, and the author reports exact-source T0/T1 success. The new release-7 guard needs its own module/image builds and hardware confirmation.

## Remaining hardware work

1. Build and flash package release 7 from the published PR revision; record the
   exact commit, image revision and SHA-256. Confirm T0/T1 still pass and the
   power-down warnings for ports 0–2 are absent.
2. Complete T3 with a link partner on each jack, record all supported/local/
   partner modes, then unplug/replug each link and verify mapping and recovery.
3. Run T5 across all six directly attached LAN-jack pairs with two hosts,
   testing both directions while capturing router CPU/conduit and port counters.
4. Keep T6 as unverified until there is a safe, documented hardware readback;
   software `bridge vlan show` output is not a switch-register result.
5. Keep throughput figures labeled as router-endpoint CPU traffic. To qualify
   switching performance, use two hosts with traffic that does not terminate
   on the router and capture both switch-port and CPU/conduit counters.

Successful T1 and one router ping do not establish complete P0 qualification
or feature parity. P1-A remains separate until the P0 acceptance matrix and
provenance gates are resolved.
