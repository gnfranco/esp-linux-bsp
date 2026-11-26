# ESP32-S31: experimental RV32 F-only qualification (2026-11-26)

Status: prototype, not production. No flash writes, no changes to S31 Lab v1.

## Why

The official S31 ISA is RV32 with single-precision F; D must not be assumed.
The official Linux baseline at
`7fdbad0c589c791cd2a6bc73fa9fcec8dbce4b11` rejects F without D in
`cpufeature.c`; the existing `fpu.S` context path uses FLD/FSD. Both
S31 Lab v1 harts advertise RV32IMAC, not F. This matters before trying
official IDF radio archives built for the ILP32F ABI. Merely changing a
compiler flag or DT does not establish correct FP context management.

Prior work: [naelolaiz/esp32s31-alpine F-only journal](https://github.com/naelolaiz/esp32s31-alpine/blob/9f7e8a2c86bddb9c07709c4870c79ffdfba3fdd6/docs/journal/2026-10-10-fpu-f-only.md).
The author previously demonstrated FADD and context tests on hardware.
Credit belongs to that author; the separate prototype below does not copy
that patch.

## Experimental kernel

`gnfranco/linux:experiment/s31-f-only`, commit
`5213e67c6eab00ead36692591ca0ab1038a9ab13`.

- Opt-in `CONFIG_RISCV_EXPERIMENTAL_F_ONLY`, default off, RV32 Espressif
  with FPU and no KVM.
- Separate F-only save/restore using FSW/FLW for all 32 registers and FCSR;
  retain the D path. Preserve the size of the existing 64-bit UAPI slots
  and zero high words on F-only save and successful writes by sigreturn
  and ptrace. This ABI choice is a prototype, not an upstream agreement.
- Restrict common FP support against mixed-width harts. The hwprobe
  `RISCV_HWPROBE_IMA_FD` bit denotes F **and** D, and must remain clear.
- Keep kernel-mode FP acceleration D-only and gate emulated misaligned
  FP accesses by width.
- No device-tree change or new default; the F-only path has not been
  enabled on the physical board.

Cross compilation of modified and dependent objects passed on Orion with
Espressif GCC 15.2.0 (RV32): fpu.o, cpufeature.o, sys_hwprobe.o,
signal.o, ptrace.o, process.o, traps_misaligned.o, kernel_mode_fpu.o.
`git diff --check` and `scripts/checkpatch.pl --no-tree --no-signoff --no-summary -`
passed. This is not proof of a linked/bootable kernel or context correctness.

## Diagnostic suite

`experiments/s31-fpu/` uses a static soft-float ILP32 executable with
FP instructions confined to assembly routines entered only after HWCAP
and hwprobe checks. Build on Linvora-dev:

```sh
experiments/s31-fpu/build.sh \
  "$HOME/toolchains/s31-linux-gcc-15.2.0-5/bin/riscv32-esp-linux-musl-gcc" \
  /tmp/s31-fpu-build
```

The stripped ELF was reproduced byte-for-byte with GCC 15.2.0:
38,136 bytes, SHA-256
`e12c92fff0bc61dba0551809f5c2105d1b3ab4d414883e0dfcedfee5b1185607`.
The deterministic gzip produced by build.sh may differ from the original
transport gzip header; the decompressed ELF is the integrity reference.

The suite returns 77 if F is not advertised, with no FP instruction
executed. An actual F-only candidate must pass FADD.S on both harts,
32-register/FCSR signal restoration, two workers (128 forced migrations
each), and ptrace GETREGSET/SETREGSET including the upper-word invariant.
Only use it on a controlled candidate because failing FP context may
cause SIGILL, corruption, or a kernel exception.

### Physical baseline (Orion)

UART checked free and acquired exclusively. Executable transmitted to
tmpfs, decompressed, checked against SHA-256 on target, executed and
removed; no flash write. Evidence:

```text
riscv32
6.18.0+
0-1
mtd:rootfs / cramfs ro,relatime 0 0
isa : rv32imac_zicsr_zifencei_zaamo_zalrsc_zca
isa : rv32imac_zicsr_zifencei_zaamo_zalrsc_zca
TARGET_SHA256_PASS e12c92fff0bc61dba0551809f5c2105d1b3ab4d414883e0dfcedfee5b1185607
HWCAP=0x1105 F=0 D=0 HWPROBE_FD=0
FPU_NOT_ADVERTISED: hardware opcodes not executed
FPU_BASELINE_PASS
TARGET_ARTIFACT_REMOVED
```

Retain the existing physical log at Orion:
`~/source/build/s31-fpu-userspace-20261126/baseline.log`.

A baseline-only UART runner is included. Requires pyserial, fuser,
and a compatible trusted UART helper implementing shell and command
(see experiments/s31-native-radio/run-uart.py). It verifies the baseline,
uses RAM, and expects status 77; it is not the candidate runner.

## Next qualification gate

1. Review S31 F capability, OpenSBI FS initialization, boot hart ISA and
   errata; create a separate DT candidate declaring F only after validation.
2. Integrate the prototype on the known-good Lab v1 kernel, without losing
   independent CAN/RMT changes. Build and check XIP image, DTB, memory
   layout and rollback; do not modify the functional image in-place.
3. Boot a separately identified candidate with an agreed reversible
   deployment method; require F=1, D=0 and HWPROBE_FD=0 and
   S31_F_ONLY_PASS. Exercise fork/exec, signal frame modification, core
   dump/privacy, lazy state, misaligned accesses, affinity and stressing
   both harts. Add D/no-F regressions before proposing upstream.
4. Only after these gates, attempt execution of official radio code which
   requires F. The metadata-only radio probe did not prove RF initialization.
