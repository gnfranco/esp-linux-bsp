# S31 native radio: experimental thread-context timer service

Date: 2026-11-27. Follows the independent counting semaphore
[NATIVE-OSI-SEM-20261127.md](NATIVE-OSI-SEM-20261127.md) and the
[coexistence registration gate](NATIVE-COEX-REGISTRATION-20261127.md).
**Source-only runtime infrastructure**. No physical PHY, Wi-Fi, Bluetooth
or coexistence initialization. No ESP-IDF proprietary objects linked,
no MMIO, no IRQ, no ROM hooks, and no OSI adapter table installed.

## Design and lifecycle

Sources in `experiments/s31-native-radio/osi/`:
`osi-timer.h`, `osi-timer.c`, `test-osi-timer.c` and
`build-osi-timer.sh`. The independent `osi-sem.c` is reused by the
test harness for synchronization. The UART runner
`experiments/s31-native-radio/run-uart.py` now accepts
`--probe osi-timer` and checks the individual markers and the target
SHA-256, not merely the process exit status.

- A single POSIX dispatcher thread, a `CLOCK_MONOTONIC` condition
  variable and at most 16 statically bounded (per-service) slots.
  The heap object and dispatcher stack are allocated once at create.
- Each handle is a slot/generation pair. Reuse changes generation;
  exhausted generations retire their slots. Handles are local to
  their issuing service lifetime, not globally unique or persistent.
- Positive-delay one-shot or periodic timer in microseconds.
  `arm` replaces pending scheduling. Periodic behavior is
  **fixed-delay after callback completion**, not fixed-rate and not a
  latency/real-time guarantee. Callbacks are serialized and run without
  the service mutex held.
- An armed callback becomes in-flight before the mutex is released.
  `disarm` can cancel pending work but cannot revoke a callback
  already selected; it reports `IN_FLIGHT` in that case.
  `delete` refuses an in-flight slot with `BUSY`.
  Explicit rearm during a callback supersedes automatic recurrence.
- Owner stops exposing the service pointer, closes and drains all
  external borrowers, then calls `destroy(&owner)` to join the
  worker and consume the handle. A worker failure is distinguished
  by `WORKER_FAILED`; post-join teardown errors are terminal and
  set owner NULL. A failed join leaves ownership with the caller.
  No concurrent `destroy`, use-after-close, signal/ISR calls or
  forced thread cancellation are supported. Callback arguments must
  remain valid until successful delete or joined dispatcher.
- Deadline math checks signed 32/64-bit Linux `time_t` bounds
  before conversion and checks monotonic microsecond overflow.
  Public `arm` deadline errors do not mutate the existing schedule;
  internal periodic-rescheduling errors close the service.

These are explicitly Linux thread-context semantics, **not** an
assertion of compatibility with the ESP-IDF FreeRTOS/ROM timer ABI.
A slow callback delays every other timer. Mapping vendor callback
types, context rules, cancellation, and ownership remains separate
integration work; do not register this implementation as a vendor OSI
table without that review.

## Independent Codex CLI review and corrections

A bounded Codex CLI review on Linvora-dev, using the permitted
workspace-write sandbox, identified:

1. Ambiguous destroy return/ownership on worker errors. Fixed by
   pointer-to-pointer destroy, consumption after a successful join,
   separate `WORKER_FAILED` and explicit terminal teardown semantics.
2. A rearm deadline test that could pass at the old deadline.
   Fixed by anchoring the old deadline, verifying the dispatcher is
   waiting, and requiring execution strictly earlier.
3. Weak periodic, rearm and close tests. Extended with a blocked
   callback spanning multiple periods, time since callback exit,
   distinct rearm-vs-disarm during in-flight callbacks, a second
   pending timer, a join-entry handshake and close/drain assertion.
4. Signed `time_t` narrowing and an unchecked compiler version.
   Fixed pre-cast range check and GCC 14.1.1 pin verification.

