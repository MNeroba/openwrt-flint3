# P1-B BE9300 hardware test procedure

This procedure is for the P1-B test candidate only. It does not qualify
general VLAN/bridge offload or approve a physical loop test. Use the exact
build revision reported by CI and include it with every result.

**Status (2026-10-08):** `61d286b50a` failed Gate 0 with dynamic L2 flush
timeouts on all four ports. Gates 1–3 were not run. The follow-up polls BUSY
bit 17 and adds mode/restore readbacks. Start with the narrow retest below on
the corrected revision. See [P1-B-REPORT.md](P1-B-REPORT.md).

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
   only on isolated test ports using Gate 2/3 transitions, preserving the
   maintenance path. Do not issue raw MDIO writes or create a loop.

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

Connect a BPDU sender or a managed test switch with STP enabled to one Flint 3
LAN port. Connect a passive capture host to a second Flint 3 LAN port, if
available. Keep every other path disconnected. On the router, capture the
ingress user interface and the bridge interface (substitute the actual DSA
interface names). Record the reset-default reserved-multicast action for the
BPDU group using maintainer diagnostics, if available; this candidate does not
change that global action:

```sh
tcpdump -eni lan1 'ether dst 01:80:c2:00:00:00'
tcpdump -eni br-lan 'ether dst 01:80:c2:00:00:00'
```

The sender should transmit valid IEEE 802.1D BPDUs to `01:80:c2:00:00:00` at a
low rate. Record whether Linux receives them on the expected DSA user port and
whether the active bridge/STP implementation processes them. If available,
record the decoded RTL8_4 forwarding reason and the `offload_fwd_mark` result;
the static-L2 route must not be assumed to produce a trap reason.

At the passive host, capture the same destination while the sender transmits:

```sh
tcpdump -eni <capture-interface> 'ether dst 01:80:c2:00:00:00'
```

**Pass:** the router receives each test BPDU on the correct ingress port; the
Linux bridge can consume valid BPDUs; no copy exits another Flint 3 LAN port;
ordinary unicast/multicast traffic and unrelated reserved groups retain the
P0 behavior. **Fail/blocker:** no CPU delivery, wrong source port, bridge
ignores a valid BPDU due to tag/forwarding metadata, any unexpected LAN egress,
or a setup/readback mismatch. Attach pcap files from both sides and the
timestamped log. Do not proceed to a multi-link or loop test after a failure.

## Gate 2: CIST callback state transitions

Use an isolated temporary bridge or an otherwise unused test bridge port; do
not change the bridge carrying the maintenance session. For a port enslaved
to a Linux bridge, drive state changes with `bridge link` and record both the
command output and the matching `P1-B CIST` readback log:

```sh
bridge link set dev lan1 state blocking
bridge link set dev lan1 state learning
bridge link set dev lan1 state forwarding
bridge link set dev lan1 state disabled
```

Expected ASIC values are: disabled `0`, blocking `1`, learning `2`, forwarding
`3`. Linux LISTENING maps to ASIC BLOCKING. Confirm other user ports and the
CPU port are unchanged. A failed write or mismatched readback is a failure;
include the entire kernel log around the transition.

## Gate 3: fast-age and static-entry preservation

Move a test bridge port from forwarding/learning to blocking so DSA invokes
`port_fast_age`, and record the `P1-B dynamic L2 fast-age completed` message.
Confirm that the static BPDU route still works by repeating Gate 1 afterward.
If the available BE9300 diagnostics can inspect the hardware L2 table, also
record one known dynamic entry before and after the flush and verify that the
dynamic entry disappears while the static BPDU entry remains.

This candidate has no FDB dump callback. If dynamic hardware entries cannot be
observed with existing maintainer tooling, report that limitation explicitly;
the surviving static BPDU route alone proves only static-entry preservation,
not that the dynamic flush removed the intended dynamic entry. Do not mark
fast-age fully qualified without both observations.

## Gate 4: report

Please provide:

1. Exact tested commit and image/build ID.
2. Board revision, active topology, port names, and how the sender generated
   valid BPDUs.
3. Gate 0–3 pass/fail with timestamps and logs.
4. Router and passive-host pcaps for Gate 1.
5. Reset-default reserved-multicast action and, if available, raw/decoded
   RTL8_4 reason, source-port and forwarding-mark evidence, plus L2 table
   snapshots around fast-age.
6. Any regression against the known P0 traffic/link tests.

Only after Gates 0–3 pass should a separate, controlled STP convergence test
with redundant links be considered. It is not part of this first validation.
