# S31 Lab v1 — reproducible warm reboot failure (2026-11-26)

Status: confirmed on two kernel variants; root cause unresolved. This is
an independent BSP stability issue, not a demonstrated F-only regression.
Do not propose an MM workaround without a reproducible root cause.

## Observations

Platform: ESP32-S31 Function-CoreBoard-1, 16 MiB NOR / 16 MiB PSRAM,
two harts, Linux RV32 XIP 6.18, same SPL/U-Boot/OpenSBI/CramFS.
See [FPU-EXTENDED-20261126.md](FPU-EXTENDED-20261126.md).

1. Experimental F-only kernel `a1b315e2e4ba386693ebb1f2c0ba44ec46fa6aba`
   passed ILP32F and SMP stress then failed on the next software reboot
   at approximately 2.34 seconds in init.
2. Restored original Lab v1 kernel
   `9b41c9c5df88ed883b05662187e431bb3452e491`, no F advertised:
   three consecutive software reboots passed (8.39, 8.29, 8.56 s).
   The **fourth** failed. Fifth was not attempted. No F instructions
   or experimental kernel were involved in this series.
3. No flash writes during the baseline reboot series. The runner
   stopped on the first error, recovered via esptool flash-id (RTS
   reset, not a power cycle) and verified a successful v1 no-F boot
   with both harts, CramFS and the diagnostic HWCAP 0x1105. UART free.

## Symbolized failure comparison

| Field | F-only candidate | Unmodified v1 |
|---|---|---|
| First failure after boot | ~2.34 s | ~2.27 s |
| Program | init | init |
| First Oops CPU/PID | 0/38 | 0/39 |
| Second Oops CPU/PID | 1/1 | 0/1 |
| PC | c00a2170 | c00a1de2 |
| Symbol | vma_last_pgoff | vma_last_pgoff |
| Fault address | 0x4 | 0x4 |
| Cause | 0xd | 0xd |
| a0 | 0 | 0 |
| Outcome | panic killing init | panic killing init |

The original v1 was symbolized against its exact saved vmlinux on
Orion. First trace includes
`anon_vma_interval_tree_remove -> unlink_anon_vmas -> free_pgtables
-> exit_mmap -> __mmput -> begin_new_exec -> load_elf_binary`;
second includes
`anon_vma_clone -> anon_vma_fork -> dup_mmap -> copy_process ->
kernel_clone`. The candidate matches this core path and the exact
faulting instruction sequence. It is significant that the second
exception also occurs on CPU0 in v1; simultaneous faults on different
harts are not required for the observed failure.

This demonstrates an existing issue in the v1 platform and that F-only
is not necessary to produce the symptom. It does not establish an
identical underlying cause, quantify the relative failure rate, prove
a generic Linux race, or prove a PSRAM/cache/reset mechanism. The
single-hart, true power-cycle, cold/warm comparisons and additional
boot repetitions remain important. No change to MM has been made.

## Evidence / next test gate

The serial traces and scripts were retained locally on Orion under
`~/source/build/s31-f-only-extended-20261126/`; sanitized trace
observations are recorded here to permit continuation without Orion.
The exact baseline and rollback archive was downloaded from the
private release to Linvora-dev, with all component digests verified.

Build and diagnostic work continues on Linvora-dev. Candidate F-only v2
addresses the independently reproduced partial-ptrace-copy invariant.
The warm reboot issue is a separate stability investigation.
