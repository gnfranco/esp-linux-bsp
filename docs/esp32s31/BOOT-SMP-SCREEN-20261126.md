# S31 Lab v1: online-CPU boot comparison (2026-11-26)

## Scope

Original Lab v1 kernel `9b41c9c5df88ed883b05662187e431bb3452e491`,
XIP SHA-256 `5fe191d8e189a229f68848791bdb22b851b1b1922ca3f4b0a150030ba0f406ed`,
original no-F DTB, two physical harts, 16 MiB PSRAM/NOR, read-only
CramFS. All image components unchanged. This is a follow-up to
[BOOT-FAULT-SCREEN-20261126.md](BOOT-FAULT-SCREEN-20261126.md),
not a test of the F-only branch.

U-Boot allows temporary boot arguments; the installed environment is
loaded from nowhere. Interrupt autoboot only at its banner, confirm
the actual `esp32s31>` prompt, and use the installed baseline boot
command with either the baseline argument string or the exact
additional `maxcpus=1` parameter. No persistent environment writes,
no flash writes. The kernel remains SMP-built in both modes; one
online CPU does not establish how many harts the firmware initialized.
The serial runner checks the target boot arguments, online CPU count,
no-F ISA, root filesystem and boot ID. The first attempt used the wrong
U-Boot prompt and was excluded; read-only recovery restored two harts.

## Initial alternating series

| Pair | One online CPU | Two online CPUs |
|---:|---|---|
| 1 | PASS 6.41 s | KERNEL_OOPS (32.99 s timeout, Oops observed) |
| 2 | PASS 6.45 s | KERNEL_PANIC (5.89 s) |
| 3 | PASS 6.44 s | PASS 6.35 s |
| 4 | PASS 6.43 s | PASS 6.35 s |
| 5 | PASS 6.46 s | PASS 6.35 s |
| Total | 5/5 | 3/5 |

Both failures match prior baseline signatures. Pair 1: CPU1,
esp-example-tar, EPC c00af0e0 (`anon_vma_clone+0xb8`), VA 0x11e,
s2=0x11a, cause 0xd. Pair 2: two Oops with EPC c00a1de2
(`vma_last_pgoff`) and VA 0x4, followed by init panic.
Read-only ROM-assisted RTS recovery succeeded for both.
Final boot with two harts passed; UART released.

The original xipImage is byte-identical to the preserved
`s31-ctucanfd-qa-20261126` vmlinux build. Its disassembly identifies
the invalid base register in the read of anonymous VMA chain
metadata. This locates the failure, not the origin of the bad
metadata. Its provenance remains unknown; a failure in the
list traversal does not demonstrate an error in generic MM code.

The contrast supports further investigation of SMP-related
conditions but is not conclusive. Small sample; changing CPU count
also changes paths and timing. Alternation couples pre-reset and
post-reset CPU counts. RTS resets are not verified removal of
physical power. Do not infer an independent probability of failure
from this design or declare the single-core mode reliable.

## Independent diagnostic build

The original kernel commit, not the Fpu v2 branch, is checked out in
`~/worktrees/linux-s31-debug-list`, branch
`experiment/s31-boot-debug-list`, on Linvora-dev. No source changes.
Use `experiments/s31-reboot/build-debug-list-linvora.sh` with the
pinned release config and pinned GCC 14.1.1, isolated output.
The actual configuration delta is exactly DEBUG_LIST and the
LIST_HARDENED dependency selected by it; DEBUG_KERNEL was already
enabled. SMP remains enabled and F stays unadvertised. The
relevant mm/rmap.o and mm/vma.o compiled normally; rmap.o references
the list-validation helper. The full XIP/DTB build is still running
in `~/deps/s31-research/kernel-v1-debug-list-build/` (PID 17972 at
the time of writing). Do not flash until complete and slot size,
config and hashes are verified.

Codex CLI reviewed the supplied technical evidence in read-only mode
(Linvora private report `~/deps/s31-research/codex-s31-smp-diagnostic-review.md`).
It recommended crossing pre- and post-reset counts, controlling
timing, and only then using a one-variable diagnostic build.
DEBUG_LIST may detect a prior inconsistency or alter timing; absence
of the observed crash under instrumentation is not proof of repair.
No changes to generic memory-management code, host settings,
OpenSBI, or release configuration were made.

## Reproducibility

The runner `experiments/s31-reboot/maxcpus-screen.py` supports the
alternating comparison and a balanced sequence crossing counts.
Use only on the authorized physical bench with free UART, verified
v1 image and rollback available. It stops on an inconclusive
boot profile or failed recovery. Logs remain private under
`~/source/build/s31-maxcpus-screen-20261126/` on Orion.

    python3 experiments/s31-reboot/maxcpus-screen.py \
        --helper experiments/s31-native-radio/run-uart.py \
        --esptool /path/to/esptool --out /private/new-log-directory \
        --pairs 3 --balanced

The build runner is separate from physical test execution. All
compilation remains in Linvora-dev.

## Follow-up: crossed pre/post-reset CPU counts

The second series used three blocks with source/destination transitions
2->1, 1->1, 1->2, 2->2. A separate initial console open caused a ROM
reported POWERON boot with the exact previous `anon_vma_clone` Oops
(c00af0e0, VA 0x11e); this was not a warm-reboot trial. Read-only
RTS recovery passed before the controlled series.

| Transition | Completed | Passed | Failed |
|---|---:|---:|---:|
| 2 -> 1 | 3 | 3 | 0 |
| 1 -> 1 | 3 | 3 | 0 |
| 1 -> 2 | 3 | 3 | 0 |
| 2 -> 2 | 3 | 3 | 0 |

All 12 controlled transitions passed (6.27–6.57 s).
Combined with the earlier alternating screen, 11/11 one-core
destination boots and 9/11 two-core destination boots passed
(2 failures), but the two runs are not identically randomized
or independent and the initial console-failure is excluded.
The second series did not reproduce a source/destination effect.
This reinforces intermittency and does not establish that SMP
itself is faulty or that maxcpus=1 is a reliable fix.

The board finished the final pair in the two-hart v1 profile, UART
released. No flash writes or persistent configuration changes.
Continue with the independently built list-check diagnostic and
a confirmed physical power-removal control when available.
Preserve SMP as a project requirement.
