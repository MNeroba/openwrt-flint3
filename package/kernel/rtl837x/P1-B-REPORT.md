# RTL8372N P1-B implementation and bench report

Updated: 2026-10-11. This candidate is stacked on [P1-A PR #1](https://github.com/MNeroba/openwrt-flint3/pull/1),
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

The next candidate adds `rtl837x_p1b.c`, an original, opt-in debugfs interface.
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
entry. Saved lookup method is restored only after confirmed table idle;
uncertain busy completion leaves it stable and reports an error. This also
hardens the existing setup readback restoration after uncertain completion.
No new register definitions or third-party source bodies are imported.

This can make direct ASIC blocking/learning readbacks, standalone blocked BPDU
CPU delivery and exact post-flush static-entry preservation testable. It does
not implement Linux bridge offload, dynamic learning/FDB fixtures or decoded
tag diagnostics. New hardware checks remain NOT RUN. The focused CI now
explicitly enables DEBUG_FS to compile its real API path. Release 12 builds
are pending; release 11 successes apply only to the preceding source.

[P1-B-BENCH.md](P1-B-BENCH.md) supplies the standalone-port commands, cleanup,
expected words and evidence limits. The diagnostic interface is temporary
bench tooling, not a stable userspace ABI; review it before an upstream series.

## Implemented scope

| Item | Source behavior | Hardware evidence / remaining gate |
| --- | --- | --- |
| CIST | Linux states map to the two-bit port field; LISTENING maps to BLOCKING; writes read back | CIST 0/3 readbacks and lan4 admin recovery observed on labelled corrected build. ASIC 1/2 and bridge-driven state enforcement BLOCKED |
| Fast-age | Serialized dynamic per-port flush, BUSY polling, mode verification/restoration | Corrected cold/warm ports 7/6/5/4 and lan4 admin flush complete with retained masks. Nonzero-mode restore, dynamic deletion and exact static readback after flush unverified |
| BPDU | Static IVL entry for `01:80:c2:00:00:00`, VID 1, CPU port only; hit/exact three-word readback | Setup readback and software-path CPU RX observed; STP-enabled consumption/no injected egress reported. Full sender-filtered counts requested; decoded tag reason and ASIC-blocked delivery unverified |
| P0 path | Initial CIST forwarding with CPU-only isolation retained | 33/33 cold/warm setup readbacks and link recovery observed; full P0 traffic/isolation regression on corrected P1-B not supplied |

General hardware bridge/VLAN/PVID callbacks, bridge flags, FDB/MDB offload,
LAG and rate limiting remain outside this candidate. The reserved-multicast
global action is unchanged. The VLAN 1 static route has partial BE9300 external-CPU/software-bridge
evidence; trap-reason semantics and ASIC-blocked delivery remain unverified.
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
| Release 12 module/image CI | Pending new exact-source runs |
| Release 12 standalone diagnostic hardware | NOT RUN |
| Exact `5969b4b568` device run | NOT RUN; runtime driver source matches attached bench fix |
| BPDU reception / egress isolation, Gate 1 | Reported PASS with STP enabled on labelled corrected build; full sender-filtered capture counts requested |
| CIST transition matrix, Gate 2 | PARTIAL admin 0/3; ASIC blocking/listening/learning via bridge BLOCKED |
| Dynamic removal / static preservation, Gate 3 | Flush completion PASS on labelled corrected build; dynamic deletion BLOCKED, post-flush static-entry/path evidence incomplete |

Reproduce the host check with:

```sh
sh package/kernel/rtl837x/tests/l2-flush-regression.sh
```

The harness compiles actual `rtl837x_stp.c` against scripted registers. It
covers retained command fields, BUSY transitions, ports 4–7, mode restoration,
I/O failures, mismatches and invalid inputs. Its bounded poll model does not
qualify kernel timing, physical MDIO concurrency or ASIC entry semantics.

## Next bench and development steps

1. Keep the tested identity as `c57dba8d11 + local format fix`; use exact release
   11 for the next build and supply full image/module hashes. A docs-only
   follow-up does not invalidate either identity's recorded evidence.
2. Complete the outstanding P0 forwarding/standalone-port regression on the
   corrected P1-B image, with an independent management/capture path.
3. Supply complete sender-filtered ingress/observer counts for both STP states,
   topology/time window and STP processing evidence. Reuse saved full pcaps if
   available; a fresh run is needed only where the capture was interrupted.
4. Use admin down/up on an isolated port as the limited CIST/flush check, then
   repeat the STP-enabled BPDU path test after recovery. Do not retry the
   inaccessible bridge-driven ASIC states on this CPU-only build.
5. Implement the separately staged bridge lifecycle, learning/flood/isolation
   and fallback work before full bridge-controlled ASIC STP tests. Dynamic
   deletion needs controlled learning and hardware entry observation; keep
   this unverified until that fixture exists. Do not open learning or hardware
   forwarding simply to make a test pass.

A physical loop and STP convergence remain outside this stage. No new
production-driver defect is established by the bridge-state test blockage.
