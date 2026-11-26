# ESP32-S31 upstream flash_args validation — 2026-11-26

Hardware: Function-CoreBoard-1 rev 0.0, 16 MiB NOR; esptool 5.3.1, UART at 1,500,000 baud.

Community baseline: `9f3781cf9b7c9f034d6a07da405e786b860accb5`.
Official Espressif BSP tip: `9ecea5e6785b95270f6b21d7ce7dc769f9c7f81c`, including `bcf9ceda354cb7efb66769cc3586cb582946442a` (tools: write an esptool argument file next to the merged image).
Integrated merge: `3a5001bd99481aa304ffdfc808271eac4d28b137`; no conflicts, diff whitespace and shell syntax checks passed. No changes to driver code.

## Packaging regression

The merged packaging script was run with the previously validated official artifact set, checked byte-for-byte against the preserved vendor reference image. This is a packaging/flash regression, not a fresh kernel or firmware build.

Generated argument file (relative paths, run from the artifact directory):

```
--flash-mode dio --flash-freq 80m --flash-size 16MB
0x2000 spl_app.bin
0x100000 u-boot.itb
0x300000 esp32s31-function-core-board-1.dtb
0x500000 xipImage
0xC00000 rootfs.cramfs
```

The merged raw image was 16,084,992 bytes, SHA-256 `86d3fff769ad70d442e25ceff5b98640e101fc7135dd793ee16a7a663dc54909`. Replaying the generated file with esptool merge-bin produced identical bytes. With the optional rootfs omitted, the four-slot argument file also reproduced its merged image byte-for-byte.

## Physical flash and boot

A full 16 MiB backup of the running community installation was taken and checked before writing. The board was programmed with esptool write-flash @flash_args and subsequently checked with verify-flash @flash_args, yielding five successful flash digest comparisons:

| Component | Offset | Result |
| --- | --- | --- |
| SPL | 0x2000 | Pass |
| U-Boot FIT | 0x100000 | Pass |
| Board DTB | 0x300000 | Pass |
| XIP kernel | 0x500000 | Pass |
| CramFS rootfs | 0xC00000 | Pass |

Serial output confirmed Linux 6.18.0+, two CPUs online at SMP bring-up, the read-only CramFS root mounted successfully, the vendor greeting, and a shell prompt. The automated prompt matcher initially failed because the vendor console contained NUL bytes; manual inspection of the captured serial log confirms the prompt. The additional automated command and reboot checks for the official boot were not performed.

## Restoration

The original full 16 MiB community flash was restored, with both the write-time hash verification and an independent verify-flash digest comparison over all 16,777,216 bytes passing. The restored board booted Linux 6.18.0 and mounted SquashFS; the corrected shell checker ran commands and reported CPU=0-1 and completion marker. No private flash backup, device identifiers, network configuration, or raw serial logs are published.

Scope: regression of the upstream packaging and physical programming path, plus verified restoration. No new radio, audio, peripheral, or sustained performance tests were performed in this flash cycle.
