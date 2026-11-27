# S31 native coexistence: typed timer object binding

Date: 2026-11-27. Experimental Linux thread-context layer on top of
[NATIVE-OSI-TIMER-20261127.md](NATIVE-OSI-TIMER-20261127.md).
No vendor binary linked, no RF initialization, no ROM access and no
operational coexistence adapter table registered.

## Official reference and limits

ESP-IDF v6.1 commit fff9895c82d744c7237be8847347bdd1b07c6643:
- [S31 coexistence adapter](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_coex/esp32s31/esp_coex_adapter.c),
  blob ec8eddb75cdb0ef645fbb456e1ad8dec1a7b1555, delegates timer functions
  to ets_timer_setfn/arm_us/disarm/done.
- [S31 ETSTimer declaration](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/esp_rom/esp32s31/include/esp32s31/rom/ets_sys.h),
  blob 7d13e9b95744126ac1fea8d088511ddb3c56d607, defines caller-provided
  timer storage and void legacy functions.
- The vendor internal allocator requests MALLOC_CAP_8BIT, MALLOC_CAP_DMA
  and MALLOC_CAP_INTERNAL. POSIX heap is not a substitute.

The Linux binding is intentionally typed and returns explicit statuses.
It is not a drop-in replacement for the vendor void callbacks. It does
not access ETSTimer fields or assume its callback execution context,
timing guarantees, supported delay range or periodic semantics match
the Linux dispatcher. These remain integration gates.

## Implementation and ownership

Sources under experiments/s31-native-radio/osi:
osi-timer-binding.c/.h, test-osi-timer-binding.c and
build-osi-timer-binding.sh. The binding associates a caller-owned opaque
address with a generation-tagged dispatcher slot. It never dereferences
the key. At most 16 slots per service; duplicate bind returns BUSY.
The callback argument and key remain valid until done succeeds or the
worker is joined. Old callers must drain before the same address is
reused. The binding does not provide external generation-safe keys.

All binding operations serialize under its mutex, with lock order
binding then timer service. The worker releases the timer lock before
invoking a callback; destruction releases the binding lock before
joining. Done disarms the slot and prevents new arms while retirement
is pending. BUSY means a selected callback may still execute or finish;
retry done only after completion. Callback arguments must remain live.
Close cancels pending work, but does not wait for a selected callback.
Stop publishing the service, close, drain external borrowers, then
destroy. No ISR/async-signal calls or concurrent destruction.

The signed int64 monotonic-time helper checks representability. Error
and ownership behavior is documented in the header. No hard real-time
or race-freedom claim is made.

## Build and tests

Use ESP Linux GCC 14.1.1 RV32IMAC ILP32, existing OSI sem/timer source.
Run from the BSP checkout:

```sh
CC="$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-gcc"
experiments/s31-native-radio/osi/build-osi-timer-binding.sh "$CC" /tmp/s31-binding
/tmp/s31-binding/test-host
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/s31-binding/test-host-asan
```

Compiler warnings are errors. Static stripped RV32 ELF: 58,548 bytes,
soft-float, no interpreter, audited ISA attributes and FP opcodes.
Final test ELF digest:
ba4fb23df73ab8ea57edf4c7c5f7478600623a20786abe3c6040fa120bc03c96.

Initial host/sanitizer runs passed. Build-03 added a retiring state
to prevent a callback or another caller rearming a slot after done
has begun. It passed 4/4 normal and 4/4 sanitizer repeats.
Independent Codex CLI read-only review found no lock inversion under
the documented ownership protocol, but identified a scheduler-sensitive
disarm test and incomplete callback argument lifetime documentation.
Build-04 clarifies the contract and tests disarm while the dispatcher
is occupied by another callback, then verifies progress via a subsequent
callback. The implementation C source is unchanged from build-03.

## Physical evidence

The first build-02 attempt was inconclusive: target console unavailable
before the artifact was executed. A second attempt passed all markers
and cleanup. Build-03 also passed all physical markers, including
retirement protection, and released UART. No flash writes in any attempt.
The original Lab v1 rollback checksums were verified. Logs and binaries
remain private under Linvora-dev
~/deps/s31-research/native-radio-timer-binding-20261127/
and the Orion bench directory
~/source/build/s31-native-timer-binding-20261127/.
Build-04 evidence is recorded separately below after execution.

## Next gate

Review actual vendor timer implementation and context requirements
before any void adapter wrapper. Determine semantics for error reporting,
periodic scheduling, ownership, IRQ dispatch, and internal DMA-capable
allocation. Keep the hardware broker independent of this ordinary POSIX
service. No upstream submission or new Lab release is implied.

## Build-04 final verification

Host repetitions: 4/4 normal and 4/4 ASan/UBSan, in addition to
the initial successful executions. SHA-256 verified for all logs and
executables. On the original S31 Lab v1, the RAM-only physical probe
returned all of the following markers:

- BINDING_LIFETIME_PASS
- BINDING_CLOSE_PASS
- BINDING_DISARM_PASS
- BINDING_REENTRY_PASS
- S31_OSI_TIMER_BINDING_PASS
- S31_OSI_TIMER_BINDING_HARDWARE_PASS
- TARGET_ARTIFACT_REMOVED

Exact target SHA-256 matched build-04. CPUs 0-1 remained online,
read-only CramFS baseline confirmed and eth0 still present (not an
Ethernet traffic test). UART released, no flash writes.
MemAvailable before/after: 10,340/9,872 KiB; not a leak measurement.

Private evidence (physical-evidence/physical-v4-01.log) SHA-256:
fe7b2a24d1b6c0b00278ebc97f359b11ac41967dac4ae89d76d41502128ba396.
Copied to Linvora-dev and verified. Across builds 02, 03 and 04:
three successful physical executions and one console-unavailable
attempt before test execution. Only one execution used build-04.
