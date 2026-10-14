# P1-B BE9300 hardware test procedure

This procedure is for the P1-B test candidate only. It does not qualify
general VLAN/bridge offload or approve a physical loop test. Use the exact
build revision reported by CI and include it with every result.

**Status (2026-10-14):** Perceival reports release 12 hardware results from
source `d0b0c16cd98c54cbe8c5d5dd8e0b89ebe737728f` plus a one-line local
`noop_llseek` build fix. The exact release 12 module/image CI failed because
Linux 6.18.39 no longer declares `no_llseek`; release 13 includes Perceival's
fix and awaits exact-source CI. On the labelled release 12 plus local-fix image,
33/33 P0 setup readbacks and cold/warm flushes passed; direct standalone ASIC
CIST readbacks 0/1/2/3 passed with other-port/isolation/learning fields
unchanged; the keyed BPDU entry matched before and after `flush 4`; and
sender BPDUs reached the lan4 DSA interface in 30-second forwarding, blocking,
listening and learning phases. These results do not prove Linux STP processing,
external-port egress behavior, dynamic entry deletion, or the full P0 traffic
matrix. Perceival reports successful cleanup/restoration. See the
[hardware report](P1-B-REPORT.md) and [test comment](https://github.com/MNeroba/openwrt-flint3/pull/2#issuecomment-6158705157).

The separate bridge Gate 1 capture still needs sender-filtered ingress and
external-observer evidence with Linux STP processing counters/root state.
Bridge-driven callbacks remain **BLOCKED** by absent bridge offload. General
dynamic FDB deletion remains unverified because the interface does not enable
learning, seed entries or dump the general hardware FDB.

## Release 13 build candidate

Release 13 changes the control file operation to Linux 6.18's `noop_llseek`
and increments the package release. The release 12 device run used the same
one-line local source fix, but retain its image identity as a labelled bench
build until exact release 13 CI and image hashes are available. The standalone
commands and cleanup are in [P1-B-BENCH.md](P1-B-BENCH.md).
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
Standalone blocked/listening CPU-facing ingress is now observed; Linux STP processing, external-port egress, bridge-state enforcement and full hardware STP remain separate open gates.

## Gate 2: administrative CIST check; bridge transitions deferred

### Available on this CPU-only candidate

The standalone debugfs cycle has already exercised ASIC disabled, blocking,
listening, learning and forwarding readbacks on lan4, with all other-port
CIST fields, isolation masks and learning limits unchanged. Repeat it on an
exact release 13 image using [P1-B-BENCH.md](P1-B-BENCH.md) after CI passes.
It tests ASIC register state only; it does not change Linux bridge state or
prove data-path blocking/learning behavior.

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

The standalone diagnostic `flush 4` path is available and its exact static
BPDU lookup passed before/after flush on the labelled release 12 plus local
build-fix image. Repeat on exact release 13 after CI passes; compare the
complete BPDU words and `match=1`, and validate BUSY/config fields. Admin
down/up remains the DSA callback path. After link and software forwarding
recover, repeat the separate STP-enabled bridge Gate 1 sender-filtered captures.
The direct lookup proves static-entry preservation for that flush, not dynamic
entry deletion or bridge-path preservation.

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
