# P1-B BE9300 hardware test procedure

This procedure is for the P1-B test candidate only. It does not qualify
general VLAN/bridge offload or approve a physical loop test. Use the exact
build revision reported by CI and include it with every result.

**Status (2026-10-11, retest):** the narrow cold/warm Gate 0 now passes on
`c57dba8d11 + local format fix`; both logs show all-port fast-age completion
with retained masks and BUSY clear. Release 11 driver source contains that
same fix and its module/image CI passed, but no device run is attributed to
its exact commit. Gate 1 is reported passed for STP-enabled software-bridge
consumption; the published pcap text is only an excerpt, so sender-filtered
counts/full captures are still requested. Gates 2/3 through bridge STP state
changes are **BLOCKED** by absent bridge offload. Admin down/up covers only
disabled/forwarding and flush completion. See [P1-B-REPORT.md](P1-B-REPORT.md).

## Release 12: newly available standalone diagnostic path

Release 12 adds opt-in, root-only debugfs controls for a standalone user port,
with current-DSA-state restoration and raw all-port snapshots. It also reads
the existing static BPDU entry without adding/repairing it. This makes direct
ASIC states 1/2 and exact static preservation observable while retaining the
CPU-only forwarding/learning policy. See [P1-B-BENCH.md](P1-B-BENCH.md) for
commands and cleanup. These new controls are NOT RUN on hardware; release 11
builds and earlier corrected bench evidence do not validate them.

Bridge-driven callbacks remain blocked until genuine bridge offload exists;
standalone diagnostic results must be reported separately. General dynamic
FDB deletion remains unverified because this interface does not enable
learning, seed entries or dump the general hardware FDB.

## Safety and setup

1. Use a spare Flint 3 / GL-BE9300, serial console or another recovery path,
   and the exact test image. Do not run this first on the production router.
2. Keep the existing P0 recovery procedure available. Record the board
   revision, OpenWrt build ID, kernel version, driver commit, DTS and active
   port names.
3. Keep LAN ports physically disconnected from each other through any other
   switch. For the first BPDU test, connect one sender to one LAN port only.
   A physical loop is explicitly out of scope.
4. Start a timestamped kernel log capture and save the output of `ip -d link`,
   `bridge -d link`, and the boot log before changing bridge state.

## Gate 0: boot and P0 baseline

### Narrow retest of the reported flush failure

Use the same isolated bench topology and preserve an independent management
path. Record one cold boot and one warm reboot, including network bring-up:

1. Confirm all 33 P0 readbacks, the BPDU entry, initial CIST states, four
   private PHY bindings and link rates still pass.
2. Confirm every fast-age request completes without a per-port one-second
   stall. The reported bring-up invokes ports 7, 6, 5 and 4. Save each
   `P1-B dynamic L2 fast-age completed` line: it includes raw control and
   original/dynamic/restored configuration values.
3. `ctrl after & 0x00020000` must be zero. Other command fields may remain
   nonzero. `config dynamic & 0x7` must be zero, and
   `config restored & 0x7` must equal `config before & 0x7`.
4. Record any port whose callback was not invoked. Exercise missing callbacks
   only on isolated test ports using the administrative down/up check in
   Gate 2, preserving the maintenance path. Do not issue raw MDIO writes or create a loop.

If a timeout recurs, stop and attach its phase and raw `ctrl`: this separates
actual BUSY from a retained mask. An I/O read error is a separate failure.
Include CIST transitions, link state, full boot/network logs, exact revision,
and image/module hashes. Do not increase the timeout or proceed to Gate 1.

After this passes, finish the full Gate 0 baseline below on the same image.
Successful completion alone does not verify dynamic removal or static BPDU
preservation; those remain Gate 3.

### Full Gate 0 baseline

After boot, check the driver log for the existing P0 VLAN/PVID/isolation/flood
readbacks and the new P1-B setup messages:

- `P1-B BPDU multicast entry VID 1 routed to CPU port ... (readback PASS)`
- `P1-B CIST port ... state 3 read back` for every enabled user port
- no L2 table, CIST, PHY, SerDes or setup error

Run the previously documented P0 test matrix, including traffic between each
LAN pair through the CPU/software path and the standalone-port negative test.
If any P0 gate regresses, stop here and attach the full log.

## Gate 1: BPDU receive and egress isolation, without a loop

Connect a low-rate BPDU sender to one LAN port and a passive capture host to
another. Keep every redundant path disconnected and use an independent
management path. Record the sender's MAC, ingress/observer ports, bridge
STP state and test start/end times. Use the sender MAC to distinguish injected
BPDUs from BPDUs generated by the router itself.

Save `ip -d link show br-lan` and the current bridge settings before any change.
Record the reset-default reserved-multicast action if maintainer diagnostics
can read it; this candidate does not change the global action.

Use simultaneous captures on the router's ingress DSA user interface and the
external passive host. Substitute the actual names and sender MAC:

```sh
tcpdump -eni lan1 -w /tmp/p1b-ingress.pcap \
  'ether src <sender-mac> and ether dst 01:80:c2:00:00:00'
tcpdump -eni <observer-interface> -w /tmp/p1b-observer.pcap \
  'ether src <sender-mac> and ether dst 01:80:c2:00:00:00'
```

The two commands run on different machines. Start both before the sender;
stop both after the fixed-count send completes. Capture the router bridge
interface as supplementary evidence, but do not require that capture to see
BPDUs consumed by the bridge's link-local receive path.

Run two explicitly labelled, separate cases:

