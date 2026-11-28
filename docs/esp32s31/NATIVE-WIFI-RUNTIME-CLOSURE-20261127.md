# Native Wi-Fi: runtime closure and scoped OSI allocator linkage

2026-11-27, continuing after the physically tested F-only/DMA/SRAM
combined kernel. All new radio work in this checkpoint is host-side
compile/link/source/relocation testing. No proprietary radio code
executed; no scan, association, RF, MMIO or ROM mapping attempted.

## Combined kernel integration gate

Full build status 0, all five artifacts SHA-256 verified:
source commit 95d9d308df6e980d4b1a421c694be09b290c79a9,
XIP 4302604 bytes, SHA-256
f629735cb2b1e56a3909515f22692dd6782a4b5bd975be71cdc93675d6fbc519.
Configuration differs from aligned SRAM base only by experimental
F-only. F-enabled DTB separately used, SHA-256
782fb1aa5af5af721aeae84d84e83e75331d05eea0ef5862d8cfcaa23a85529d.
Physical F-only suite ILP32F, 8192/8192 context checks and all
required markers; CPU-only SRAM allocator groups and reboot/fresh
lease checks PASS. Pool 3->7 used pages, 3 after fresh boot,
allocation failures zero. Strict screened logs no matched alerts.
Original DTB and XIP erase spans and all five original slots
restored/verified; original runtime PASS and UART free.
These are bounded functional tests, not full peripheral qualification,
not proof of RCU root cause or hardware DMA ownership. Existing
invalid-sigreturn partial-copy negative path remains inconclusive.
ROM-assisted resets, not measured physical power cycles.
Private combined-evidence.tar.gz SHA-256
89e3d09f0acd4df0304215fbd60961a14a91ff4f19e5bf5c1c14d89ef432359e
verified after transfer to Linvora-dev.
Original physical backup sizes 12288 and 4304896 bytes; hashes
a7a5b1c2cda25214058293f79c3b3e0f9d201c1fe65e6fb6a7f7b958cf232e97
and f89d8653eb98cf52379bd932ad061b9f383d08094a714853b3fc8903a8764be3.
Kernel integration/s31-native-wifi fast-forwarded to the exact
combined tested source commit and pushed to personal fork only.

## Matched link proof

All source bytes retrieved from pinned IDF v6.1 Git tree;
hashes checked before compilation. New script
experiments/s31-native-radio/wifi-runtime-link-proof.py generates
private outputs from exact previous FTM closure. Stages compared
against equivalent ld -r controls, exact removed sets, no newly
undefined names in these three stages:

| Stage | Before | After | Actual names removed |
|---|---:|---:|---:|
| 2.4 GHz regulatory data | 193 | 191 | 2 |
| ILP32F floor and libgcc | 191 | 166 | 25 |
| Event base and hex helper | 166 | 164 | 2 |

Regulatory table source d6028adb14fa43c90724c726d34db7fbde2fe4ea,
generic type header 6be8440d2311835341ebf7e1244fcee9a249a362,
S31 soc_caps.h 53e2aec61a3cc50356e52696e5dca0dc40c00f71.
Original table body retained; generated data-only translation unit
omits exactly the unused esp_wifi.h public API include, retaining
official generic header and 2.4 GHz selection (5G macro absent).
No changes to regulatory channel/power values. Objects are
regdomain_table 552 bytes and regulatory_data 200 bytes.
Not proof of actual radio regulatory enforcement.

Pinned GCC14.1.1 lib32/ilp32f libgcc.a SHA-256
3f02b11387714d45babfce91c4f77d16ff0f6fbb082b9b1cd45f9d398c6c6835,
libc.a SHA-256
3dda872fd193788b21329bee0aeb7a34af52e0e1f6dc5218497cf61313f20f35.
Extract only floor.o from musl archive and link libgcc; actual
selected providers verified against link map and symbol inventory.
25 math/compiler names resolved (including the seven former ROM
alternative-address names). All resulting ELF attributes checked
against the exact RV32IMAF+C/Z extensions string without D.
Do not infer runtime behavior from ELF attributes alone.
No whole-libc link yet.

Pure helpers: original WPA common.c
ef36edc7dca3815a2b0b89a04c74493f22a9739c,
common.h 4fa4e11798a165ec6b79c200f00d58b2213b20b6.
Extract exact hex2num, hex2byte and hexstr2bin bodies with original
license notice, official uint8_t u8 typedef. WIFI_EVENT macro
definition from wifi_init.c e50a7f4a19c42fdb404902fa2f7cedf39c34e043
and event header 2dbdd71df89ab9b8422795d8bc680f3eec44ba0f.
No event loop implied. Host normal and ASan/UBSan tests PASS
(all 256 values in upper/lowercase, invalid/truncated strings,
zero length, multi-byte, guard bytes, event identity).
RV32 standalone test compiled, not run; SHA-256
4cbaae873b7f31fd890c0b01b271761d9bc8f17d5041e2d0f0d6fe69c757c4b2.

