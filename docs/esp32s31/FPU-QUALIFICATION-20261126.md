# ESP32-S31: experimental RV32 F-only qualification (2026-11-26)

Status: experimental prototype, initial F-only hardware qualification passed. The candidate was tested temporarily and S31 Lab v1 was restored and verified.

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

## Initial qualification plan (before physical testing)

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

## Lab v1 integration candidate (same day)

The separate integration branch
[`gnfranco/linux:experiment/s31-lab-f-only`](https://github.com/gnfranco/linux/tree/experiment/s31-lab-f-only)
is based on the exact v1 kernel tree, not the plain official integration
branch. Commit `a1b315e2e4ba386693ebb1f2c0ba44ec46fa6aba` cherry-picks
the prototype onto `9b41c9c5df88ed883b05662187e431bb3452e491`.
The original branches remain unchanged.

A full `xipImage dtbs` build passed on Orion with the **same pinned**
Espressif GCC 14.1.1 (esp-linux-14.1.1_20260922) as Lab v1. The
input config SHA-256 matched the checked-in
`configs/esp32s31-lab-v1.kernel.config`
(`71c1077bb6cd8f07a091db0ffd0e7f5bda36ad954cc5bc74bd29960def4f8b38`).
The only Kconfig delta after olddefconfig was:

```text
+RISCV_EXPERIMENTAL_F_ONLY y
```

```sh
# On Orion, using the separate integration worktree:
cd "$HOME/source/worktrees/s31-lab-f-only"
out="$HOME/source/build/s31-lab-f-only-pinned-20261126"
# For reproduction choose a new, empty output directory.
mkdir "$out"
cp "$HOME/source/build/s31-ctucanfd-qa-20261126/.config" "$out/.config"
scripts/config --file "$out/.config" --enable RISCV_EXPERIMENTAL_F_ONLY
cc="$HOME/source/build/esp32s31/host/opt/ext-toolchain/bin/riscv64-esp-linux-musl-"
make O="$out" ARCH=riscv CROSS_COMPILE="$cc" olddefconfig
scripts/diffconfig "$HOME/source/build/s31-ctucanfd-qa-20261126/.config" "$out/.config"
make -j6 O="$out" ARCH=riscv CROSS_COMPILE="$cc" xipImage dtbs
```

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Candidate xipImage | 4,294,412 | `1a0476869628008b8027c5554d19423bc9af20bda337e91cf79ec76359ae2b28` |
| Candidate .config | — | `ea4ef1490fb8054f16ad970e72df37f0adeae361ff84096c77bee5c593c20500` |
| Candidate DTB | 10,608 | `782fb1aa5af5af721aeae84d84e83e75331d05eea0ef5862d8cfcaa23a85529d` |
| Test overlay DTBO | 484 | `6e98c879e0dab0b29e2cedfca886aac5ebcf3013410f489c4d60318775a7a87e` |

XIP address remains `0x40400000`. XIP image size equals the released
v1 size; its SHA differs as expected. The 7 MiB flash slot has 3,045,620
bytes unused (not a claim of radio space). Disassembly and linked
symbols confirm all existing D save/restore (32 FSD/32 FLD) and new F
save/restore (32 FSW/32 FLW) functions. SMP, CAN CTU FD, and RMT LED
configuration remain enabled; their runtime regressions are still pending.

The optional overlay `experiments/s31-fpu/esp32s31-test-f-only.dtso` is
not applied to the standard DTB. It declares F (not D) on both harts.
`build-dtb.sh` applies it offline to the released **post-overlay** v1
board DTB (SHA-256 `090508d75505c389b915dd868b49c180650ec066876a803ae28394bc644eac42`),
verifies the input hash, compares disassembled DTBs, and rejects any
other change. Repeated generation produced an identical candidate DTB.
No CAN enablement or pin change is introduced.

```sh
experiments/s31-fpu/build-dtb.sh \
  "$HOME/source/build/s31-ctucanfd-qa-20261126/scripts/dtc" \
  "$HOME/source/build/s31-lab-v1-20261126/esp32s31-function-core-board-1.dtb" \
  /tmp/s31-lab-f-only-dtb
```

The pinned OpenSBI `0940d8a5834d02b93e2be0e60ef6505767862875`
reads misa and sets MSTATUS_FS if it detects F or D; this source check
does not establish that both physical harts have usable F or that FS
transitions are correct. Candidate artifacts and build logs remain on Orion:
`~/source/build/s31-lab-f-only-pinned-20261126/` and
`~/source/build/s31-lab-f-only-dtb-20261126/`. At this build-only checkpoint, no candidate had been
booted or written to flash. All v1 rollback SHA256SUMS passed, and
the UART was free at the end of the session. The experimental
candidate is not a production image or a release.

## Physical F-only qualification (Orion, 2026-11-26)

The candidate XIP and isolated DTB from the preceding section were
tested on the Function-CoreBoard-1 (rev 0.0) using the official
S31 Lab v1 SPL, U-Boot/OpenSBI and CramFS rootfs. Before the experiment,
all five existing flash artifacts were verified against the released
v1 image with esptool 5.3.1. The UART was confirmed free.
Only the DTB (0x300000) and XIP kernel (0x500000) were written.
Both were digest-verified before boot. No radio payload or credentials
were transferred.

Boot evidence:

```text
Linux version 6.18.0+
riscv: base ISA extensions acfim
riscv: ELF capabilities acfim
0-1
mtd:rootfs / cramfs ro,relatime 0 0
isa : rv32imafc_zicsr_zifencei_zaamo_zalrsc_zca_zcf
isa : rv32imafc_zicsr_zifencei_zaamo_zalrsc_zca_zcf
MemAvailable: 10356 kB
F_ONLY_CANDIDATE_BOOT_PASS
```

The exact same stripped soft-float diagnostic as the baseline
(SHA-256 `e12c92fff0bc61dba0551809f5c2105d1b3ab4d414883e0dfcedfee5b1185607`)
was transported in RAM and digest-verified on target. Its output:

```text
HWCAP=0x1125 F=1 D=0 HWPROBE_FD=0
FADD_PASS hart=0
FADD_PASS hart=1
SIGNAL_PASS
SMP_CONTEXT_PASS seed=3 harts=0,1
SMP_CONTEXT_PASS seed=4 harts=0,1
PTRACE_PASS
S31_F_ONLY_PASS
F_ONLY_HARDWARE_PASS
```

The suite tested preservation of all 32 32-bit registers and FCSR across
signal delivery and restoration, plus two independent processes with
128 affinity/check iterations each. It validated ptrace GET/SETREGSET,
including discarding sentinel high words and zero padding on the returned
64-bit slots. This confirms the experimental F-only context path on
this board, not a generic ABI decision or support for D.
The test executable was removed from RAM.

Regression smoke checks passed: two online harts, Ethernet interface
presence, RGB LED sysfs presence, GPIO character device, I2C adapter,
ES8311 identification registers (0x83 0x11 0x01), read-only USB mass
storage read (64 blocks of 4096 bytes), disabled physical CAN controllers
as expected for the default DTB, and three successful loopback pings.
Ethernet DHCP/external traffic, active RGB control, CAN loopback,
candidate software reboot, long-duration FP stress, signal frame
mutation, core dumps, D/no-F regressions and actual ILP32F radio
execution remain separate qualification gates. The USB device emitted
an alternate GPT invalid/primary GPT selected warning; no disk content
was modified. No kernel panic/oops was seen in the captured candidate
UART log.

The candidate runner is now in `experiments/s31-fpu/run-candidate.py`.
It asserts the exact candidate profile and per-test markers and captures
a local serial trace. Example (with a verified candidate already booting
on the bench; do not use it as a flashing tool):

```sh
python3 experiments/s31-fpu/run-candidate.py \
  --helper /path/to/trusted/run-uart-final.py \
  --gzip /path/to/fpu-test.gz \
  --sha256 e12c92fff0bc61dba0551809f5c2105d1b3ab4d414883e0dfcedfee5b1185607 \
  --log /path/to/new-candidate-uart.log
```

Raw logs and the guarded, bench-specific flash/rollback script are retained
on Orion under `~/source/build/s31-f-only-hardware-20261126/`.
No raw UART logs, flash dumps, machine identifiers, or private data are
published.

### Rollback confirmation

The guarded script restored both original v1 components and verified all
five v1 artifact digests against the physical flash. The board booted
the restored dual-core CramFS profile and the exact same diagnostic
returned status 77, as intended:

```text
RESTORE_WRITE_PASS
RESTORE_ALL_SLOTS_PASS
HWCAP=0x1105 F=0 D=0 HWPROBE_FD=0
FPU_NOT_ADVERTISED: hardware opcodes not executed
FPU_BASELINE_PASS
TARGET_ARTIFACT_REMOVED
RESTORE_BASELINE_PASS
```

The complete guarded session returned exit status 0; UART was free after
the run. The signed-off v1 rollback files were not modified.
Local evidence digests (logs themselves are not distributed):

- candidate-test.log: `205457858cb05264d0a3e8cb3e1b3a81ae7db3319491810bb25cc9ef6ef8502f`
- restore-verify.log: `076a07aad5a3bc750c402383fcf0fb78ac545b15b49373a32b4e2d6fab435710`
- baseline-restored.log: `3c12d6d994b36383d170e2a8f4596695edd990d3e638c0b69993ad77e0cdaa13`
