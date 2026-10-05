# RTL8372N internal-PHY probe diagnostics

Updated: 2026-10-05. This revision improves diagnosis and corrects the binding
gate. It does not establish the root cause or a hardware fix for the T1 failure.

## Reported failure

Perceival's [2026-10-04 bench report](https://github.com/perceival/openwrt-flint3/pull/104#issuecomment-5983997672)
uses source `908810c09bd9adfbbc7d25437a9d50b55b2de940`, Linux 6.18.39,
CPU port 3 and user ports 4–7, with all four SerDes polarity properties enabled.
Sysupgrade SHA-256:
`6acb8d57df6023ae10417048c5390cbbf044c4f835a5bef262d8e63fb4585a5e`.
The source through pre-diagnostic head `7be7f8d541` has the same driver inputs.

T0 passed. The installed image reads chip ID `0x83727000`, then reports
`port 4 did not bind to the private PHY driver` and aborts setup with `-ENODEV`.
No DSA user ports are created. **T1 FAIL; T2–T8 BLOCKED** is the recorded result.
The report has no oops, pstore record or MDIO/SDS timeout. Successful outer
switch-register access does not establish successful internal-PHY transactions.

## Source analysis

The original check combines a missing `phy_device` and an unexpected
`phy->drv` into one message, and stops at the first failing port. It cannot
distinguish the two alternatives proposed in the bench report.

Linux 6.18.39 [phylib](https://github.com/gregkh/linux/blob/v6.18.39/drivers/net/phy/phy_device.c)
can turn a C22 ID-read `-EIO` into `-ENODEV`, allowing scanning to continue.
The candidate's internal access also returns `-EIO` on nonzero command status,
which does not require a timeout. Actual read values/errors are therefore needed.

The [driver core](https://github.com/gregkh/linux/blob/v6.18.39/drivers/base/dd.c)
iterates matching drivers and stops at a successful probe. The private driver's
bus match grants no priority over an already registered Realtek driver. Autoload
17 versus 18 does not prove the runtime registration order. A Realtek driver
binding to the internal PHYs is plausible, not yet observed; keep the Realtek
module available for the external WAN PHY.

There is also an independent binding-check defect: `phy_probe` assigns
`phy->drv` before feature/EEE initialization and can return an error without
clearing it. This pointer alone does not prove that binding completed.
`device_is_bound` explicitly requires the device lock and reports successful
binding; the revised gate uses it and checks the actual device-driver identity
as well as the PHY-driver identity.

## Diagnostic change

- Log C22 `MII_PHYSID1`/`MII_PHYSID2` reads actually issued by the existing scan,
  with port, register, value and errno. A nonzero `err` means the displayed zero
  value is a placeholder, not a successful PHY ID read. If PHYSID1 fails,
  phylib may not issue PHYSID2; the revision adds no extra diagnostic reads.
- Report every available user port after successful bus registration: absent
  PHY, or ID, C22/C45 mode, completed-binding flag, actual driver name and
  private-PHY identity. Inspect binding under the child device lock. Continue
  collecting port results and then retain the existing `-ENODEV` setup cleanup.
- Log private feature-probe entry, C22 ability or 2.5G capability failures and
  the successfully read capability value. Native MMD and child-bus C22/C45 read
  failures retain their errno and address context; repeated errors are limited.
- Log read-completion failures with port, MMD, register and last control value.
  On a polling read error, that value may still be the issued command rather
  than a fresh hardware status; use errno and the full surrounding log together.
- Bump package release from 3 to 4 so the diagnostic package is identifiable.

The register map, scan order, reset, PHY power, SerDes and forwarding policy
are unchanged. No alternate-driver rebind, fabricated ID, SDK table, firmware,
error suppression or relaxed binding gate is added.

## Next bench run

1. Rebuild the exact new source and pass T0. The baseline's module/image passes
   do not validate the diagnostic revision. Record commit, any local deltas,
   configuration/feed lock and image SHA-256; verify release 4 and both modules.
2. Use the established recovery/management path, CPU port 3, users 4–7 and all
   four polarity properties. Keep `kmod-phy-realtek` for WAN and the same config
   adjustments as the previous run; do not change module ordering as a workaround.
3. Repeat T1 and attach the full boot log, including every `PHY ID`, `PHY binding`,
   `private PHY feature probe`, capability and PHY read-error line. An early bus
   registration failure may prevent binding summaries; its errno and earlier
   scan/read logs still matter. Save logs before failed-probe devres cleanup
   removes private-bus sysfs nodes.
4. If T1 fails, keep T2–T8 BLOCKED and report the first failing operation. If T1
   passes, continue the complete [P0 matrix](FIRST-HARDWARE-TEST.md), including
   private-driver binding, PHY rates, CPU traffic, isolation and warm/cold reset.

| Observed result | Next investigation |
| --- | --- |
| ID read fails or no usable ID; PHY absent | Internal command status, C22/OCP mapping, reset/power readiness; use any C45 scan evidence already logged before changing discovery. |
| PHY present and a different driver is actually bound | Make private-bus selection deterministic before registration; preserve WAN driver behavior and repeat T0/T1. |
| PHY present but `bound=0`, including `private_phy=1` | Inspect feature/MMD/probe errors; the old pointer check could accept a failed probe. |
| All PHYs bound to the private driver | Continue T2–T8; successful binding alone does not qualify links or forwarding. |

Mainline ARM64 and full OpenWrt image rebuilds are pending at preparation.
No hardware result for this diagnostic revision is claimed.