Sol read-only review completed (status 0) and flagged incomplete
D detection, relative tool resolution and attribution/provider
checks. Corrected: exact architecture string, resolve tool paths
before chdir, verify providers, record tools/startup object hashes,
clarify host executes pure open-source helpers. No fully hermetic
toolchain pin or second independent review. All final proofs rerun;
final closure byte-identical across reproductions, SHA-256
3b9aadcc32048d05525d5366558bef68ee0408f92dcd673db4b61ee30257683a.
Regulatory and math closure SHA-256:
6ee02cd6741afadc39fd0613ca6c892f9362e87d76d07838efd5f25a2464ff4b
cf27b7939a99f55c19eead09f613e7873e98fd5058140165d8fd6466f48523a1.
All 33 final output hashes verified; manifest SHA-256
15222c9c17affda14a286095ae00cb63f547bc2c767dba594dbe6bf0a2cba3cf.
S31_WIFI_RUNTIME_LINK_PROOF_PASS.

## Scoped free bridge

Exactly one surviving direct free relocation, offset 0x17a in
hal_crypto_set_key_entry. Disassembly shows the temporary buffer
allocated via g_osi_funcs_p table offset 360 when input is unaligned,
and freed only when pointer differs from the original input.
RV32 compile-time offsetof gate on pinned OSI v9 header verifies
size 512, _wifi_malloc offset 360, _free offset 176.
Existing radio_osi_wire_alloc binds _wifi_malloc to general_malloc.
Plain libc free would bypass the allocator ledger. Do not globally
interpose free; do not make a blanket claim about unexamined ROM
calls or future link roots.

Private objcopy --redefine-sym free=radio_osi_free ONLY on the exact
audited 164-name radio closure, before linking the actual radio-ledger,
radio-alloc, radio-osi-alloc and radio-sram-backend code as ILP32F.
No proprietary archive distribution. Verified original single
relocation, rewritten relocation, linked real radio_osi_free,
and ordinary free remaining only in backend release at 0x28.
Two link proofs reproduced identical scoped-osi-closure.o SHA-256
4fd15d2d3e0296886c164c613d0d429498927e9fd0c19d7a34881024e2856e09.
S31_WIFI_SCOPED_FREE_LINK_PASS. Manifest SHA-256
7c8ba66671efbdb7d05d5ee030ac95b334b10ff013c6140ad5ecba057f766e29.
New runtime dependencies abort, aligned_alloc, free, pthread_mutex_lock,
pthread_mutex_unlock; 164 -> 168 undefined in scoped combined closure
(one old name replaced then defined; five ordinary runtime names).
This is linkage proof only. No OSI table published to vendor code,
no runtime allocator/free test on that vendor call. The same function
contains hard-coded peripheral register accesses; not safe to run
without kernel mediation. Preserve failure paths and verify ownership.

## Remaining ROM inventory and next gate

All 164 pre-bridge undefined symbols have actual relocations.
157 correspond to unique addresses in pinned ROM maps:
100 IDF ROM-reserved SRAM (noncall-only),
57 ROM aperture (41 call-only, 3 mixed, 13 noncall-only).
Seven non-ROM names in pre-bridge closure:
free, net80211_printf, phy_printf, pp_printf, putchar, puts, sprintf.
The scoped bridge fixes only the examined vendor free reference,
adding legitimate runtime dependencies as noted above.
No remaining ambiguous ROM addresses, but none thereby qualified as
callable/initialized. Relocation class not proof of function vs data.
Inventory SHA-256
08e066570078cf3ea41b542fc833f8499059a9ca65926e606d747de7a57fcaac.
Next resolve standard runtime functions with consistent ownership;
audit logging/ISR context, identify ROM globals and initialization,
safe CPU execution domain and direct MMIO/interrupt/clock/PHY
ownership. Do not map all ROM or radio registers indiscriminately.
No scan until validated resource path. The earlier CPU-only lease
cannot establish DMA reachability or production radio capacity.

## Reproduction on Linvora-dev

Private directory d=$HOME/deps/s31-research/wifi-first-20261127.
From BSP worktree, use new unique output directories:
```sh
p="$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-"
d="$HOME/deps/s31-research/wifi-first-20261127"
experiments/s31-native-radio/wifi-runtime-link-proof.py "$d/nonrom-sources" "$d/wifi_init.c" "$d/ftm-link-proof-03/wifi-ftm-closure.o" "$p" "$d/runtime-link-repro-new"
experiments/s31-native-radio/wifi-runtime-rom-inventory.py "$d/rom-maps" "$d/runtime-link-repro-new/remaining-relocation-sites.json" > "$d/runtime-rom-inventory-new.json"
experiments/s31-native-radio/wifi-scoped-free-proof.sh "$HOME/deps/s31-research/radio-demand-20261127" "$d/runtime-link-repro-new/wifi-helpers-closure.o" "$p" "$d/free-osi-link-proof-new"
```
Final outputs runtime-link-repro-03, runtime-rom-inventory-03.json,
free-osi-link-proof-02 and combined-evidence.tar.gz. Keep private.
