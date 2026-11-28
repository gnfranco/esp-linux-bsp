# Native Wi-Fi: ROM initialization evidence and mutex adapter

Continuation of NATIVE-WIFI-LOGGING-RUNTIME-20261127.md.
Exact input runtime closure SHA-256
1a3a30b511390c1618dbff01cb147ac8e9fc11185230665e7c49240836658fac;
148 undefined names, all pinned ROM map matches, 100 in reserved SRAM.
No vendor radio execution, RF, MMIO write, flash write or ROM function call.

## Read-only physical observation

The original Lab v1 remained installed. The previous combined bench
session confirmed exact rollback, all five flash slots, runtime, UART free.
Fresh preflight on Orion confirmed RV32 Linux6.18, both harts, read-only
CramFS root, root shell, eth0, BusyBox devmem and /dev/mem. Narrow
experimental read-only command: busybox devmem ADDRESS 32 (no value
operand), only five aligned words in one 4 KiB page of ROM-reserved
SRAM, outside DMA pool. No bulk memory access. No permission changes.

| Symbol | Address | Observed value |
|---|---|---|
| g_wifi_global_lock | 0x2f07ff20 | 0 |
| g_intr_lock_mux | 0x2f07ff24 | 0 |
| g_osi_funcs_p | 0x2f07ff44 | 0 |
| pp_wdev_funcs | 0x2f07ff48 | 0 |
| net80211_funcs | 0x2f07ff90 | 0 |

All five returned zero twice per boot, across two distinct boot IDs.
First collector completed reads and postflight but failed writing its
JSON due a host Python NameError; original log preserved, no PASS
marker claimed for that runner. Corrected collector reran on a
different boot and returned ROM_SNAPSHOT_READ_ONLY_PASS, with JSON.
Boot ID unchanged within each reading session. Both complete logs
screened: zero matched kernel/RCU/USB alerts. eth0 and both harts
present after reads, UART free. This demonstrates read access for
these words under this specific Lab v1 configuration only. No
claim for other ROM state, write access, executable mapping, ABI,
cache/ordering, PMP/privilege or resource ownership. No proof
that a future boot or a running radio has the same state.

Private evidence on Orion ~/source/build/s31-rom-state-read-20261127;
Linvora-dev ~/deps/s31-research/wifi-first-20261127/rom-init-gate-01/
rom-state-evidence.tar.gz, SHA-256
e8fb0225e42c163a0d1223a6fe2ca23b5ba30d96552af598e48da025861c0b9c,
verified after transfer. Corrected snapshot JSON SHA-256
2de760d115f4aaaa727df11e5a8184ae978488273327587771577544edbedd87.
Raw serial logs and boot identifiers retained privately only.
No new physical flash verification performed in this step because
the session is read-only and no flash write was requested.

## Exact initialization path

Offline disassembly/relocation evidence, not execution:
wifi_osi_funcs_register loads g_osi_funcs_p then branches to an
immediate success return if already nonzero (0x8 -> 0x68).
Otherwise it validates version 9 and magic 0xDEADBEAF at offset
508 and publishes the pointer at relocation offset 0x60.
Do not infer a successful registration proves a fresh table.
Never blanket-clear reserved ROM SRAM.

esp_wifi_init_internal calls registration, wifi_api_lock,
pm_funcs_init, wdev_funcs_init, net80211_funcs_init and
wifi_init_in_caller_task, with additional checks/failure paths.
The exact RV32 OSI v9 type is 512 bytes, compiled from verified
IDF header 0495a83c5c2a28516e4997a69b9f596de9a86a0c.
Initial PM/WDEV/Net80211 allocations use _calloc_internal
(offset 352) for 68, 1560 and 332 bytes, respectively; 1960 bytes
payload total, not a radio budget or DMA proof.
net80211_data_ptr_init has 12 direct store relocations, 11 to
remaining ROM-reserved SRAM symbols; wdev_data_init has 31,
all to remaining ROM SRAM. Union 42/100. These are write-site
observations, not a complete initialization-coverage proof.
The other 58 require analysis including indirect/ROM writers.
wifi_init_in_caller_task creates g_wifi_global_lock with
_recursive_mutex_create (76), g_intr_lock_mux with
_spin_lock_create (32), and calls _mutex_create (72).
wifi_api_lock uses _recursive_mutex_create and _mutex_lock (84).
wifi_rf_phy_enable: sleep-lock acquire (200), Wi-Fi clock enable
(248), PHY enable (212), then MAC init, conditional paths.
Disable: MAC deinit, PHY disable (208), clock disable (252),
sleep-lock release (204). Pinned public IDF adapter also calls
phy_wifi_enable_set and BB watchdog configuration, and the public
esp_wifi_init wrapper powers the radio domain before internal init.
Do not replace public init with an unchecked internal-init call.

