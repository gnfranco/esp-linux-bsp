# ESP32-S31 native Wi-Fi: continuation checkpoint, 2026-11-27

## Decision and scope

Continue direct native Linux Wi-Fi on S31, first real RF scan, then
association and IP, then Bluetooth and extensive regressions. Do not
substitute a radio firmware running ESP-IDF on another hart. Preserve
both Linux harts. An isolated trusted userspace proof with kernel
resource mediation is the intended experimental architecture; no
vendor binaries in the public source tree. No silent stub success.
The user accepts the initial 3/3 boot gate plus one UART workload
run with the alignment fix, and defers the 20-cycle stability
campaign until Wi-Fi is operational. This is not a production gate.

## Exact branch state and evidence

- Kernel personal fork: gnfranco/linux.
  integration/s31-native-wifi =
  68bee8b8f52f05ecc472effa3bb90201d056c4ef (aligned SRAM candidate);
  experiment/s31-native-wifi-f-only =
  95d9d308df6e980d4b1a421c694be09b290c79a9 (adds F-only patches,
  not yet qualified in this combination).
- BSP personal fork: gnfranco/esp-linux-bsp,
  experiment/s31-native-wifi-rcu-investigation, this checkpoint.
- Lab v1 rollback: gnfranco/esp-linux-lab private release s31-lab-v1.
  Kernel 9b41c9c5df88ed883b05662187e431bb3452e491; exact original
  kernel XIP SHA-256 5fe191d8e189a229f68848791bdb22b851b1b1922ca3f4b0a150030ba0f406ed.
- Physical state at checkpoint: original Lab v1 restored, exact full
  kernel erase span and all five slots verified, restored runtime
  both harts/eth0, UART free. USB Kingston 0951:1666 and Ethernet
  remain connected. Never write USB media.
- Full aligned candidate XIP SHA-256
  e45f05329785b053503be996f9e08c37cbc59c5fab78e6d19fa93b7bffb6ba78,
  config/DTB identical to prior session, single 13-line alignment
  change. One hardened UART/tmpfs observation + 3/3 independent
  100-second boot-only observations, zero screened alerts. No proof
  of RCU root cause. Prior original Lab v1 Oops anon_vma_clone
  repeated with exact historical signature. Full report:
  NATIVE-WIFI-RCU-INVESTIGATION-20261127.md.
- Private evidence on Linvora-dev:
  ~/deps/s31-research/wifi-first-20261127/rcu-evidence/
  SHA256SUMS covers eight archives, including repeat-03. Do not
  publish raw serial logs or private identifiers.

## What is actually proven and what is not

1. Original Lab v1 Linux6.18 RV32 XIP, two harts, Ethernet, USB host,
   GPIO/RMT, CAN internal loopback, reboot. No native radio.
2. Experimental F-only branch previously qualified ILP32F, 8192
   SMP context tests, no D instructions accepted; invalid sigreturn
   partial-copy negative still inconclusive. The vendor archives
   need RV32 single-float ABI. F-only + SRAM lease + aligned DMA
   combination is NEW and needs physical qualification.
3. Stats/arena and SRAM session: existing 256 KiB global pool
   0x2f030000..0x2f070000; temporary 16 KiB CPU-only lease
   /dev/s31-wifi-sram-lab, 192 KiB free floor on acquisition.
   Physical cross-hart/cross-view, lifetime and quarantine pass
   on older session kernel. Mapping never proves DMA reachability,
   bus translation or cache coherence. Quarantine until reboot.
   Do not allocate a new fixed physical carveout without measurement.
4. OSI memory adapter, ledger, semaphore and timer prototypes,
   coex registration metadata and CPU-only SRAM backend implemented
   and tested. Backend physical callback tests passed on older
   kernel, but overall session incomplete (RCU stall before workload,
   final unmount timeout). Repeat on aligned/F-only base with strict
   log screening. Vendor-required size not measured; 16 KiB is
   not a sufficient asserted radio budget.
5. Official IDF v6.1 archives and map hashes pinned in
   dependencies/s31-native-wifi-first.lock.json.
   Wi-Fi link closure 345 undefined, 297 ROM name matches,
   48 non-ROM names (24 FTM compensation data). ROM matches:
   194 addresses in ROM aperture, 103 reserved ROM SRAM, seven
   alternative libgcc/rvfp addresses. No callable ABI/privilege
   or initialization proven. No vendor executable or RF scan yet.
   Public esp_wifi_init wrapper includes PHY/power/coex work;
   internal init is not equivalent. Timer prototype fixed-delay
   semantics differ from IDF periodic deadlines; correct before
   wiring vendor callbacks. Bluetooth deferred.

## Immediate next work in order

A. Finish combined F-only kernel build currently launched on
   Linvora-dev, PID129098. Actual status file, not assumed success:
   ~/deps/s31-research/wifi-first-20261127/native-wifi-f-only-full.status
   log native-wifi-f-only-full.log, output native-wifi-f-only-build,
   NATIVE-WIFI-F-ONLY-SHA256SUMS on success. Config must differ only
   by +RISCV_EXPERIMENTAL_F_ONLY y. Validate all hashes and exact
   source commit before bench. Preserve aligned base build and
   original image. The F-enabled DTB from prior qualification must
   be verified (SHA-256
   782fb1aa5af5af721aeae84d84e83e75331d05eea0ef5862d8cfcaa23a85529d);
   standard DTB does not advertise F. Reuse the previously qualified
   F-only userspace ELF and test procedure after checking digests.
   A candidate requiring DTB replacement must protect both affected
   erase spans, not reuse a kernel-only rollback unmodified.
   Test F-only/context and SRAM session/OSI backend narrowly, strict
   RCU/Oops/USB screen, restore Lab v1. No need for the full
   peripheral regression matrix before Wi-Fi.

