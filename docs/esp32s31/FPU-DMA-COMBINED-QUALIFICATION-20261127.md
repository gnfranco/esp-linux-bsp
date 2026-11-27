# ESP32-S31 F-only v2 + early DMA alignment: combined qualification

Date: 2026-11-27. Experimental integration only; **not** a replacement
for S31 Lab v1 and **not** evidence of independent RFC DMA hardware
qualification. F-only and DMA fixes remain separately reviewable upstream.

## Exact candidate and reproducible artifacts

- Linux commit: `d3e404995222d8933317433c55bb355b126a2cb8`
  (`experiment/s31-lab-f-only-v2-dma`), adding the 13-line early
  non-coherent DMA alignment protection to the separately qualified
  F-only v2 commit `060679ff35be74800f0fcb792296e01e5ff524ee`.
- Build: Linvora-dev, pinned ESP Linux GCC 14.1.1, the existing Lab v1
  kernel configuration with experimental F-only enabled. The
  `full-config.diff` was empty compared with the prepared config.
  Build exit 0; started 11:03:07Z, finished 11:27:37Z (24m30s).
- XIP image: 4,294,412 bytes, SHA-256
  `07137d46b6c265aa8c0a04aec2e2a6e2b74239b482ee26b03f552a0feacf2146`.
- `.config` SHA-256:
  `ea4ef1490fb8054f16ad970e72df37f0adeae361ff84096c77bee5c593c20500`.
- `vmlinux` SHA-256:
  `9e08bc973114c4c73e9723d571c7cc5eeed77be4f7392db1e0095644812264a9`.
- `System.map` SHA-256:
  `80e1424834d8a152bc5638b2fe2e38c03b7ca07f88f2c7cb251cc16a9e5da4e9`.
- F-enabled laboratory DTB SHA-256:
  `782fb1aa5af5af721aeae84d84e83e75331d05eea0ef5862d8cfcaa23a85529d`.
  The standard no-F DTB was also verified before its test.
- The existing pinned F-only userspace ELF suite was reused without
  rebuilding: extended ELF SHA-256
  `8c81e19863e54491e6c6c40741501b28935a35813faf1330f3cad5028cfb0a46`;
  hardfloat ABI companion
  `fd67f72c08bfaff3f15a00db154f48a5cc87d61faac67d70d8a7b19e6422d98e`.

The candidate XIP and DTB were transferred in individually verified
chunks to Orion. The physical script checked the candidate and exact
baseline artifacts by SHA-256 before modifying flash, and checked the
restored image afterwards. This was a temporary bench qualification;
the published Lab v1 rollback remains untouched as a release.

## Physical results (Orion, Function-CoreBoard-1)

| Verification | Result |
|---|---|
| Original no-F DTB profile, SMP/read-only CramFS baseline | PASS |
| Candidate artifact and F-only DTB hashes before flash | PASS |
| ILP32F call ABI | PASS |
| Valid signal-frame mutation | PASS |
| Fork FP isolation / exec FP reset | PASS |
| ptrace immediate and faulting partial-copy normalization tests | PASS |
| Five forbidden D-instruction probes | PASS, 5/5 SIGILL |
| Misaligned F and Zcf probes | PASS |
| SMP pinned/migrating FP context tests | PASS, 8,192/8,192 (5,761 ms) |
| Software reboot after functional suite | PASS, 8.28 s |
| Restore original kernel and standard DTB, verify all five flash slots | PASS |
| Original Lab v1 no-F SMP baseline after restoration | PASS |

The runner exited 0. No new flash partition map or permanent image
was introduced. The final Orion state was original S31 Lab v1 with
the UART released.

## Evidence retention

On Linvora-dev (private, not for direct publication):
`~/deps/s31-research/native-radio-next-20261127/f-only-dma-object/`
contains the complete build tree and `full-build.log`,
`full-build.status`, `FULL-SHA256SUMS`, and
`bench-evidence/bench-evidence.tar.gz`. The archived evidence
SHA-256 is
`f7c4a02611af8bab6793ee48232f448d01b168a71eb6a1cd396863f44f41a288`.
It includes the test runner and sanitization-sensitive UART/flash logs;
do not commit it or raw private identifiers.

Physical test files are under
`~/source/build/s31-f-only-dma-qualification-20261127/` on Orion.
A condensed verification transcript has markers
`CANDIDATE_NO_F_PASS`, `CANDIDATE_EXTENDED_STRICT_PASS`,
`CANDIDATE_SOFTWARE_REBOOT_PASS`, `RESTORE_ALL_SLOTS_PASS`,
and `RESTORE_BASELINE_PASS`.

## Limitations and next gates

This is a **single combined functional qualification**, not a
statistically meaningful boot-stability comparison. It does not prove
that early DMA alignment caused or eliminates every intermittent
anonymous-VMA memory failure, and does not repeat the full Ethernet,
USB, CAN, RMT, GPIO, I2C and audio/peripheral regression matrix in
this session. The isolated DMA RFC previously compiled; its physical
proof was on a combined tree, which remains distinct. The separate
invalid sigreturn frame / partial-copy test remains **inconclusive**,
not a pass. Its diagnostic is documented in
[FPU-V2-QUALIFICATION-20261126.md](FPU-V2-QUALIFICATION-20261126.md).

Do not merge this experimental combination into either independent
upstream patch merely on the strength of the combined qualification.
Retain Lab v1 as rollback, finish F-only negative-path qualification,
and re-run stability and peripheral controls before selecting a Lab v2
baseline.
