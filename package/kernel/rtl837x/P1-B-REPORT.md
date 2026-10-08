# RTL8372N P1-B implementation and failure report

Updated: 2026-10-08. This candidate is stacked on [P1-A PR #1](https://github.com/MNeroba/openwrt-flint3/pull/1),
whose parent is [P0 PR #104](https://github.com/perceival/openwrt-flint3/pull/104).
The first P1-B hardware run failed Gate 0. The correction below needs an
exact-source build and BE9300 retest before Gates 1–3.

## Observed hardware failure

Perceival tested `61d286b50ad945c5db135e226374f7cebd5e1269` on BE9300,
Linux 6.18.39, image revision `r35533+305-3b2bc55dcb`:
[bench report](https://github.com/MNeroba/openwrt-flint3/pull/2#issuecomment-6054817334),
[two boot logs and timeline](https://gist.github.com/perceival/872006f2236bc5bc0ccc313318485336).

All 33 P0 setup readbacks, the static BPDU-entry readback, initial CIST fields,
four private PHY bindings and four link rates passed. Both cold and warm
boots then reported a one-second dynamic L2 fast-age timeout on every user
port during network bring-up. The first port reported a completion timeout;
the remaining calls failed in the pre-command wait.

Gate 0 is **FAIL** for that revision. The P0 forwarding/isolation matrix on
this image and P1-B Gates 1–3 are **NOT RUN**. A correct BPDU-entry readback
does not establish CPU delivery, Linux STP processing or absence of LAN egress.

## Defect and correction

The previous helper used `!command` for both polls of `0x53d4`, requiring the
whole register to become zero, including the selected port mask. The public
ZTE RTL8372N map identifies **BUSY at bit 17**, START at bit 16 and the port
mask at bits 9:0. Its flush operation tests BUSY alone. RTLPlayground's
per-port example reads `SFR_DATA_16`, the upper 16-bit half, rather than
requiring the complete 32-bit word to clear.

The zero-word condition is a confirmed source defect. A retained mask with
BUSY clear reproduces the first completion timeout and later pre-command
waits in a register model. The hardware logs contain no raw `0x53d4` values,
so that precise register state still needs confirmation on BE9300.

The correction polls BUSY alone before and after submission, retaining the
one-second limit and transaction mutex. It changes only mode/static-selection
bits 2:0 in `0x53dc`, preserves unrelated bits, and verifies dynamic-only mode
before submitting a command. After BUSY clears it restores and reads back the
previous mode fields. Timeout, I/O failure or restore mismatch cannot produce
a success log. A genuine busy timeout or uncertain command/completion I/O
error leaves the configuration stable instead of racing an active engine.
The package release increases from 9 to 10.

Success logs now include pre/post control and original/dynamic/restored config
words. True timeouts report the phase and last control word; unreadable status
is reported separately. Completion verifies BUSY and configuration only;
dynamic-entry removal and static BPDU preservation remain Gate 3 observations.

## Implemented scope

| Item | Source behavior | Hardware evidence / remaining gate |
| --- | --- | --- |
| CIST | Linux states map to the two-bit port field; LISTENING maps to BLOCKING; writes read back | Initial forwarding and network-bring-up disabled-state readbacks observed. Gate 2 and data-path effects untested |
| Fast-age | Serialized dynamic per-port flush, BUSY polling, mode verification/restoration | Original revision timed out; correction needs Gate 0 retest, then Gate 3 |
| BPDU | Static IVL entry for `01:80:c2:00:00:00`, VID 1, CPU port only; hit/exact three-word readback | Setup readback passed. CPU RX, source port, RTL8_4 reason, Linux processing, no LAN egress and blocked-port delivery untested |
| P0 path | Initial CIST forwarding with CPU-only isolation retained | Setup/PHY/link evidence recorded; P0 forwarding/isolation regression on P1-B unrun |

General hardware bridge/VLAN/PVID callbacks, bridge flags, FDB/MDB offload,
LAG and rate limiting remain outside this candidate. The reserved-multicast
global action is unchanged. The VLAN 1 static route remains an unverified
alternative to a BPDU trap with the BE9300 external CPU and RTL8_4 tagger.
No physical loop is part of the first test.

## Provenance

Register facts are compared with [RTLPlayground](https://github.com/logicog/RTLPlayground/tree/f0aea3dcac056e3274fd39e1c76a7117471c37da)
at `f0aea3dcac056e3274fd39e1c76a7117471c37da` (MIT repository; STP function
marked public domain). BUSY and mode/static fields are compared with the
public [ZTE map](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c#L141-L150)
and [flush operation](https://github.com/cnjn/linux-mainline-zte-zxslc-sr1010/blob/07f8687248578d4be6931c665ff5d08bb6cc3d9d/drivers/net/ethernet/zte/zx279133-rtl8372n.c#L4710-L4758)
at `07f8687248578d4be6931c665ff5d08bb6cc3d9d` (GPL-2.0 source metadata).
The transaction code is original; no reference function body or restricted
header is imported. Existing Air/ZTE lineage and SDS questions remain in
[PROVENANCE.md](PROVENANCE.md).

## Validation

| Check | Result and scope |
| --- | --- |
| Driver whitespace / strict checkpatch | PASS, 0 errors/warnings/checks for the correction |
| Host regression of actual production helper | PASS, 20 register-model cases; also passed with AddressSanitizer and UndefinedBehaviorSanitizer |
| Replay against `61d286b50a` helper | Expected FAIL: zero initial control plus retained completion mask returns `-ETIMEDOUT` |
| Original `61d286b50a` module build | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37684900484); does not validate changed source |
| Original `61d286b50a` BE9300 image | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37684899671); failed hardware Gate 0 |
| Corrected-source module/image CI | Pending new exact-source runs |
| Corrected-source Gate 0 | NOT RUN |
| BPDU reception / egress isolation, Gate 1 | NOT RUN |
| CIST transition matrix, Gate 2 | NOT RUN |
| Dynamic removal / static preservation, Gate 3 | NOT RUN |

Reproduce the host check with:

```sh
sh package/kernel/rtl837x/tests/l2-flush-regression.sh
```

The harness compiles actual `rtl837x_stp.c` against scripted registers. It
covers retained command fields, BUSY transitions, ports 4–7, mode restoration,
I/O failures, mismatches and invalid inputs. Its bounded poll model does not
qualify kernel timing, physical MDIO concurrency or ASIC entry semantics.

## Next bench gate

Build and identify the corrected source, then perform the narrow Gate 0
retest in [P1-B-TEST.md](P1-B-TEST.md): cold boot and warm reboot, including
network bring-up and raw fast-age/config logs. If it passes, finish the P0
regression on that image, then proceed through Gates 1–3. Any error stops the
sequence. A physical loop remains outside the first-device procedure.