B. Resolve link/runtime closure without hardware writes. Classify
   297 ROM entries by symbol type/ABI/ECO/access domain/init needs;
   48 non-ROM names by actual source/runtime provenance. Retrieve
   pinned open IDF source via Git blob hash and verify bytes.
   Avoid raw ROM mapping or guessed signatures. Check Newlib vs
   musl incompatibilities and hardfloat ABI. Reproduce closure:
   d="$HOME/deps/s31-research/wifi-first-20261127"
   python3 experiments/s31-native-radio/wifi-closure.py "$HOME/deps/s31-radio" "$d/rom-maps" "$HOME/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-" "$d/repro-new"
   Review NATIVE-WIFI-FIRST-20261127.md and
   NATIVE-WIFI-SRAM-BACKEND-20261127.md. Build an honest unresolved
   dependency inventory; do not make a stub-linked executable
   pass by ignoring undefined behavior.

C. Finish OSI/PHY services and kernel resource mediation: queues,
   task lifecycle, locks/interrupt context, timers, event delivery,
   clocks/resets, power, IRQ and real DMA internal memory ownership,
   bus addressing, device quiescence. Determine actual simultaneous
   radio memory demand; no arbitrary MMIO or addresses. Audit ROM
   privilege and firmware ownership. FPU support is necessary
   but not sufficient for the vendor radio ABI.

D. First actual scan only after init/start/shutdown resource path
   is valid. Evidence: scan API returns real AP records, controlled
   local network only, repeat scan, safe stop/deinit, no fault.
   Then WPA2-CCMP association, DHCP/IP and Linux network integration
   (cfg80211/FullMAC goal). No TKIP security downgrade; no Bluetooth
   until Wi-Fi path understood. No claim of native wlan0 until
   Linux integration actually works.

## Administrative / tooling continuity

Linvora-dev persistent build host, ~/projects/linux-s31,
~/worktrees/s31-native-radio (BSP), ~/projects/opensbi-s31.
Pinned GCC14.1.1 cross prefix
~/toolchains/s31-linux-kernel-gcc-14.1.1/bin/riscv64-esp-linux-musl-.
Private vendor archives in ~/deps/s31-radio, source/headers in
~/deps/s31-research/radio-demand-20261127. Host tools in
~/deps/s31-host-tools/root/usr. Check free disk before builds;
available around 4.4 GiB at checkpoint. Keep build output outside
git. Orion physical host may be unavailable because it is being
returned; verify connected device and UART access before bench.
Use Remote Desktop Commander device discovery, do not restore
personal GitHub credentials. Private draft release asset transport
with short-lived signed URLs only, verified SHA256, no credential
transfer. Use exact release rollback and verified fresh flash preflight.
UART /dev/ttyUSB0, pyserial 115200, esptool chip esp32s31 1500000.
Keep raw boot logs, check profile and boot IDs, fail on RCU/Oops,
distinguish incomplete transport from kernel failure. Check rollback
even on failed trial. No physical power-cycle claim for RTS reset.
Original kernel span 4300800 bytes at 0x500000, SHA-256
8a0ef4de94b59d407cfbf668ae4625d2be9ec0d6372dcab4906e9a5ebecc0e3b.
Five slots: SPL 0x2000, FIT 0x100000, DTB 0x300000,
XIP 0x500000, CramFS 0xC00000.
Original images on Orion ~/source/build/s31-lab-v1-20261126.
Prior combined F-only runner location:
~/source/build/s31-f-only-dma-qualification-20261127/.
Current corrected bench proof:
~/source/build/s31-rcu-investigation-20261127/uart-control/hardened-v2.
Reuse prior proven helpers, preserve original files and hashes.
Use Codex Astra read-only review for risky changes; review results
are evidence only if completed. Keep experimental branches isolated,
signed-off commits, git diff --check, push only personal fork,
verify clean status. Do not submit upstream without user-approved
validated changes. No fabricated PASS, no future-work promises.

## Link closure refinement (later checkpoint)

See NATIVE-WIFI-FTM-LINK-20261127.md. Matched ld -r control removes
128 unreferenced LOCAL UND entries from the original 345 count;
217 meaningful names remain in normalized closure. Actual official
S31 FTM source resolves 24 with zero new undefined: 193 remaining,
180 ROM name matches and 13 non-ROM. This does not resolve ROM
ABI or prove runtime execution. Reproducible script and verified
private inputs documented; narrow Codex review completed, two
broad audits timed out. Resume with regulatory data and precise
ROM/ABI inventory rather than redoing FTM.

## Runtime closure continuation checkpoint

Supersedes earlier pending kernel-build and qualification steps.
Combined kernel 95d9d308df6e980d4b1a421c694be09b290c79a9 now passed
bounded physical FPU/SRAM/reboot tests; promoted to personal-fork
integration/s31-native-wifi. Lab v1 restored, both full flash spans
and all five components verified, UART free. Private evidence saved.
See NATIVE-WIFI-RUNTIME-CLOSURE-20261127.md for exact evidence,
script reproduction and limitations. BSP branch
experiment/s31-native-wifi-runtime-closure contains new checkpoint.
Normalized 217->193 FTM->191 regulatory->166 math->164 pure
helpers; 53 actual resolutions, zero new undefined in these four
stages. Scoped radio free routed to the tested ledger allocator,
combined closure 168 due five new ordinary runtime symbols. No
vendor runtime execution. 157 ROM addresses (100 reserved SRAM,
57 ROM aperture); actual relocation inventory saved. Next focus:
ROM data initialization and access/ABI, remaining logging/runtime,
resource broker with narrow real DMA/IRQ/PHY/clock ownership.
No full peripheral matrix or 20-cycle campaign until Wi-Fi works.
