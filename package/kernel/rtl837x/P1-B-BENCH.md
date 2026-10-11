# P1-B standalone ASIC diagnostics — release 12

These opt-in debugfs controls make the existing CIST and flush primitives
reachable without claiming Linux bridge offload. They are bench diagnostics,
not a stable userspace ABI. They default to disabled and require root plus
`CAP_NET_ADMIN` in the initial user namespace. A kernel without DEBUG_FS
continues to run the driver but has no diagnostic files.

## What this can establish

| Observation | Available check | Limit |
| --- | --- | --- |
| ASIC CIST states 0/1/2/3 | Named state override with field readback on a standalone user port | Linux bridge state is unchanged; does not establish DSA bridge/STP offload |
| Unrelated port/isolation/learning fields | Whole CIST word and all nine isolation/learning-limit values | Snapshot, not a traffic test |
| BPDU static entry | Known MAC/VID 1 keyed lookup, three raw words and exact expected-entry match | Does not decode RTL8_4 reason or prove which path delivered a packet |
| Post-flush static preservation | Same keyed lookup before/after an explicit per-port flush | Does not prove deletion of a dynamic entry |
| ASIC-blocked BPDU ingress | Capture the standalone user interface while overridden to blocking/listening | Needs a real sender; result not yet known |
| Dynamic deletion / hardware learning | Not implemented | Learning limits remain zero; no general ASIC FDB dump/seed operation |

There are no arbitrary-register writes, learning-limit changes, L2 entry
insertion/deletion commands, RMA changes or bridge-join stubs in this interface.
The BPDU read uses staging/method registers and a read command under the table
mutex, so it is not a passive raw-register read. It restores the saved lookup
method only after the table engine is idle. Busy/uncertain completion remains
an error, without speculative mode restoration.

## Build and identify

Use the exact release 12 source revision and record the full image/module
SHA-256, kernel version and topology. Release 11 CI and the labelled release
10 format-fixed hardware results do not qualify this new interface. Build
results are linked in the PR; the release 12 device checks start NOT RUN.

OpenWrt's `CONFIG_KERNEL_DEBUG_FS` defaults to enabled. Confirm the files exist
on the installed candidate; mount debugfs if it is not already mounted:

```sh
mount -t debugfs debugfs /sys/kernel/debug
ls -d /sys/kernel/debug/rtl8372n-*
```

Use the actual device directory, for example:

```sh
bench=/sys/kernel/debug/rtl8372n-90000.mdio-1:1d
cat "$bench/status"
```

When disabled, status reports `enabled=0 overrides=0` and the command syntax;
it performs no hardware register or table access. If the directory is absent,
check DEBUG_FS and the driver build; do not substitute raw MDIO commands.

## Prepare one standalone test port

Use serial or another management path that does not depend on the selected
port. Disconnect redundant physical paths. On BE9300, lan4 is normally ASIC
port 4; verify the `netdev` mapping in the enabled snapshot rather than
assuming it on another board. Record its original admin state and bridge
membership so they can be restored after the check.

For a spare lan4 that was originally up and in br-lan:

```sh
ip link set dev lan4 nomaster
ip link set dev lan4 up
printf 'enable 1\n' > "$bench/control"
cat "$bench/status" > /tmp/p1b-before.txt
```

State and flush commands reject CPU/unused ports, ports with upper devices
(bridge/bond/VLAN/etc.) and admin-down ports. Only DSA user ports 4–7 are accepted. A state override affects ASIC CIST
only; the Linux netdev/DSA state does not change. Keep the test port detached
until the override is restored. An administrative down/up can replace an
override through normal DSA operation; record such an event and repeat the
intended snapshot if it occurred.

## CIST readback cycle

Run this in a shell with the `bench` variable above. The trap attempts to
restore all overridden ports if a command fails or the shell is interrupted:

```sh
(
  set -e
  trap 'printf "enable 0\n" > "$bench/control"' EXIT HUP INT TERM
  for state in disabled blocking listening learning forwarding; do
    printf 'stp 4 %s\n' "$state" > "$bench/control"
    cat "$bench/status" > "/tmp/p1b-state-$state.txt"
  done
  printf 'stp 4 restore\n' > "$bench/control"
  cat "$bench/status" > /tmp/p1b-restored.txt
)
```

Expected port-4 ASIC values are disabled 0, blocking/listening 1, learning 2,
forwarding 3. Compare every other CIST field, all isolation masks and learning
limits with the before snapshot. They must remain unchanged. A new
`snapshot_error=...`, missing BPDU result, `match=0`, command error or readback
mismatch is a failure; stop and save full logs. `cat` can exit successfully
with an incomplete snapshot: inspect the error marker and BPDU match.

`restore` uses the current DSA STP state, including a later administrative
transition, rather than an old saved value. `enable 0` restores every tracked
override; if a restoration fails it returns an error and leaves the controls
enabled so restoration can be retried. Save the failure and use the existing
recovery path if MDIO is no longer usable. Removal/shutdown drain debugfs
operations, attempt restoration, then proceed to DSA teardown.

## Static BPDU preservation across flush

After the state cycle has restored the port, keep it standalone and enable
the diagnostic interface for this bounded check:

```sh
(
  set -e
  trap 'printf "enable 0\n" > "$bench/control"' EXIT HUP INT TERM
  printf 'enable 1\n' > "$bench/control"
  cat "$bench/status" > /tmp/p1b-pre-flush.txt
  printf 'flush 4\n' > "$bench/control"
  cat "$bench/status" > /tmp/p1b-post-flush.txt
)
```

The two complete snapshots must report identical BPDU words and `match=1`.
The flush log must show BUSY clear, dynamic mode bits 2:0 zero and restored
mode bits matching the original. On CPU port 3/VID 1 the expected BPDU words
are `0xc2000000,0x20010180,0x00000002`; CPU selection is taken from the DSA
wiring rather than forced by the debug interface. Other CIST/isolation/learning
fields should be unchanged. This qualifies exact static-entry preservation
for the observed flush, not dynamic-entry deletion.

## BPDU receive while ASIC blocking/listening

Perform a separate manual run with one low-rate sender connected to the
standalone test port. Start a full ingress pcap filtered by the sender MAC and
`01:80:c2:00:00:00`. Enable diagnostics, record the baseline, then use:

```sh
printf 'stp 4 blocking\n' > "$bench/control"
cat "$bench/status" > /tmp/p1b-blocked.txt
```

Send a fixed count of valid BPDUs and record ingress counts and any ordinary
traffic behavior. Repeat for `listening` if needed, then use `stp 4 restore`
and `enable 0`. Use a cleanup trap for this manual shell too. A standalone
port has no Linux bridge consuming STP; this check answers ASIC CPU delivery
while blocked, not Linux STP processing. Compare against the forwarding-state
control with the same sender/capture setup. Keep full pcaps, not just their
first packets. An absent BPDU while blocked is evidence to investigate, not
permission to bypass isolation or enable a physical loop.

## Restore the topology and report

Confirm `enabled=0 overrides=0`, restore the original admin state/membership
(for the example: `ip link set dev lan4 master br-lan`), wait for link/software
forwarding recovery, and save the kernel log. Repeat the STP-enabled software
bridge BPDU ingress/no-injected-egress check after recovery, then finish the
outstanding P0 forwarding/standalone-port regression.

Report each state readback, exact BPDU pre/post lookup, blocked/listening CPU
capture and restoration as PASS/FAIL/NOT RUN with the complete snapshots,
command errors, topology and tested identity. The bridge-driven STP matrix,
learning/data-path semantics, dynamic deletion and loop convergence remain
separate deferred gates.
