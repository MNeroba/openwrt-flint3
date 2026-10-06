# RTL8372N internal-PHY binding failure and correction

Updated: 2026-10-06. The T1 binding failure has an observed cause. The
registration correction is prepared; its builds and hardware rerun are pending.
The later boot hang remains a separate unresolved observation.

## Latest hardware evidence

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

The board also hangs about seven seconds after probe failure, following
`10GBASE-R link not up before USXG_EN`. It produces no further serial output
for minutes and the watchdog does not restart it; a second power cycle
reproduced this. The excerpt does not establish the hang's cause. Do not claim
that fixing PHY selection also fixes this hang; request the complete serial log.

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

## Registration correction (package release 5)

1. Register the managed internal MDIO bus with all automatic scanning masked.
   This prevents a PHY from being created and bound before its matcher is set.
2. Discover each enabled internal user PHY, addresses 4–7, with
   `get_phy_device()`. Retain the actual hardware IDs and existing C22/C45
   discovery semantics; do not fabricate IDs to avoid `realtek.ko`.
3. Set `mdio.bus_match` to permit the private driver identity on this private
   bus/address range, then call `phy_device_register()`. No alternate driver
   is probed first and subsequently unbound/rebound by this registration path.
4. Preserve optional MDIO-node association, reset delays and per-PHY OF
   registration for explicit child PHY nodes. Require unique addresses for
   every enabled internal user port; unsupported/missing children fail.
5. Keep the strict completed-binding gate and feature/MMD/read diagnostics.
   A private probe failure still aborts setup; it cannot become a false pass.
6. Free an unregistered PHY after registration failure. Registered PHYs belong
   to managed bus teardown, including partial registration and setup failure.

The standard Realtek driver remains available for WAN and all other buses.
Reset, PHY/SerDes register programming, forwarding and firmware policy are
unchanged. No restricted header or vendor patch data is introduced.

## Revision-specific build evidence

| Revision | Mainline ARM64 module | Full BE9300 image | Hardware |
| --- | --- | --- | --- |
| Diagnostic `1b7a32bef2`, release 4 | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273928197) | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37273960255) | Maintainer T0 PASS; T1 FAIL; T2–T8 BLOCKED |
| Registration correction, release 5 | New build required | New build required | Not run |

Earlier build passes qualify their own inputs only. The release-5 revision
changes three sources and the package release; it needs fresh compilation,
modpost, packaging and exact-source T0 before flashing.

## Requested next bench run

1. Rebuild the exact release-5 source with the same recorded configuration
   adjustments and pinned inputs. Pass T0; report commit/local deltas,
   configuration/feed lock, image revision and image SHA-256.
2. Keep CPU port 3, internal users 4–7, all four polarity properties and
   `kmod-phy-realtek` for WAN. Use the established recoverable bench.
3. Attach the **complete serial log**, including the previous failing boot if
   available. Save all PHY ID/binding/feature/capability/read errors before
   failed-probe cleanup removes sysfs evidence.
4. T1 requires all four ports to show `bound=1`,
   `driver=RTL8372N internal PHY (P0)` and `private_phy=1`, successful switch/DSA
   setup, and a boot reaching usable management beyond the former hang point.
   Confirm the external WAN PHY still binds to its normal Realtek driver.
5. If discovery or private probe fails, report its first errno and full context;
   keep T2–T8 BLOCKED. If binding succeeds but boot still hangs, report that
   separately with timing and the full PCS/SoC log. Do not infer causation from
   the last printed PCS message alone.
6. If T1 passes, continue the entire [P0 matrix](FIRST-HARDWARE-TEST.md):
   PHY rates/AN, CPU tag/jack mapping, isolation, software forwarding and
   warm/cold reset. Include the boot-hang observation in reset results.
   On a recoverable bench, record any failed-probe/reprobe cleanup errors.

Successful binding alone is not hardware qualification or feature parity.
The dependent P1-A series remains separate until P0 results are available.
