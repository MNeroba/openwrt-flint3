# RTL8372N P1-B implementation and bench report

Updated: 2026-10-14. This candidate is stacked on [P1-A PR #1](https://github.com/MNeroba/openwrt-flint3/pull/1),
whose parent is [P0 PR #104](https://github.com/perceival/openwrt-flint3/pull/104).
The first P1-B hardware run failed Gate 0. A subsequent, explicitly labelled
`c57dba8d11 + local format fix` run passes the narrow cold/warm retest.
Release 11 (`5969b4b568`) has matching runtime driver source and passing
focused module/image CI. Its exact commit has not been run on the device.
Bridge-driven Gates 2/3 are blocked by the deliberately CPU-only bridge path.

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
waits in a register model. The original logs contain no raw `0x53d4` values. The follow-up below
confirms retained masks with BUSY clear on BE9300.

The correction polls BUSY alone before and after submission, retaining the
one-second limit and transaction mutex. It changes only mode/static-selection
bits 2:0 in `0x53dc`, preserves unrelated bits, and verifies dynamic-only mode
before submitting a command. After BUSY clears it restores and reads back the
previous mode fields. Timeout, I/O failure or restore mismatch cannot produce
a success log. A genuine busy timeout or uncertain command/completion I/O
error leaves the configuration stable instead of racing an active engine.
This BUSY correction was published as package release 10; release 11 fixes
the compilation and host-harness defects below without changing its register
sequence or timeout policy.

Success logs now include pre/post control and original/dynamic/restored config
words. True timeouts report the phase and last control word; unreadable status
is reported separately. Completion verifies BUSY and configuration only;
dynamic-entry removal and static BPDU preservation remain Gate 3 observations.

## Release 10 build failure and release 11 correction

[Perceival reported](https://github.com/MNeroba/openwrt-flint3/pull/2#issuecomment-6105564587)
that `c57dba8d11` does not compile with Linux 6.18.39, aarch64 and
`-Werror=format`. `GENMASK(2, 0)` has type `unsigned long`, so the masked
old configuration passed to `%#x` also has that type. The diagnostic now
uses `%#lx`. Register writes, polling and restoration behavior are unchanged.

The [release 10 image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37745435348)
failed with the same compiler error. The [module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/37745380863)
stopped earlier in the host regression: its empty `linux/errno.h` stub hid
error constants that glibc imports through that header. On Linux the harness
now forwards this include to the system header; on macOS it uses host errno.

The previous host shim used 32-bit masks and had no printf format attribute,
so its passing cases did not catch the kernel type mismatch. The shim now
uses unsigned-long BIT/GENMASK semantics and checks the log wrapper's printf
arguments. With this strengthened harness, the unmodified release 10 helper
fails compilation on the reported diagnostic; release 11 passes all 20 cases,
also with ASan/UBSan. The Linux host regression, ARM64 module build and
BE9300 image build subsequently passed on `5969b4b568`; see Validation.

Results from `c57dba8d11 + local format fix` can be kept as separately labelled
bench evidence. For the exact-source retest, use release 11 and record its
commit and image/module hashes. The follow-up below uses the labelled local
format correction; it is not attributed to the exact release 11 commit.

## 2026-10-11 corrected-source bench retest

[Perceival's follow-up](https://github.com/MNeroba/openwrt-flint3/pull/2#issuecomment-6111966505)
and [evidence gist](https://gist.github.com/perceival/f53a90c6ccf441040401926a968ab1f3)
identify `c57dba8d11 + local format fix`, Linux 6.18.39, module revision
`r35533+306-3b2bc55dcb`. The attached patch changes only `%#x` to `%#lx`,
matching the release 11 driver correction. Release 11 also changes the host
harness, documentation and package release, so this remains separately
labelled device evidence. Image SHA-256 prefixes supplied in the comment are
factory `11984cf88dd3`, sysupgrade `5edc6cfb8991`, initramfs `231449123865`;
full image hashes and a module hash were not supplied in that follow-up.

Both cold/warm logs contain 33/33 passing P0 readbacks, the BPDU-entry
readback and all four fast-age completions. The successive completion values
are `0x80`, `0x40`, `0x20`, `0x10` for ports 7/6/5/4, with BUSY clear. These
values directly explain why requiring the entire command word to clear was
wrong. Configuration before/dynamic/restored is zero in every shown case:
restoration of a nonzero original mode remains untested. The narrow Gate 0 is
**PASS**; the complete P0 traffic/isolation matrix on this image is not
recorded as completed.

Gate 1 is **reported PASS**: BPDUs reach the CPU/software bridge; STP off
allows software forwarding, while STP on consumes the injected BPDUs without
copies on other ports. The STP-off text captures contain 30 sender frames;
the STP-on user-port captures expose only their first 40 packets, which are
the router's own BPDUs. The bridge-interface STP-on capture is empty, which
alone does not establish packet loss or delivery failure for this link-local
path. Sender-filtered full counts/captures and Linux STP processing evidence
are requested to independently substantiate no injected egress; generated
router BPDUs must be counted separately. Decoded RTL8_4 reason/forwarding mark
and CPU delivery with ASIC CIST BLOCKING remain unverified.
CPU reception alone does not distinguish use of the static L2 entry from
reset-default reserved-multicast handling or P0 CPU-only forwarding; that
causal register/path question also remains open.

The attempted `bridge link set state` tests did not reach ASIC callbacks.
The user-STP log does retain the requested software states, including
blocking and learning, but still has no matching ASIC readbacks. This is a
DSA offload boundary, not a PHY, BUSY or STP register failure:

- [Linux DSA bridge join](https://github.com/gregkh/linux/blob/v6.18.39/net/dsa/switch.c#L82-L97)
  returns `-EOPNOTSUPP` when the driver has no `port_bridge_join`.
- [DSA STP attribute handling](https://github.com/gregkh/linux/blob/v6.18.39/net/dsa/user.c#L584-L607)
  rejects attributes for a bridge port that DSA does not offload.
- [Administrative port transitions](https://github.com/gregkh/linux/blob/v6.18.39/net/dsa/port.c#L213-L260)
  still set standalone CIST forwarding/disabled.
- [Manual bridge state control](https://github.com/gregkh/linux/blob/v6.18.39/net/bridge/br_netlink.c#L907-L926)
  returns `EBUSY` with kernel STP. Changing to userspace STP does not bypass
  the separate DSA offload check.

The lan4 administrative down/up log verifies ASIC CIST 0 then 3, flush
completion/configuration readback, and recovery to 1 Gbit/s after about
four seconds. It does not verify ASIC states 1/2 or dynamic entry deletion.
P0 disables hardware learning, and there is no hardware FDB dump; a software
bridge FDB entry is not a suitable ASIC deletion fixture. Full Gates 2/3 are
**BLOCKED**, with administrative-transition/flush evidence recorded as partial.
The test procedure is corrected to express those reachable and deferred gates.

[Linux bridge BPDU receive policy](https://github.com/gregkh/linux/blob/v6.18.39/net/bridge/br_input.c#L383-L393)
explicitly permits forwarding with STP off and otherwise delivers the default
bridge group locally. This report provides no basis for changing the driver
to suppress Linux's normal STP-off forwarding policy or for advertising bridge
offload with a no-op join callback.

## Release 12 standalone diagnostic extension

Release 12 added `rtl837x_p1b.c`, an original, opt-in debugfs interface.
Controls default to disabled, require root and initial-namespace NET_ADMIN,
and accept CIST overrides/flush only for admin-up standalone DSA user ports
4–7. RTNL serializes controls with DSA administrative transitions; the shared
table mutex protects snapshots/lookup/flush operations. CPU/unused/bridge
member ports are rejected. Disable restores every tracked override using the
current DSA state; a failed restore remains an error and can be retried.
Protected debugfs operations are drained before DSA remove/shutdown.

Status exposes the full CIST word, all nine isolation/learning-limit registers,
raw flush status/config and a keyed VID 1 BPDU lookup with three raw words and
exact expected-entry matching. The lookup does not insert/delete/repair any
entry. Saved lookup method is restored and read back only after confirmed table
idle; uncertain busy completion leaves it stable and reports an error. This
also hardens the existing setup readback restoration after uncertain
completion. No new register definitions or third-party source bodies are
imported.

### Release 12 compile failure and release 13 correction

Both exact release 12 focused builds failed in `rtl837x_p1b.c`: Linux 6.18.39
no longer declares `no_llseek` and the compiler suggested `noop_llseek`.
The [ARM64 module run](https://github.com/MNeroba/openwrt-flint3/actions/runs/38166231888)
and [BE9300 image run](https://github.com/MNeroba/openwrt-flint3/actions/runs/38166239822)
reproduce the same source error. Perceival confirmed that changing the file
operation to `.llseek = noop_llseek` builds both the module and full image.
Release 13 carries that exact source fix and increments the package release.
Its exact-source [ARM64 module CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/38511958507)
passes with `W=1`, real DEBUG_FS APIs, modpost and module linking. The exact
[BE9300 image CI](https://github.com/MNeroba/openwrt-flint3/actions/runs/38512016012)
is still running; do not treat the complete-image build as passed yet.

### 2026-10-14 standalone hardware retest

[Perceival's release 12 report](https://github.com/MNeroba/openwrt-flint3/pull/2#issuecomment-6158705157)
and [logs, captures, sender script and one-line build patch](https://gist.github.com/perceival/a0fa27d931b863d2054aa28ef6ffae73)
identify source `d0b0c16cd98c54cbe8c5d5dd8e0b89ebe737728f` plus only the
`no_llseek` to `noop_llseek` build fix, Linux 6.18.39, image revision
`r35533+311-3b2bc55dcb`. The supplied 12-character image SHA-256 prefixes are
factory `d0e02ce97655`, sysupgrade `b85a40add754`, initramfs `01475fe19eb6`;
full hashes were not supplied. Keep this labelled as the release 12 image plus
local build patch; it is not described as an exact release 13 package/image.

| Check | Observed result | What it establishes |
| --- | --- | --- |
| Cold boot and warm reboot | 33/33 P0 readbacks, BPDU setup readback, CIST ports 4–7 at state 3; flushes on ports 7/6/5/4 complete with BUSY clear and mode bits restored; no reported warnings, oops or timeouts | Narrow boot/fast-age baseline only; full P0 traffic/isolation matrix remains unreported |
| Standalone CIST cycle on lan4 / ASIC port 4 | Readbacks: disabled 0, blocking 1, listening 1, learning 2, forwarding 3, restore 3. Only port 4's CIST bits changed; all nine isolation masks and learning limits stayed identical | Direct register-state behavior for this test port; not Linux bridge offload or traffic-state enforcement |
| Static BPDU entry across `flush 4` | Complete before/after snapshots were byte-identical; expected words `0xc2000000,0x20010180,0x00000002`, `match=1`; BUSY clear and mode 0 before/after | Exact static entry survived the observed flush; does not prove a dynamic entry was deleted |
| BPDU ingress while standalone ASIC port was blocked/listening | Same sender `e0:70:ea:f3:b9:52`, 30-second phases: forwarding 29, blocking 29, listening 30, learning 30, forwarding again 29 frames captured on lan4 | BPDUs reached the CPU-facing DSA user interface in each ASIC CIST state; no Linux STP processing or external-port egress conclusion |
| Cleanup | `enabled=0 overrides=0x0`; lan4 reattached to br-lan | Diagnostic override cleanup was reported successful |

The standalone ingress capture directly answers the earlier blocked/listening
CPU-delivery question for this path. It does not demonstrate that Linux STP
processed those packets, identify the RTL8_4 trap reason, or show whether any
copy egressed another LAN port. The separate bridge Gate 1 sender-filtered
external-observer capture and full P0 traffic/isolation regression remain
open. Dynamic ASIC entry deletion, hardware learning, bridge-driven STP and
loop convergence were not run.

The same report found that BusyBox `printf 'enable 1\n' > control` returned
status 1 even though the write took effect, which breaks the guide's `set -e`
examples. The bench guide now uses `echo` for every control-file write and
explains why. This is a test-procedure correction, not a driver change.

## Implemented scope

| Item | Source behavior | Hardware evidence / remaining gate |
| --- | --- | --- |
| CIST | Linux states map to the two-bit port field; LISTENING maps to BLOCKING; writes read back | Standalone lan4 direct test reads 0/1/1/2/3 and restores 3; other-port fields unchanged. Bridge-driven STP enforcement remains blocked |
| Fast-age | Serialized dynamic per-port flush, BUSY polling, mode verification/restoration | Cold/warm per-port completion and standalone `flush 4` observed with BUSY clear. Static BPDU entry verified unchanged across standalone flush. Dynamic deletion and nonzero-mode restore remain unverified |
| BPDU | Static IVL entry for `01:80:c2:00:00:00`, VID 1, CPU port only; hit/exact three-word readback | Setup and post-flush lookup match; standalone CPU-facing ingress captured 29–30 sender BPDUs per 30-second ASIC-state phase. Linux STP processing, external egress and decoded tag reason remain unverified |
| P0 path | Initial CIST forwarding with CPU-only isolation retained | 33/33 cold/warm setup readbacks and link recovery observed; full P0 traffic/isolation regression on tested release 12 plus local build fix not supplied |

General hardware bridge/VLAN/PVID callbacks, bridge flags, FDB/MDB offload,
LAG and rate limiting remain outside this candidate. The reserved-multicast
global action is unchanged. The VLAN 1 static route has BE9300 setup and
standalone CPU-facing ingress evidence. Decoded trap-reason semantics, the
Linux bridge path and external-port egress behavior remain unverified. No
physical loop is part of this test stage.

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
| Compile replay of release 10 with strengthened harness | Expected FAIL with `-Werror=format`; current helper passes |
| Replay against `61d286b50a` helper | Expected FAIL: zero initial control plus retained completion mask returns `-ETIMEDOUT` |
| Original `61d286b50a` module build | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37684900484); does not validate changed source |
| Original `61d286b50a` BE9300 image | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/37684899671); failed hardware Gate 0 |
| Release 10 `c57dba8d11` module/image CI | FAIL: host Linux errno shim / driver printf type, respectively; see links above |
| Release 11 `5969b4b568` Linux host / ARM64 module | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/38116485074), including `W=1` / modpost |
| Release 11 `5969b4b568` BE9300 image | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/38116487791) |
| Release 11 PR profile | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/38116555421) |
| Broad inherited CI matrix | Not all green; sampled failures include toolchain GDB, mt76 API errors and kernel-patch refresh checks; separate from focused image/module results |
| Labelled `c57dba8d11 + local format fix` narrow Gate 0 | PASS cold/warm; does not complete the full P0 matrix |
| Release 12 ARM64 module CI | [FAIL](https://github.com/MNeroba/openwrt-flint3/actions/runs/38166231888): Linux 6.18.39 no longer declares `no_llseek` |
| Release 12 BE9300 image CI | [FAIL](https://github.com/MNeroba/openwrt-flint3/actions/runs/38166239822): same `no_llseek` compile error |
| Release 13 ARM64 module CI | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/38511958507): Linux 6.18.39, `W=1`, DEBUG_FS, modpost and module link |
| Release 13 BE9300 image CI | IN PROGRESS ([run](https://github.com/MNeroba/openwrt-flint3/actions/runs/38512016012)); complete image not yet confirmed |
| Release 13 PR profile | [PASS](https://github.com/MNeroba/openwrt-flint3/actions/runs/38511962435) |
| Release 12 plus Perceival local `noop_llseek` build fix, standalone diagnostics | PASS for direct CIST 0/1/2/3 readbacks, all-port snapshot stability, static BPDU preservation across flush, and 29–30 CPU-facing ingress BPDUs per 30-second state phase; does not qualify bridge STP or egress |
| Exact `5969b4b568` device run | NOT RUN; runtime driver source matches attached bench fix |
| Bridge BPDU reception / egress isolation, Gate 1 | STP-enabled software-bridge behavior was reported PASS on the earlier labelled build; full sender-filtered ingress/external-observer evidence and Linux STP processing still requested. The new standalone ingress test is separate |
| CIST transition matrix, Gate 2 | Direct standalone ASIC readback 0/1/2/3 PASS with unchanged other-port/isolation/learning fields; bridge-driven enforcement remains BLOCKED |
| Dynamic removal / static preservation, Gate 3 | Static BPDU entry exact pre/post-flush readback PASS on release 12 plus local build fix; dynamic deletion remains BLOCKED/UNVERIFIED |

Reproduce the host check with:

```sh
sh package/kernel/rtl837x/tests/l2-flush-regression.sh
```

The harness compiles actual `rtl837x_stp.c` against scripted registers. It
covers retained command fields, BUSY transitions, ports 4–7, mode restoration,
I/O failures, mismatches and invalid inputs. Its bounded poll model does not
qualify kernel timing, physical MDIO concurrency or ASIC entry semantics.

## Next bench and development steps

1. Finish the exact-source release 13 ARM64 module and BE9300 image CI after
   replacing the removed Linux 6.18 `no_llseek` API. Keep Perceival's device
   results labelled as release 12 plus the same one-line local build fix.
2. Complete the full P0 forwarding/standalone-port regression on that image,
   with an independent management and capture path.
3. Close the separate bridge Gate 1 evidence gap with sender-filtered ingress
   and external-observer captures plus Linux STP processing evidence. The
   standalone ASIC-state test establishes CPU-facing ingress only.
4. Implement genuine bridge lifecycle, learning/flood/isolation and fallback
   behavior before claiming bridge-driven ASIC STP tests. Dynamic deletion
   still needs a controlled learning fixture and hardware FDB observation.
   Do not change learning or forwarding policy just to create a passing test.

A physical loop and STP convergence remain outside this stage. No new
production-driver defect is established by the bridge-state test blockage.
