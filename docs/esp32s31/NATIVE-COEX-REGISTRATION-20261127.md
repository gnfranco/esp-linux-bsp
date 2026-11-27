# S31 native radio: coexistence adapter registration gate

Date: 2026-11-27. Experimental source-only follow-up to
[NATIVE-RADIO-POC-20261126.md](NATIVE-RADIO-POC-20261126.md).

## Scope and architecture

The next bounded experiment exercises the real `esp_coex_adapter_register`
from the pinned official ESP-IDF v6.1 S31 `libcoexist.a`, beyond metadata.
It tests rejection of invalid arguments, registration as a process-private
state transition, and the behavior of repeated registration. It deliberately
does **not** attempt a working OSI callback table, ROM sharing, coex startup,
PHY, Wi-Fi, Bluetooth, RF or any privileged resource. All callback slots
are NULL, including in the version/magic-valid table. No callback is invoked.
The external `g_coa_funcs_p` is private BSS storage, not the hardware/ROM
global. No persistent registration survives the process.

One reason to test this boundary early: the inspected vendor implementation
checks the pointer first, then version 2 at offset 0 and magic 0xDEADBEAF
at offset 76. It stores the pointer, not a copy. Subsequent calls return
success without rechecking the supplied table or replacing the first pointer.
It does not validate individual callbacks, run the separate adapter MD5
check or synchronize the check/store. Future radio services must enforce their
own single-writer registration, callback completeness, lifetime and teardown
contract; return zero is not evidence of a functional adapter.

Primary references, pinned to ESP-IDF v6.1 commit
`fff9895c82d744c7237be8847347bdd1b07c6643`:
- [Coexistence adapter header](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/include/private/esp_coexist_adapter.h),
  Git blob `15e8ef7114fefeba8671c51a42f0e60ee9de0ec8`; RV32 table size 80.
- [Private registration prototype](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/include/private/esp_coexist_internal.h).
- [S31 adapter implementation](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/esp32s31/esp_coex_adapter.c).
- [Vendor diagnostic adapter](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/src/lib_printf.c).
- Archive provenance and hashes: [v6.1 lock](../../dependencies/s31-native-radio.lock.json).

Relocation inspection of `coexist_api.o` and independent Codex CLI
review (workspace-write sandbox, no hardware, tracked edits or network)
confirmed that the only call relocations in registration are `puts`
and `coexist_printf`. The latter is implemented by the probe as a bounded,
working stdout logger for the two observed %x diagnostics, rejecting unsupported
formats; it is not a silent success stub. No generic libc or OSI runtime is
provided. The independent review also found identical registration code and
relocations in the separately pinned v6.1.1 archive. That newer archive was
not linked or physically executed in this test.

## Rebuild

Use existing pinned archive checkout and compiler, outside the source
tree. Fetch exactly the official header, verified by Git blob hash in the
build script (no vendor header or compiled binary redistributed):

```sh
CC="$HOME/toolchains/s31-linux-gcc-15.2.0-5/bin/riscv32-esp-linux-musl-gcc"
D="$HOME/deps/s31-radio"
H=/tmp/s31-coex-header
mkdir -p "$H"
gh api 'repos/espressif/esp-idf/contents/components/esp_coex/include/private/esp_coexist_adapter.h?ref=fff9895c82d744c7237be8847347bdd1b07c6643' \
  --jq .content | base64 -d > "$H/esp_coexist_adapter.h"
experiments/s31-native-radio/build-coex-register.sh "$D" "$CC" \
  "$H/esp_coexist_adapter.h" /tmp/s31-coex-build
```

Build result: 2/2 independent builds, byte identical; GCC 15.2.0-5,
freestanding static ELF32 RV32IMAFC, ILP32F, no undefined symbols or
interpreter, no floating-point opcodes in linked disassembly.
ELF 5,952 bytes, RX LOAD 1,828 bytes and RW LOAD 256 bytes memory
(240 bytes file), no executable stack.
SHA-256:
`4904d2c32e2a72efd76f7e74003461e85a8ccfd001203b22fd6b90bd9778d3be`.
The only retained global code symbols are the probe entry point,
vendor registration and the two diagnostic functions. Final disassembly
shows load/store through process-local table addresses; no MMIO,
ROM entry or indirect callback call. Passing on the no-F Lab v1 baseline
does not qualify the FPU or the ILP32F runtime in general.

