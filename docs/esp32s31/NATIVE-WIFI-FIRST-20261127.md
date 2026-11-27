# Native Wi-Fi first: selected link closure and persistent SRAM lease

2026-11-27. Wi-Fi scan first; do not bring up Bluetooth controller,
Classic/BLE or RF coexistence with Bluetooth yet. The vendor Wi-Fi
archives may still require coexistence functions even in this profile.

## Selected link closure

Source-only audit, never execute the relocatable object. ESP-IDF v6.1
fff9895c82d744c7237be8847347bdd1b07c6643. The new
dependencies/s31-native-wifi-first.lock.json pins all nine archives,
three source repository commits, 13 ROM linker-script Git blobs,
and eight root symbols. Official wifi_init.c blob
e50a7f4a19c42fdb404902fa2f7cedf39c34e043 and CMakeLists.txt blob
a0c6f614167afc5515f23bab83238242ef434b54 inspected. The IDF public
esp_wifi_init has pre/post initialization and power/PHY/coex work;
esp_wifi_init_internal alone is NOT equivalent to public init.

GCC14.1.1 toolchain ld -r --gc-sections, eight roots covering internal
init, mode, start, scan, scan record/count retrieval, stop and internal
deinit. Initial five-archive trial: 355 undefined names. Final seven
official Wi-Fi blobs plus PHY and coex: 345 undefined names, 297 match
symbol assignments in official ROM maps, 48 no match. 24 of those
48 are PHY FTM compensation data names. Remaining names include libc/
math/compiler helpers, event API, connect/disconnect wrapper functions,
regulatory data, logging, xtal clock query and other support. No claim
that matching ROM names are callable on Linux, interchangeable with
musl, actually linked, or all applicable to the selected ECO. Some
are data, not functions; some are ROM globals or need initialization.
No raw ROM mapping or unresolved-symbol stubs. The relocatable closure
is ELF32 RISC-V single-float ABI, text 456749, data 5667, BSS 18686
bytes (not a runtime memory budget). The previously qualified F-only
kernel remains a separate experiment, not yet integrated here.
Open-source IDF component code and further dependencies remain to be
accounted for. No final executable, scan or RF startup.

Reproducible audit (private cache; no vendor binary or ROM linker
scripts added to the repository):
```sh
d="$HOME/deps/s31-research/wifi-first-20261127"
python3 experiments/s31-native-radio/wifi-closure.py \
  "$HOME/deps/s31-radio" "$d/rom-maps" \
  "$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-" \
  "$d/repro-new"
```
Reproduction confirmed byte-identical undefined and classification
files and same relocatable object SHA-256
adec29146d41273e904a9f5b420baed911b2243b8d5b3331ce177720a52e5b22.
Evidence in ~/deps/s31-research/wifi-first-20261127.
Keep no vendor binary redistribution.

## Persistent CPU-only SRAM session

Kernel commit e328a231fcc71494777b014d77302b230e3c21ca,
branch experiment/s31-wifi-sram-session, parent 39066a9cadae.
S31_WIFI_SRAM_SESSION is default n. Exact S31 machine, pool geometry,
4 KiB pages. /dev/s31-wifi-sram-lab mode 0600; exclusive open,
CAP_SYS_RAWIO checked for open/read/write/mmap. One 16 KiB lease,
atomic floor of 192 KiB free at acquisition, no device DMA address.
Bounded file operations use normal temporary kernel allocations for
usercopy. Mapping accepts exactly four pages, offset zero, shared,
non-executable, clears MAYEXEC, sets IO/DONTCOPY/DONTDUMP/DONTEXPAND,
uses existing coherent pool mapping. File/VMA references retain
ownership; trusted lab process must synchronize access, must not
delegate descriptors or mappings to untrusted processes. These
are CPU accesses only; no radio MMIO, bus-master, DMA handle or
general raw memory interface.

Astra reviewed the first draft: extent/lifetime checks sound under
stated contract, flagged memory attributes on RV32 and per-operation
privilege. Sv32 _PAGE_NOCACHE and _PAGE_MTMASK are zero; writecombine
does not establish uncached mapping. Pinned official soc.h
e32604a5f4c931b0701fd10e402693386f88c3d4, cache_ll.h
ea4100ea1c03c4750018e425fb2245c15cdeb9ae, mmu_ll.h
40a95f6ec6c6e5568f55bced75533b3c74e49772 and ext_mem_defs.h
ce40d9ce3f4b103830e0a8837bb55bd0b9efa274 show the external-cache
windows exclude internal SRAM. Pinned OpenSBI cache_mmu.c at
0940d8a5834d02b93e2be0e60ef6505767862875 similarly describes
external cache only, rejects ranges outside flash/PSRAM. These
support a bounded lab test, not a measured alias/coherence guarantee.
Do not invoke external-cache ROM maintenance on SRAM. Corrected
privilege checks, bounce usercopy and added conservative quarantine:
copy-only lease may scrub and release; any lease that reaches
mapping helper remains reserved until reboot, even on map failure.
No cross-client reuse of possibly unqualified mapped pages.
A successful final unmap does not currently return those pages to
the pool. No second independent review of final changes.

Final coherent.o RV32 compile PASS, SHA-256
d7490ec072953da9010505eaa8f4fd5b277699ff2a9db4574afbab2dd93a4da9.
checkpatch 0 errors/0 warnings; only S31_WIFI_SRAM_SESSION=y added
relative to qualified arena-proof config. Test-sram-session.c
host and RV32 compilation PASS, not executed (device absent on
host and no new kernel on board). RV32 ELF SHA-256
4cfc2548704630234138d17a5d39fbcbf050a1938a6b907f59073c38db5433fd.
Tests require copy-only reuse, exact mmap restrictions,
bidirectional CPU cross-view, mapping retention after descriptor
close, and mapped-lease quarantine after final unmap. Need
cross-hart visibility, post-release pool accounting, reboot,
candidate/rollback integrity at the bench before connecting the
OSI backend. No DMA reachability or physical test in this checkpoint.

Full session kernel build actually launched, PID 104056, output
~/deps/s31-research/wifi-first-20261127/session-build.
Check session-full-build.status, session-full-build.log and
SESSION-FULL-SHA256SUMS before using the image. Keep previously
qualified images and rollback intact. No new flash write this turn.

Next work: classify ROM symbols (code/data, ABI, ECO, execution
domain), resolve the 48 non-ROM names via actual source/runtime,
finish real OSI services and PHY/clock/IRQ integration, and wire
the persistent SRAM backend only after the mapping gate. The first
scan must be real, not a synthetic success or stubs. Full peripheral
regression remains deferred to integration.