The review is an independent design/source critique, not a formal
proof of scheduler fairness, bounded callback execution or full
real-time behavior. Synthetic time32 boundary/fault injection and
true concurrent model checking are still pending.

## Host build and test

Use the already installed ESP toolchain, without recreating kernels:

```sh
CC="$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-gcc"
experiments/s31-native-radio/osi/build-osi-timer.sh "$CC" /tmp/s31-timer-build
/tmp/s31-timer-build/test-host
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 /tmp/s31-timer-build/test-host-asan
```

Build checks `-Wall -Wextra -Werror`, static stripped ELF32
RV32IMAC ILP32 soft-float, no interpreter or unwanted F/D/B/Zcf
attributes, and disassembly for FP opcodes.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `test-rv32` | 33,588 | `98d6d1da435fc60730fcc0e3ebb4ae9e97021e7c152c3f11bcb58f207e95e2dd` |
| `test-host` | 31,488 | `614ebffd81b4ce45d3da2f631e3c4c6be5c6e318965dd97820e132e917379344` |
| `test-host-asan` | 200,008 | `9b682482b1aeab887081726d7b064c296c0c420c68e3f38a7f3aebbaba3926d4` |

Repeated host execution: **16/16** regular; **8/8**
ASan+UBSan with `detect_leaks=1` and `halt_on_error=1`.
Every run emitted:

- `TIMER_ORDER_REARM_STALE_PASS`
- `TIMER_PERIODIC_FIXED_DELAY_PASS`
- `TIMER_IN_FLIGHT_REARM_PASS`
- `TIMER_CLOSE_DRAIN_PASS`
- `S31_OSI_TIMER_PASS`

These exercise deadline order, stale IDs, full-capacity behavior,
early rearm, auto-periodicity, callback in-flight handling,
serialization, cancellation, close and dispatcher join.
Timing assertions are behavioral checks on the tested scheduler,
not formal worst-case time guarantees.

## Physical qualification, Orion

Function-CoreBoard-1 with original **S31 Lab v1**. Before testing,
the original release artifact `SHA256SUMS` checked successfully,
and `/dev/ttyUSB0` was free. The 20,300-byte gzipped RV32 target
was transferred in two SHA-256-verified chunks; target checked
`98d6d1da435fc60730fcc0e3ebb4ae9e97021e7c152c3f11bcb58f207e95e2dd`.

Two independent RAM-only UART sessions both reported:

- `riscv32`, Linux `6.18.0+`, online CPUs `0-1`;
- read-only CramFS root, Ethernet interface still present;
- exact target digest check and every five test markers above;
- `S31_OSI_TIMER_HARDWARE_PASS` and
  `TARGET_ARTIFACT_REMOVED` following the run.

**Physical result: 2/2 PASS**, no flash writes. The final check
verified original Lab v1 artifacts and the UART was free. Retained
private sanitized-transfer logs on Linvora-dev have SHA-256
`cc254b39cdb3c3fa98414c5e1d751712dacf81f25d89722a4ee479cd17219175`
(first) and
`d97aca0eb7188cecf30f30515d5c60ccc34a84a727d5104c978417c767083c5b`
(second).

Private Linvora-dev evidence root:
`~/deps/s31-research/native-radio-osi-timer-20261127/`
(`build-03/`, `final-host-evidence/`, `physical-evidence/`,
`codex-review.log`). Do not publish raw UART logs or other
bench identifiers.

## Integration decision and remaining gates

Keep the timer separate from semaphores, DMA and F-only kernel
branches. The native runtime has validated metadata, semaphore and
timer primitives, **not** vendor runtime registration or integrated
radio functionality. Next inspect actual ESP-IDF v6.1 adapter/coex
timer callback expectations, including ISR/task context and
resource lifetime, before implementing a narrowly typed adapter.
Then qualify those callback contracts with host/RV32 probes and
only later consider safe radio initiation after F-only and DMA/cache
constraints have been reviewed. Do not treat this as Lab v2 release
qualification.
