# S31 F-only v2 physical requalification (2026-11-26)

## Artifacts and scope

Kernel `060679ff35be74800f0fcb792296e01e5ff524ee`, built wholly
on Linvora-dev with the pinned GCC 14.1.1 and Lab v1 config plus
RISCV_EXPERIMENTAL_F_ONLY. Kernel SHA-256
`461e97f16e7b8e362e83e4e7f6048c3e0e42c1b1a2eeb7306b6ce4a4d90f5f47`.
Optional F-only DTB SHA-256
`782fb1aa5af5af721aeae84d84e83e75331d05eea0ef5862d8cfcaa23a85529d`.
Both verified on Orion before flashing. SPL, FIT/OpenSBI, rootfs
and all other slots unchanged. Strict userspace suite built on
Linvora-dev using GCC 15.2.0-5, ELF SHA-256
`8c81e19863e54491e6c6c40741501b28935a35813faf1330f3cad5028cfb0a46`;
hard-float ABI companion SHA-256
`fd67f72c08bfaff3f15a00db154f48a5cc87d61faac67d70d8a7b19e6422d98e`.
Transport via a private unpublished draft, not a new Lab reference.

## Results

| Test | Physical v2 outcome |
|---|---|
| Original no-F DTB, SMP, read-only CramFS | PASS |
| ILP32F call ABI | PASS |
| Valid signal-frame mutation | PASS |
| Fork isolation and exec reset | PASS |
| Partial register write actually observed | PASS |
| Canonical FP state after faulting ptrace copy | PASS |
| Normal FP register write/read and child resume | PASS |
| D instruction rejection | PASS (5/5, SIGILL) |
| Misaligned F and ZCF | PASS |
| SMP pinned/migrating register context | PASS (8192/8192, 5875 ms) |
| Software reboot after functional suite | PASS (8.62 s, new boot ID) |
| Separate bad signal-frame partial-copy probe | INCONCLUSIVE |
| Restore v1 and verify all five slots (both rounds) | PASS |

The stricter regression test requires a demonstrated partial write,
then canonical FP register representation. It emitted
PTRACE_PARTIAL_WRITE_OBSERVED, PTRACE_FAULT_CANONICAL=1 and
PTRACE_FAULT_PASS. The prior v1 experimental kernel had reported
a canonicalization gap. This is physical evidence for the v2
ptrace change, not just compilation or a synthetic success.

The first F-enabled boot failed at ~2.30 s during esp-example-tar,
CPU1 PID39, with load page fault (cause 0xd), EPC c00a262c
(anon_vma_interval_tree_remove), RA c00b3650
(anon_vma_interval_tree_pre_update_vma), badaddr aa0a8ada.
The exact v2 vmlinux resolves the instruction to lw a5,4(a1);
a1 was aa0a8ad6. A bounded ROM-assisted reset recovery succeeded.
The analogous class of anonymous-VMA failures has been observed
on original Lab v1 without F. See
[BOOT-FAULT-SCREEN-20261126.md](BOOT-FAULT-SCREEN-20261126.md).
This does not establish the underlying cause or rule out additional
F-only regressions. One successful reboot is not reliability
qualification.

## Additional signal-return negative-path coverage

A separate experimental probe reached the kernel's bad-frame handling
and saw the expected SIGSEGV stop, but did not observe the intended
partial change in saved FP state. Register 0 retained its original
value. Consequently the probe exited nonzero and the result must be
classified **inconclusive**, not a regression confirmation or pass.
The valid signal-frame mutation test did pass.

A bounded independent Codex CLI review on Linvora-dev agreed that
absence of the intended write cannot establish normalization behavior:
the copy may not commit the prefix, or later FP state saving may
overwrite it. Stop attribution and FP state observation need further
review. The probe is experimental, not part of the successful strict
qualification gate. No system-protection or host settings changed.

## Recovery and remaining qualification gates

Both runs restored the original v1 DTB and XIP kernel, verified
all five flash component digests and passed the baseline RV32IMAC
no-F/SMP diagnostic (HWCAP 0x1105, F=0 D=0 HWPROBE_FD=0).
The board was left running v1 with UART free. Raw logs stay on
Orion under `~/source/build/s31-f-only-v2-qualification-20261126/`;
private review on Linvora-dev under
`~/deps/s31-research/fpu-v2-sigreturn/codex-sigreturn-review.md`.

F-only v2 is physically functionally validated for the tested scope,
including the demonstrated ptrace correction. It remains an
experimental branch. Resolve the separate boot stability issue,
complete the signal-return negative-path qualification and review
the patch series/ABI with maintainers before proposing merge or a
new reference release. No public release or PR created.
