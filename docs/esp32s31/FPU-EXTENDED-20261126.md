# ESP32-S31 — extended F-only qualification / Linvora-dev handoff

Date: 2026-11-26. Experimental; not ready for upstream RFC or production.
Previous initial qualification: [FPU-QUALIFICATION-20261126.md](FPU-QUALIFICATION-20261126.md).

## Physical results (candidate v1)

Kernel integration `a1b315e2e4ba386693ebb1f2c0ba44ec46fa6aba`,
XIP SHA-256 `1a0476869628008b8027c5554d19423bc9af20bda337e91cf79ec76359ae2b28`,
F-only overlay DTB SHA-256 `782fb1aa5af5af721aeae84d84e83e75331d05eea0ef5862d8cfcaa23a85529d`.
S31 v1 flash was validated before testing. First the candidate XIP was
installed with the **unchanged no-F DTB**; the diagnostic correctly
returned 77 (HWCAP 0x1105, F=0 D=0 HWPROBE_FD=0). Then the F-only test
DTB was installed; the candidate XIP and DTB were digest-verified.

The new freestanding ILP32F test (separate C translation units, not
linked against the soft-float Linux libc) executed actual F arithmetic,
float and integer register arguments, ninth float in integer ABI and
seventeenth float on the stack. The extended soft-float diagnostic
uses explicit assembler for F/D instruction testing and checks:

| Test | Observed |
|---|---|
| ILP32F cross-TU calls | PASS |
| Signal frame read/mutation and normalized upper words | PASS |
| Fork inheritance and parent/child isolation | PASS |
| Exec resets FP registers/FCSR | PASS |
| Successful ptrace SET/GET immediate canonicalization | PASS |
| Faulting partial ptrace SETREGSET | **FAIL**: EFAULT with nonzero upper word |
| 5 D operations (FADD.D, aligned and misaligned FLD/FSD) | SIGILL, expected |
| Misaligned F load/store | PASS |
| Misaligned compressed ZCF load/store | PASS |
| Four concurrent processes (2 pinned and 2 alternating CPU affinity) | 8192 checks PASS, 5724 ms |
| Software reboot after extended suite | **FAIL**, early init Oops/panic |
| Restored S31 Lab v1, all five slots and baseline no-F boot | PASS |

Actual key results:
```text
CANDIDATE_NO_F_PASS
ILP32F_CALL_ABI_PASS
SIGNAL_FRAME_MUTATION_PASS
FORK_ISOLATION_PASS
EXEC_RESET_PASS
PTRACE_FAULT_CANONICAL=0
PTRACE_IMMEDIATE_PASS
PTRACE_FAULT_INVARIANT_GAP
D_REJECT_PASS case=0 signal=4
D_REJECT_PASS case=1 signal=4
D_REJECT_PASS case=2 signal=4
D_REJECT_PASS case=3 signal=4
D_REJECT_PASS case=4 signal=4
MISALIGNED_F_PASS
MISALIGNED_ZCF_PASS
SMP_STRESS_PASS checks=8192 elapsed_ms=5724
S31_F_EXTENDED_PASS
RESTORE_WRITE_PASS
RESTORE_ALL_SLOTS_PASS
RESTORE_BASELINE_PASS
```

The final diagnostic marker means execution completed, not that all
invariants or reboot passed. The revised host runner requires the
fault-injection invariant and reboot to succeed before marking the
entire qualification passed.

