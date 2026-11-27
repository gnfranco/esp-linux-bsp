# S31 native coexistence: thread-context counting semaphore

Date: 2026-11-27. Experimental component following the
[registration gate](NATIVE-COEX-REGISTRATION-20261127.md).
Source-only, no vendor library changes, RF initialization, hardware MMIO,
ISR callback, or operational OSI table registration.

## Contract

The pinned ESP-IDF v6.1 S31 adapter
[header](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/include/private/esp_coexist_adapter.h)
declares counting semaphore create/delete, give/take (uint32 tick timeout),
and distinct ISR give/take callbacks. The
[S31 implementation](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/esp32s31/esp_coex_adapter.c)
uses FreeRTOS counting semaphores and 0xffffffff as the blocking sentinel.

Our independent POSIX primitive in `experiments/s31-native-radio/osi/`
implements only thread-context operations. It has an explicit tick rate
and typed results, not a claim of binary or scheduling equivalence with
FreeRTOS. The owner must map these results to the actual vendor contract
only after the remaining callback and lifetime requirements are established.

- Logical capacity and initial count use uint32; no count wraparound.
  One heap object per semaphore. The owner must bound the number of
  objects and the lifetime of exported handles.
- Mutex-protected count and closed flag, condition variable with
  `CLOCK_MONOTONIC`. Zero ticks is a nonblocking take; UINT32_MAX waits
  for give or close. Finite ticks use one absolute, rounded-up deadline,
  without extension on spurious wake. Overflow/clock failure is reported
  if waiting is needed; an already-available token or closed state wins
  first.
- `give` is nonblocking with respect to capacity (may take the mutex).
  Full is returned explicitly. Close is idempotent and wakes all waiters;
  no subsequent token consumption or give. At the deadline boundary a
  token visible under the mutex may win the give/timeout race.
- Deferred pthread cancellation during a condition wait reacquires the
  mutex and runs cleanup to decrement waiter count and unlock the mutex.
  Asynchronous cancellation and signal-handler context are unsupported.
- Lifetime is owner-managed. Stop publishing the pointer, close,
  drain/join every caller (including one holding a pointer but not yet
  entering), then destroy. No concurrent destruction, no implicit
  reference counting, no stale-handle protection. The busy check is not
  a replacement for this precondition. No ISR behavior is simulated.

This ordinary userspace heap allocation is not DMA-capable internal SRAM.
Do not wire this partial primitive into the coex table yet. IRQ semantics,
resource bounds/ownership, callback mapping, timer/task primitives, ROM
and broker dependencies remain separate gates.

## Build and verification

Use ESP GCC 14.1.1's RV32IMAC ILP32 multilib, not the earlier GCC 15.2.0-5
community sysroot. The latter linked actual Bitmanip instructions and
F/B ELF architecture attributes despite compiling the local C source
with rv32imac; that executable was **not** run on the board. The official
multilib build rejects an ELF interpreter and F/D/B/zba/zbb/zbc/zbs/zcf
attributes, and audits executable disassembly for FP opcodes (integer
fence instructions are permitted).

```sh
CC="$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-gcc"
experiments/s31-native-radio/osi/build-osi-sem.sh "$CC" /tmp/s31-sem-build
/tmp/s31-sem-build/test-host
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 /tmp/s31-sem-build/test-host-asan
```

Final static stripped RV32 ELF: 29,492 bytes, soft-float ABI,
`rv32i2p1_m2p0_a2p1_c2p0_zmmul1p0_zca1p0` architecture attributes.
No FP or vendor dependency. SHA-256:
`3b41d34a1cf6d25c77a2ca3767eabecbb590888ab347edf6b68ce7e762bde3c6`.

Independent Codex CLI source review (workspace-write sandbox, no edits or
hardware access) found the core consistent with the documented lifetime
and cancellation contract. It identified a weak wakeup test that could
pass after its full timeout, an inverted-shell-command interpreter check
that could falsely pass, and a precedence limitation when deadline
conversion fails. All three were corrected; the suite was extended with
active close/give/take and cancel/give races. A disposable host mutation
that suppresses pthread_cond_signal was positively rejected by the
new prompt-wakeup timing assertion. No mutation is in the committed source.

Final implementation:
- 32/32 complete host runs and 12/12 ASan+UBSan/leak-check runs.
- Each run checks invalid parameters, full/empty and UINT32_MAX boundaries,
  monotonic finite and fractional-rate timeout, prompt finite wakeup,
  prompt close of finite/indefinite waiters, deferred cancellation,
  2,048 MPMC token transfers with zero loss/extra tokens,
  16 active close races and 16 cancel/give races in both operation orders.
- Bounded process watchdog (30 s); explicit 64 KiB test-thread stacks
  on the target, larger stacks on the instrumented host.
- Concurrency exercised by contention and checked accounting, not
  guaranteed execution on specific harts, real-time latency qualification,
  exhaustive interleaving, or ThreadSanitizer coverage. No changes to
  host protections. No direct test of time32 overflow or injected
  spurious wakeups; the single absolute deadline and conversion checks
  are reviewed in source.

## Physical gate, S31 Lab v1

Before testing, the rollback image files and UART availability were
checked. Loaded only in a private tmpfs directory on the dual-core
RV32 Linux 6.18 CramFS read-only profile; target digest was verified.
No flash, USB media writes, radio commands, or external wiring changes.
One initial implementation ran successfully; the corrected final
implementation was then tested twice with the strengthened suite.

| Final test | Run 1 | Run 2 |
|---|---|---|
| Capacity / max / invalid arguments | PASS | PASS |
| Monotonic timeout and prompt wake | PASS | PASS |
| Close wakes all, deferred cancellation | PASS | PASS |
| 2,048 concurrent token transfers | PASS | PASS |
| 16 close races, 16 cancel/give races | PASS | PASS |
| Target SHA, exit 0, CPUs 0-1, eth0 present | PASS | PASS |
| Target temporary artifact removed | PASS | PASS |

Two final runs: 4,096 checked MPMC token transfers, 32 close races and
32 cancel/give races. MemAvailable before/after 10,288/9,828 and
10,356/9,844 KiB; not a leak measurement.
UART free at the end. Ethernet presence is not traffic regression.
Raw logs, source audit, private builds and hashes preserved under
`~/deps/s31-research/native-radio-osi-sem-20261127/` on Linvora-dev.

```sh
python3 experiments/s31-native-radio/run-uart.py --port /dev/ttyUSB0 \
  --artifact /tmp/s31-sem-build/test-rv32 \
  --sha256 3b41d34a1cf6d25c77a2ca3767eabecbb590888ab347edf6b68ce7e762bde3c6 \
  --probe osi-sem
```

## Independent kernel track

The already-published F-only + DMA candidate
`d3e404995222d8933317433c55bb355b126a2cb8` has a full XIP kernel/DTB
build started separately on Linvora-dev, reusing the prepared object
output tree and the exact F-only v2 config. The combined kernel remains
unqualified physically; no candidate kernel is flashed during semaphore
testing. Build status/log and, if successful, full result digests are
in `~/deps/s31-research/native-radio-next-20261127/f-only-dma-object/`
(`full-build.status`, `full-build.log`, `FULL-SHA256SUMS`).
Main kernel checkout is restored automatically when the build exits.

Next gates: thread-context bounded queues and timer lifecycle;
then the real callback adapter plus its ownership/IRQ broker boundary,
and complete kernel physical qualification independently. No operational
radio support is claimed.