OpenSBI source 0940d8a5834d02b93e2be0e60ef6505767862875:
APM setup grants broad bus access and later sets S-mode peripheral
privilege selection; this does not prove ROM execution or DMA
ownership under Linux MMU. Linux source 95d9d308df6e980d4b1a421c694be09b290c79a9:
DTS memory is PSRAM, DMA pool 0x2f030000..0x2f070000 no-map;
no Wi-Fi PHY/clock resource node. The ROM-reserved region is
outside the reported pool. No new fixed reservation inferred.

## Offline evidence gate

experiments/s31-native-radio/wifi-rom-init-gate.py verifies exact
closure and inventory hashes, map blobs, header hash, compile-time
RV32 offsets, instruction encodings, call and store relocations,
and the captured five-word snapshot schema. Six negative snapshot
tests reject nonzero state, missing word, missing sample, wrong
address, read/write claim and invalid boot identity. Gate is
offline; it always emits execution_allowed=false. It cannot
attest a current boot or validate provenance of arbitrary JSON.

```sh
d="$HOME/deps/s31-research/wifi-first-20261127"
experiments/s31-native-radio/wifi-rom-init-gate.py "$d/codex-runtime-next-01/repro-01/runtime.o" "$d/runtime-rom-inventory.json" "$d/rom-maps" "$HOME/deps/s31-research/radio-demand-20261127/wifi_os_adapter.h" "$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-" "$d/rom-init-gate-01/evidence/snapshot.json"
```
Marker S31_WIFI_ROM_INIT_EVIDENCE_PASS. Result SHA-256
c4fa72fc17adf27f47c7a3068b1c9a84a41e5820f7b3198bf6ceb6ebe4f26419.

## Codex mutex implementation

Narrow Sol task completed status 0, not timed out. Commit
9d661210929810f8738a0152767989fe26cfb4c2. Implements the five
typed OSI mutex callbacks in a 32-slot generation-tagged opaque
handle pool with pthread synchronization, recursive ownership,
non-owner failure, stale handle rejection, bounded allocation and
busy deletion fault latch. Wire only those five fields into an
unpublished v9 table. Thread context only; caller must disable
cancellation, not exit with locks held, and stop on latched fault.
No priority-inheritance or full FreeRTOS semantics claim, no
hardware spinlock, no ISR or signal context qualification.
Token values are opaque, not pointer dereferences. Need verify
vendor treats handles as opaque and uses compatible semantics.
Not yet linked into the full radio runtime or run on target.

Independent host and ASan/UBSan tests with leak checking passed,
including 20,000 protected increments, recursive lock,
non-owner unlock, stale token, exhaustion and busy delete.
RV32 ILP32F static test built twice byte-identical, SHA-256
0630a2bfa01317141d42188d133809f6b544ef85986edc300c62a176b528937f.
Repro script resources/build-radio-osi-mutex.sh. Private
codex-mutex-next-01/repro-01 and repro-02. No independent
second-agent code review; tests and manual inspection only.

## Next resource gate

Implement actual IRQ-safe spinlock semantics with the Linux broker,
task/queue/event services, and verify PHY power/clock ownership
using the pinned IDF sources. The read-only snapshot is a useful
precondition, not permission to publish callbacks or execute ROM.
Need exclusive radio ownership, safe address-space design,
ROM ABI/ECO and coherent pointer lifetime, known reset/quiescence
and DMA reachability. Existing broad /dev/mem observation is
lab-only; not a production resource interface. No upstream PR yet.