| Bridge configuration | Expected software-bridge behavior | Interpretation |
| --- | --- | --- |
| `stp_state=0` | The Linux bridge may forward received BPDUs to other forwarding ports | Control case; observed egress is not by itself an ASIC isolation defect |
| `stp_state=1`, default group address and `group_fwd_mask=0` | Linux processes valid BPDUs locally; no copy of the injected sender's BPDUs should reach the other LAN port | Required STP-enabled receive/no-egress case |

Do not alter the production bridge to perform this check. Use an isolated
bench bridge, record the setting changes and restore them afterward. Enabling
kernel STP changes software states; it does not enable ASIC bridge/STP offload.

Send a known number of valid IEEE 802.1D configuration BPDUs at a low rate.
Record the actual bridge priority: a high numeric sender priority does not
universally guarantee that it cannot become root. In a no-loop isolated setup,
check the root ID and STP counters/logs before and after the test to establish
Linux processing. Confirm that captures were running on a linked observer;
record drops and adapter re-enumerations. A zero-packet observer file alone
is not sufficient without a working capture/control path.

For complete sender-filtered counts from each saved pcap, use:

```sh
tcpdump -nne -r /tmp/p1b-ingress.pcap \
  'ether src <sender-mac> and ether dst 01:80:c2:00:00:00' | wc -l
```

Repeat on the external observer pcap. Include the complete pcaps or complete
filtered output/counts, not just their first 40 packets. Locally generated
bridge BPDUs are permitted and must be counted separately.

**Pass for the tested path:** each injected BPDU arrives on the correct router
ingress port, Linux STP processes it, and zero injected copies appear on the
external observer with STP enabled. Record ordinary traffic/reserved-group
regressions separately. **Fail/blocker:** absent or wrong-port ingress, valid
BPDUs rejected due to tagging, injected BPDU egress with STP enabled, or a
setup/readback mismatch. If available, capture decoded RTL8_4 reason and
`offload_fwd_mark`; do not infer those from the static route or Linux pcaps.
Blocked-port CPU delivery and full hardware STP remain separate open gates.

## Gate 2: administrative CIST check; bridge transitions deferred

### Available on this CPU-only candidate

On an unused test port with an independent management path, record a log
snapshot, link state and bridge membership, then use administrative down/up:

```sh
ip link set dev lan4 down
ip link set dev lan4 up
```

Allow link negotiation to settle and record full logs and `bridge -d link`.
On BE9300 lan4 maps to ASIC port 4; verify the mapping for other ports.
Expected readbacks are CIST disabled `0` on down and forwarding `3` on up;
fast-age should complete on down if DSA's transition invokes it. Record any
missing callback rather than issuing a raw MDIO write. Check that other ports
and the management path keep working. Restore the original admin state.

This exercises DSA administrative transitions; it does not prove bridge
state offload, ASIC blocking/learning, or data-path enforcement.

### Deferred until genuine bridge offload is implemented

There is no `port_bridge_join` in this candidate. Linux rejects hardware
bridge join and filters switchdev STP attributes for non-offloaded bridge
ports. `bridge link set ... state` therefore cannot drive the ASIC CIST
callback here. Kernel STP also returns `EBUSY` for manual state changes;
with STP off, software state selection may revert a requested state. Even a
persistent userspace-STP software state does not bypass DSA's offload check.

Do not install a userspace STP stub or keep retrying manual bridge commands
as a hardware test. Mark the blocking/listening/learning matrix **BLOCKED**
until the dependent bridge lifecycle work is ready. A successful bridge-join
stub is insufficient: the implementation needs isolation/flood/learning,
leave/fallback handling and CPU/tagging semantics before claiming offload.

The future matrix must cover disabled `0`, blocking `1`, learning `2`,
forwarding `3`, with Linux LISTENING mapped to ASIC BLOCKING, plus unchanged
other-port fields and physical traffic behavior. Document how the STP state
is controlled for that future candidate.

## Gate 3: fast-age completion and static-entry recheck

Administrative down/up from Gate 2 is the currently available flush trigger.
Save each raw completion line and validate BUSY/config fields as in Gate 0.
After link and software forwarding recover, repeat the STP-enabled Gate 1
sender-filtered captures to check that the BPDU path survives the flush.
This is a path-preservation check, not an exact hardware table snapshot.

This image disables hardware learning through the P0 zero learning limits
and has no FDB dump callback. Software `bridge fdb` entries are not ASIC
entries. A known dynamic ASIC entry cannot be assumed to exist, so successful
flush completion does not qualify deletion. Mark dynamic-entry removal and
learning-to-blocking bridge-triggered flush **BLOCKED/UNVERIFIED** until a
controlled learning/table-inspection path is implemented and reviewed.
Do not bypass isolation by raw learning-limit or MDIO writes to seed a test.

For the future full gate, capture a known dynamic hardware entry before and
after a per-port flush, plus a static BPDU-entry lookup before and after;
verify target-port deletion, unrelated-port preservation, static preservation,
and Linux delivery. Restoration from a nonzero mode field also needs separate
controlled evidence: the current bench showed `0 -> 0 -> 0` only.

## Gate 4: report

Please provide:

1. Exact tested commit and image/build ID.
2. Board revision, active topology, port names, and how the sender generated
   valid BPDUs.
3. Gate 0–3 PASS/FAIL/PARTIAL/BLOCKED/NOT RUN with timestamps and logs;
   separate narrow boot checks from the full P0 baseline.
4. Router and passive-host pcaps for Gate 1.
5. Reset-default reserved-multicast action and, if available, raw/decoded
   RTL8_4 reason, source-port and forwarding-mark evidence, plus L2 table
   snapshots around fast-age.
6. Any regression against the known P0 traffic/link tests.

Only after the full bridge-dependent Gates 0–3 pass should a separate, controlled STP convergence test
with redundant links be considered. It is not part of this first validation.