## Physical gate

On authorized Function-CoreBoard-1, the local rollback image checksums
were verified and UART availability checked. The existing RAM-only runner
supports an additional profile with markers specific to this experiment:

```sh
python3 experiments/s31-native-radio/run-uart.py --port /dev/ttyUSB0 \
  --artifact /tmp/s31-coex-build/coex_register.elf \
  --sha256 4904d2c32e2a72efd76f7e74003461e85a8ccfd001203b22fd6b90bd9778d3be \
  --probe coex-register
```

| Check | Run 1 | Run 2 |
|---|---|---|
| RV32 Linux 6.18, CramFS read-only, CPUs 0-1 | PASS | PASS |
| Artifact SHA-256 on target | PASS | PASS |
| Empty initial pointer | PASS | PASS |
| NULL, wrong version, wrong magic rejected (0x102) | PASS | PASS |
| Exact process-local pointer registered (0) | PASS | PASS |
| Repeated invalid version and NULL ignored, original pointer retained | PASS | PASS |
| Probe process exit 0, CPUs 0-1 and eth0 present after | PASS | PASS |
| Temporary target files removed | PASS | PASS |

MemAvailable before/after: 10,360/10,008 KiB and 10,356/10,064 KiB.
Not a leak measurement. Ethernet presence is not Ethernet traffic testing.
No flashing, changes to board wiring, USB writes, or RF activity.
The UART was free at the end. Raw evidence and binaries are private,
preserved in Linvora-dev `~/deps/s31-research/native-radio-next-20261127/`
with map, disassembly, pinned header, two build outputs, and bench log
hashes. No radio binary is committed or released.

## Separate F-only + DMA integration experiment

On the Linux fork, cherry-picked independent DMA RFC
`1c2d2160bff76b11b81c2dc4bd5ef1ecb7275094` onto F-only v2
`060679ff35be74800f0fcb792296e01e5ff524ee`, keeping both upstream and
F-only branches unchanged. New experimental branch
`experiment/s31-lab-f-only-v2-dma`, commit
`d3e404995222d8933317433c55bb355b126a2cb8`.
Independent review confirmed the patch applies cleanly, S31 compatible and
cache config. `git diff --check` and kernel checkpatch: no errors/warnings.
Using GCC 14.1.1 and preserved F-only v2 config, olddefconfig produced no
configuration diff. Targeted build of `arch/riscv/mm/dma-noncoherent.o`
passed (SHA-256
`a7435f5ecb657001628ee2a445f266a63246a7ce16579e070699660c1be6de54`).
Config digest
`ea4ef1490fb8054f16ad970e72df37f0adeae361ff84096c77bee5c593c20500`.
This is **object compilation only**, not a linked XIP kernel, full build,
FPU qualification or a physical combined test. The original DMA RFC remains
independent. The primary kernel checkout was restored to F-only v2.

The partial-copy sigreturn fault test remains inconclusive. Prior analysis
shows the failed copy need not commit a prefix and a later dirty FP save can
obscure a partial update. A next independent diagnostic should demonstrate
actual partial-copy commitment and normalization inside the restore path,
without printing raw register data; additionally check stop attribution
with PTRACE_GETSIGINFO. No FPU claim is extended by the registration test.

## Upstream status and next gate

On 2026-11-27, official BSP/Linux/U-Boot/OpenSBI refs remained unchanged
from the previous audit. All seven open PRs were still open without
maintainer reviews. No public merge of the internally accepted OpenSBI
fix observed. No new upstream PR or comment created.

Next: map the complete coex OSI v2 callbacks and state/ROM dependencies;
implement and stress a bounded thread-context semaphore with monotonic
timeouts, explicit cancellation/teardown, and rejection of IRQ-only
operations where unsupported. Keep IRQ semantics and DMA-capable internal
memory under a Linux-owned broker, not a userspace simulation or fake
successful callbacks. Qualify the F-only/DMA combined kernel and remaining
negative sigreturn test independently before any probe using real FP,
and defer RF initialization until all selected dependencies and resource
ownership paths are established.