A partial SETREGSET fault can modify the saved state before returning
EFAULT. The previous implementation canonicalized only on success.
V2 commit `060679ff35be74800f0fcb792296e01e5ff524ee` on
[`experiment/s31-lab-f-only-v2`](https://github.com/gnfranco/linux/tree/experiment/s31-lab-f-only-v2)
corrects both ptrace and sigreturn paths by normalizing F-only upper
words even for a faulting partial user copy. This does not promise
atomicity of the lower words, nor change semantics for D harts.
That particular correction requires physical retest.

## Unresolved warm reboot failure

After successful userspace tests and RAM artifact cleanup, software
reboot started but kernels on both harts faulted during early init.
The captured symbol map for the exact v1 candidate identified:

- CPU0 PID38 init, epc c00a2170 `vma_last_pgoff`, bad address 0x4,
  cause 0xd, a0=0. Call path: anonymous VMA interval tree augment
  propagate, remove, unlink_anon_vmas, free_pgtables, exit_mmap,
  begin_new_exec, load_elf_binary.
- CPU1 PID1 init, same epc/address, augment_rotate, anon_vma_clone,
  dup_mmap; kernel panic attempting to kill init.

This is not proof of a causal relationship to FPU state. Potential
memory/SMP, reset state and generic VMA issues require isolation.
Do not add a blind null-pointer workaround in mm/interval_tree.c.
The expected SIGILL from D test children is not a kernel Oops.
The full run returned 1 for reboot failure; the guarded rollback
restored v1 and verified all five slot digests, booted both harts,
and reran the original no-F baseline successfully. UART free.

Bounded next experiments: reboot repetitions of unmodified v1 vs
candidate no-F vs candidate F-only without stress, then with
integer-only and FP stress, keeping cold/hard reset distinguished
from warm software reset; hook startup/reboot logging. Compare
failure rates and symbolized traces; preserve peripheral state and
rollback at each gate. Do not infer causation from a single failure.

## Configuration builds

With the same pinned GCC 14.1.1 on Orion, the affected objects built
successfully for (a) FPU=y, experimental option off, and (b) FPU=n.
Disassembly of fpu.o with option off was identical to the original
v1 FPU assembly, with no F-only save symbol. D-hart runtime regression
is not represented by this static equality check; the S31 has no D
and this environment did not have a RISC-V system emulator.
Core dump qualification is unavailable in current v1 config
(`CONFIG_COREDUMP=n`).

## Reproduce userspace diagnostics

```sh
experiments/s31-fpu/build-extended.sh \
  "$HOME/toolchains/s31-linux-gcc-15.2.0-5/bin/riscv32-esp-linux-musl-gcc" \
  /tmp/s31-fpu-extended-build
```

Stripped ELF SHA-256:
- 42,240 bytes, `fpu-extended.stripped`:
  `d856425decd26d3739420853ac8e01ac035577d5dcd341a1dc5052cb6b0306f5`
- 4,716 bytes, `abi-hardfloat.stripped`:
  `fd67f72c08bfaff3f15a00db154f48a5cc87d61faac67d70d8a7b19e6422d98e`

Both reproduced byte-for-byte on Linvora-dev and verified on Orion
and target. `run-extended.py` transmits into RAM only; it intentionally
executes faulting user-mode D opcodes in isolated children and
reboots, so use only on an agreed test candidate and authorized bench
with guarded rollback. Raw traces are private on Orion at
`~/source/build/s31-f-only-extended-20261126/`; do not publish.

## Linvora-dev migration

Linvora-dev is now the development environment, Orion only the
physical bench while available.

- Kernel checkout: `~/projects/linux-s31`, fork branch
  `experiment/s31-lab-f-only-v2`. Shallow (depth 2 initial clone); fetch
  needed upstream history for future independent contributions.
- BSP worktree: `~/worktrees/s31-fpu-qualification`, extended
  experiment branch; source and documentation tracked in Git.
- Original userspace compiler: `~/toolchains/s31-linux-gcc-15.2.0-5`.
- Pinned kernel compiler installed in
  `~/toolchains/s31-linux-kernel-gcc-14.1.1`. Official archive:
  `https://dl.espressif.com/dl/toolchains/preview/riscv64/14.1.1_20260922/riscv64-esp-linux-musl-14.1.1_20260922-x86_64-linux-gnu.tar.xz`,
  SHA-256 `e04069baebe4aca9ebd6d2a235adc400e89c661e51a32c8a0d221a01e484081b`,
  identical to the original Orion archive. Version esp-linux-14.1.1_20260922.
- Debian 12 user-local host tools extracted from signed repository
  packages to `~/deps/s31-host-tools/root`; no global package install
  and no host/production changes. Bison 3.8.2, flex 2.6.4, bc 1.07.1.
  Package files retained in `~/deps/s31-host-tools/packages`.
- Build is constrained to two parallel jobs (the dev VPS has 2 vCPUs).
  The exact checked-in v1 config SHA is
  `71c1077bb6cd8f07a091db0ffd0e7f5bda36ad954cc5bc74bd29960def4f8b38`.
  The v2 build output is
  `~/deps/s31-research/kernel-f-only-v2-build`; Kconfig diff is exactly
  `+RISCV_EXPERIMENTAL_F_ONLY y`. Affected signal.o and ptrace.o have
  cross-compiled on Linvora-dev. Full XIP build initiated.
- Codex CLI 0.172.0 authenticated with ChatGPT; independent review
  invoked in read-only mode with a sanitized technical summary;
  shell sandbox cannot create user namespaces in this container.
  Authorized read-only file access is available. Do not disable
  sandbox protections or change host namespace settings to run it.

## Independent Codex CLI reliability review

Codex CLI 0.172.0 was invoked with read-only sandbox and a sanitized
technical summary. The initial broad review could not complete
(container user-namespace restriction and a platform filter). Without
changing security settings, a second task reviewed only supplied
reboot observations and relevant source excerpts via stdin. Completed
successfully and produced a reliability decision: retain Lab v1;
candidate not ready for reboot-dependent work.

The reviewer independently confirmed that the traces locate a
null VMA argument in the anonymous-VMA interval-tree path, not the
origin of the invalid pointer. They do not establish that two harts
modified the same node or even the same tree. The proposed initial
matrix is 10 attempts per variant/condition: v1 vs candidate,
power-cycle with 2 harts, software reboot with 2 harts, software
reboot with 1 hart. Add no-F candidate as a control and distinguish
reset that never reaches boot vs early-kernel failure vs init timeout.
Power-cycle and one-hart controls require bench capability; do not label
a serial RTS reset as full power-cycle.

Keep serial evidence from reset request to final init marker, exact
artifact/config hashes and CPU count, first exception and symbolized
stack. A small zero-failure sample is screening, not reliability
qualification. Use separate integer/FP stress as additional factors
after establishing the no-load reboot baseline. Don't modify the VMA
tree implementation based on a single failure.

Review workflow: run Codex with `--ephemeral -s read-only` in the
kernel checkout; pipe a curated technical context on stdin, request
analysis only, save final review in a private scratch report. The
container prohibits unprivileged user namespaces, so do not disable
sandbox or change the production host to enable CLI shell operations.
